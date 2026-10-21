// Mosics for Windows — a tiny audio-only music player with a vinyl-record look.
// Same design and features as the Mac app. Pure Win32 + GDI/GDI+ + Media Foundation (MFPlay) + WinHTTP.
// Search and suggestions come from YouTube Music; audio comes through yt-dlp.
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <psapi.h>
#include <shellapi.h>

#include "ui.h"

enum : int {
    IDC_SEARCH = 100,
    TIMER_TICK = 1, TIMER_SEARCH, TIMER_SPIN, TIMER_ANIM, TIMER_ART,
    HOTKEY_PLAY = 1, HOTKEY_NEXT, HOTKEY_PREV,
    ID_PLAY = 1000, ID_PLAYNEXT, ID_ADDQUEUE, ID_RADIO, ID_LIKE, ID_NEWPL_WITH, ID_REMOVE_PL, ID_REMOVE_Q, ID_COPYLINK,
    ID_PL_RENAME, ID_PL_COVER, ID_PL_DELETE, ID_SAVE_MIX,
    ID_TRAY_PLAY, ID_TRAY_NEXT, ID_TRAY_PREV, ID_TRAY_LIKE, ID_TRAY_OPEN, ID_TRAY_QUIT,
    ID_ADDPL_BASE = 2000,
};

static HINSTANCE g_inst;
static HWND g_search;
static HFONT g_editFont;
static HBRUSH g_brCard, g_brPaper;
static ULONG_PTR g_gdiplusToken;
static HICON g_iconSmall;
static NOTIFYICONDATAW g_nid;
static UINT g_taskbarCreated;
static bool g_visible = true, g_quitting = false, g_spinTimer = false, g_ignoreEdit = false, g_focusSearch = false;
static ULONGLONG g_lastSpin = 0;
static int g_lastSecond = -1;
static int g_pressAct = aNone, g_pressArg = 0;

// Back buffer: the whole window is drawn here, then copied to the screen.
static HDC g_mem;
static HBITMAP g_memBmp;
static int g_memW, g_memH;

struct ArtResult { string url; Bitmap* bmp; };

// ───────────────────────────── Rendering ─────────────────────────────

static void FreeBuffer() {
    if (g_mem) { DeleteDC(g_mem); g_mem = nullptr; }
    if (g_memBmp) { DeleteObject(g_memBmp); g_memBmp = nullptr; }
    g_memW = g_memH = 0;
}

static void EnsureBuffer(HDC ref, int w, int h) {
    if (g_mem && w == g_memW && h == g_memH) return;
    FreeBuffer();
    g_mem = CreateCompatibleDC(ref);
    g_memBmp = CreateCompatibleBitmap(ref, std::max(1, w), std::max(1, h));
    SelectObject(g_mem, g_memBmp);
    g_memW = w;
    g_memH = h;
}

static void BeginRender(const RECT& rc) {
    R.dc = g_mem;
    R.paint = rc;
    R.clips.clear();
    R.states.clear();
    SaveDC(g_mem);
    IntersectClipRect(g_mem, rc.left, rc.top, rc.right, rc.bottom);
    R.g = new Gdiplus::Graphics(g_mem);
    R.g->SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    R.g->SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    R.g->SetInterpolationMode(Gdiplus::InterpolationModeBilinear);
    R.g->SetClip(Gdiplus::Rect(rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top));
}

static void EndRender() {
    R.g->Flush(Gdiplus::FlushIntentionSync);
    delete R.g;
    R.g = nullptr;
    RestoreDC(g_mem, -1);
    R.dc = nullptr;
}

static void StartAnimating() {
    if (!g_animating && g_hwnd && g_visible) {
        g_animating = true;
        SetTimer(g_hwnd, TIMER_ANIM, 16, nullptr);
    }
}

static void UpdateSpinTimer() {
    bool need = g_visible && !IsIconic(g_hwnd) && !g_spinJobs.empty();
    if (need == g_spinTimer) return;
    g_spinTimer = need;
    if (need) { g_lastSpin = GetTickCount64(); SetTimer(g_hwnd, TIMER_SPIN, 40, nullptr); }
    else KillTimer(g_hwnd, TIMER_SPIN);
}

// Turns the records: each spinning record is redrawn on its own (not the whole window), ~25 times a second.
static void SpinTick() {
    ULONGLONG now = GetTickCount64();
    g_spinAngle = std::fmod(g_spinAngle + (now - g_lastSpin) / 1000.0 * 200.0, 360.0);   // 33⅓ rpm: 1.8 s a turn
    g_lastSpin = now;
    if (!g_mem || g_spinJobs.empty()) return;
    HDC wdc = GetDC(g_hwnd);
    for (const SpinJob& j : g_spinJobs) {
        float l = std::max(j.box.x, j.clip.x), t = std::max(j.box.y, j.clip.y);
        float r = std::min(j.box.x + j.box.w, j.clip.x + j.clip.w), b = std::min(j.box.y + j.box.h, j.clip.y + j.clip.h);
        if (r <= l || b <= t) continue;
        RECT rc{(int)std::floor(l * g_k), (int)std::floor(t * g_k), (int)std::ceil(r * g_k), (int)std::ceil(b * g_k)};
        BeginRender(rc);
        FillRect(l, t, r - l, b - t, j.bg);
        j.draw();
        EndRender();
        BitBlt(wdc, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, g_mem, rc.left, rc.top, SRCCOPY);
    }
    ReleaseDC(g_hwnd, wdc);
}

static void InvalidateDip(const ClipRect& c) {
    RECT rc{(int)std::floor(c.x * g_k) - 2, (int)std::floor(c.y * g_k) - 2, (int)std::ceil((c.x + c.w) * g_k) + 2,
            (int)std::ceil((c.y + c.h) * g_k) + 2};
    InvalidateRect(g_hwnd, &rc, FALSE);
}

// ───────────────────────────── Window helpers ─────────────────────────────

static void UpdateTray() {
    if (!g_nid.hWnd) return;
    const Track* t = g.current();
    wstring tip = t ? W(t->title) + L" \x2014 " + W(t->artist) : L"Mosics";
    if (tip.size() > 120) tip = tip.substr(0, 117) + L"\x2026";
    lstrcpynW(g_nid.szTip, tip.c_str(), ARRAYSIZE(g_nid.szTip));
    g_nid.uFlags = NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

static void UpdateTitle() {
    const Track* t = g.current();
    SetWindowTextW(g_hwnd, t ? (W(t->title) + L" \x00B7 " + W(t->artist) + L" \x2014 Mosics").c_str() : L"Mosics");
    UpdateTray();
}

static void AddTray() {
    g_nid = {};
    g_nid.cbSize = sizeof g_nid;
    g_nid.hWnd = g_hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_APP_TRAY;
    g_nid.hIcon = g_iconSmall;
    lstrcpyW(g_nid.szTip, L"Mosics");
    Shell_NotifyIconW(NIM_ADD, &g_nid);
    UpdateTray();
}

static void Navigate(Page p, const string& id = "") {
    g.page = p;
    g.pageId = id;
    g.nowPlaying = false;
    g_scroll = g_scrollTarget = 0;
    if (p == Page::Home) RefreshShelves(false);
    Redraw();
    PostMessageW(g_hwnd, WM_APP_LAYOUT, 0, 0);
    if (p == Page::Search) {
        g_focusSearch = true;   // once the box is placed (after the next paint)
    } else {
        SetFocus(g_hwnd);
    }
}

// Closing the window keeps the music playing; the tray icon brings it back (like the Mac's menu-bar record).
static void HideToTray() {
    ShowWindow(g_hwnd, SW_HIDE);
    g_visible = false;
    KillTimer(g_hwnd, TIMER_ANIM);
    KillTimer(g_hwnd, TIMER_TICK);
    g_animating = false;
    UpdateSpinTimer();
    // Let go of everything that only the window needs: pixels, decoded artwork, fonts.
    PurgeArt();
    ClearFonts();
    FreeBuffer();
    g_spinJobs.clear();
    g_hits.clear();
    SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);
    if (!g.trayHintShown) {
        g.trayHintShown = true;
        SaveSettings();
        g_nid.uFlags = NIF_INFO;
        lstrcpyW(g_nid.szInfoTitle, L"Mosics is still playing");
        lstrcpyW(g_nid.szInfo, L"Closing the window keeps the music going. Use the record icon here to control it or quit.");
        g_nid.dwInfoFlags = NIIF_NONE;
        Shell_NotifyIconW(NIM_MODIFY, &g_nid);
    }
}

static void ShowMain() {
    if (!g_visible) {
        g_visible = true;
        ShowWindow(g_hwnd, SW_SHOW);
        SetTimer(g_hwnd, TIMER_TICK, 500, nullptr);
    }
    if (IsIconic(g_hwnd)) ShowWindow(g_hwnd, SW_RESTORE);
    SetForegroundWindow(g_hwnd);
    Redraw();
}

static void Quit() {
    g_quitting = true;
    DestroyWindow(g_hwnd);
}

// ───────────────────────────── Prompt dialog ─────────────────────────────

static bool PromptText(const wchar_t* title, wstring& value) {
    struct State { bool done = false, ok = false; HWND edit = nullptr; wstring* value; } st{false, false, nullptr, &value};
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = [](HWND h, UINT m, WPARAM w, LPARAM l) -> LRESULT {
            auto* s = (State*)GetWindowLongPtrW(h, GWLP_USERDATA);
            switch (m) {
            case WM_COMMAND:
                if (LOWORD(w) == IDOK || LOWORD(w) == IDCANCEL) {
                    if (LOWORD(w) == IDOK) {
                        int n = GetWindowTextLengthW(s->edit);
                        s->value->resize(n);
                        GetWindowTextW(s->edit, s->value->data(), n + 1);
                    }
                    s->ok = LOWORD(w) == IDOK;
                    s->done = true;
                }
                return 0;
            case WM_CLOSE: s->done = true; return 0;
            case WM_CTLCOLOREDIT:
                SetTextColor((HDC)w, CR(pal::ink));
                SetBkColor((HDC)w, CR(pal::card));
                return (LRESULT)g_brCard;
            case WM_CTLCOLORSTATIC: case WM_CTLCOLORBTN:
                SetBkColor((HDC)w, CR(pal::paper));
                return (LRESULT)g_brPaper;
            }
            return DefWindowProcW(h, m, w, l);
        };
        wc.hInstance = g_inst;
        wc.hbrBackground = g_brPaper;
        wc.lpszClassName = L"MosicsPrompt";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        RegisterClassW(&wc);
        registered = true;
    }
    RECT orc;
    GetWindowRect(g_hwnd, &orc);
    if (!g_visible) SystemParametersInfoW(SPI_GETWORKAREA, 0, &orc, 0);
    int w = IPX(380), h = IPX(156);
    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"MosicsPrompt", title, WS_POPUP | WS_CAPTION | WS_SYSMENU,
                               (orc.left + orc.right - w) / 2, (orc.top + orc.bottom - h) / 2, w, h, g_hwnd, nullptr,
                               g_inst, nullptr);
    COLORREF cap = CR(pal::paper);
    DwmSetWindowAttribute(dlg, 35 /* DWMWA_CAPTION_COLOR */, &cap, sizeof cap);
    SetWindowLongPtrW(dlg, GWLP_USERDATA, (LONG_PTR)&st);
    RECT cr;
    GetClientRect(dlg, &cr);
    st.edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", value.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                              IPX(18), IPX(18), cr.right - IPX(36), IPX(30), dlg, (HMENU)100, g_inst, nullptr);
    HWND ok = CreateWindowExW(0, L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                              cr.right - IPX(212), cr.bottom - IPX(48), IPX(94), IPX(30), dlg, (HMENU)IDOK, g_inst, nullptr);
    HWND cancel = CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                  cr.right - IPX(112), cr.bottom - IPX(48), IPX(94), IPX(30), dlg, (HMENU)IDCANCEL, g_inst, nullptr);
    for (HWND c : {st.edit, ok, cancel}) SendMessageW(c, WM_SETFONT, (WPARAM)g_editFont, TRUE);
    SendMessageW(st.edit, EM_SETSEL, 0, -1);
    EnableWindow(g_hwnd, FALSE);
    ShowWindow(dlg, SW_SHOW);
    SetForegroundWindow(dlg);
    SetFocus(st.edit);
    MSG msg;
    while (!st.done && GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(dlg, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    EnableWindow(g_hwnd, TRUE);
    DestroyWindow(dlg);
    if (g_visible) SetForegroundWindow(g_hwnd);
    return st.ok;
}

static void RenamePlaylist(const string& id) {
    Playlist* p = FindPlaylist(id);
    if (!p) return;
    wstring name = W(p->name);
    if (PromptText(L"Rename playlist", name) && !Trim(U8(name)).empty()) {
        if ((p = FindPlaylist(id))) { p->name = Trim(U8(name)); SaveLibrary(); }
    }
    Redraw();
}

// ───────────────────────────── Playlist covers ─────────────────────────────

static bool JpegEncoder(CLSID& out) {
    UINT n = 0, size = 0;
    Gdiplus::GetImageEncodersSize(&n, &size);
    if (!size) return false;
    vector<BYTE> buf(size);
    auto* enc = (Gdiplus::ImageCodecInfo*)buf.data();
    Gdiplus::GetImageEncoders(n, size, enc);
    for (UINT i = 0; i < n; ++i)
        if (wcscmp(enc[i].MimeType, L"image/jpeg") == 0) { out = enc[i].Clsid; return true; }
    return false;
}

// Copies a picture in as the playlist cover: square-cropped to 600 px, saved as a small JPEG.
static bool ImportCover(const wstring& path, const string& id) {
    Bitmap* src = LoadImageFile(path, 0);
    if (!src) return false;
    int side = (int)std::min(src->GetWidth(), src->GetHeight());
    Bitmap out(600, 600, PixelFormat24bppRGB);
    {
        Gdiplus::Graphics gg(&out);
        gg.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        gg.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        gg.DrawImage(src, Gdiplus::Rect(0, 0, 600, 600), (INT)(src->GetWidth() - side) / 2, (INT)(src->GetHeight() - side) / 2,
                     side, side, Gdiplus::UnitPixel);
    }
    delete src;
    CLSID jpeg;
    if (!JpegEncoder(jpeg)) return false;
    Gdiplus::EncoderParameters params;
    ULONG quality = 85;
    params.Count = 1;
    params.Parameter[0] = {Gdiplus::EncoderQuality, 1, Gdiplus::EncoderParameterValueTypeLong, &quality};
    string file = id + "-" + std::to_string((long long)NowSeconds()) + ".jpg";
    if (out.Save(CoverPath(file).c_str(), &jpeg, &params) != Gdiplus::Ok) return false;
    SetCover(id, Playlist::Image, 0, file);
    return true;
}

static void ChooseCoverImage(const string& id) {
    wchar_t file[MAX_PATH] = L"";
    OPENFILENAMEW of{};
    of.lStructSize = sizeof of;
    of.hwndOwner = g_hwnd;
    of.lpstrFilter = L"Pictures\0*.jpg;*.jpeg;*.png;*.bmp;*.gif;*.tif;*.tiff\0All files\0*.*\0";
    of.lpstrFile = file;
    of.nMaxFile = MAX_PATH;
    of.lpstrTitle = L"Choose a picture for this playlist's cover";
    of.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&of) && !ImportCover(file, id))
        MessageBoxW(g_hwnd, L"Couldn't use that picture.", L"Mosics", MB_OK | MB_ICONWARNING);
}

// ───────────────────────────── Menus ─────────────────────────────

static void CopyText(const wstring& s) {
    if (!OpenClipboard(g_hwnd)) return;
    EmptyClipboard();
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (s.size() + 1) * sizeof(wchar_t));
    if (mem) {
        memcpy(GlobalLock(mem), s.c_str(), (s.size() + 1) * sizeof(wchar_t));
        GlobalUnlock(mem);
        SetClipboardData(CF_UNICODETEXT, mem);
    }
    CloseClipboard();
}

// The right-click menu shared everywhere a song appears.
static void TrackMenu(const Track& t, int kind, int index, POINT pt) {
    HMENU m = CreatePopupMenu(), pl = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, ID_PLAY, L"Play");
    AppendMenuW(m, MF_STRING, ID_PLAYNEXT, L"Play Next");
    AppendMenuW(m, MF_STRING, ID_ADDQUEUE, L"Add to Queue");
    AppendMenuW(m, MF_STRING, ID_RADIO, L"Start Radio");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_LIKE, IsLiked(t.id) ? L"Remove from Liked Songs" : L"Save to Liked Songs");
    AppendMenuW(pl, MF_STRING, ID_NEWPL_WITH, L"New Playlist");
    if (!g.playlists.empty()) AppendMenuW(pl, MF_SEPARATOR, 0, nullptr);
    for (size_t i = 0; i < g.playlists.size(); ++i) AppendMenuW(pl, MF_STRING, ID_ADDPL_BASE + i, W(g.playlists[i].name).c_str());
    AppendMenuW(m, MF_POPUP, (UINT_PTR)pl, L"Add to Playlist");
    if (kind == kPlaylist) AppendMenuW(m, MF_STRING, ID_REMOVE_PL, L"Remove from This Playlist");
    if (kind == kQueue) AppendMenuW(m, MF_STRING, ID_REMOVE_Q, L"Remove from Queue");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_COPYLINK, L"Copy Song Link");
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_hwnd, nullptr);
    DestroyMenu(m);   // also destroys the submenu
    switch (cmd) {
    case ID_PLAY: PlayTracks({t}); break;
    case ID_PLAYNEXT: PlayNext(t); break;
    case ID_ADDQUEUE: AddToQueue(t); break;
    case ID_RADIO: StartRadio(t); break;
    case ID_LIKE: ToggleLike(t); break;
    case ID_NEWPL_WITH: AddToPlaylist(t, NewPlaylist(t.title)); break;
    case ID_REMOVE_PL:
        if (Playlist* p = FindPlaylist(g.pageId); p && index >= 0 && index < (int)p->tracks.size()) {
            p->tracks.erase(p->tracks.begin() + index);
            SaveLibrary();
        }
        break;
    case ID_REMOVE_Q: RemoveUpNext(index); break;
    case ID_COPYLINK: CopyText(L"https://music.youtube.com/watch?v=" + W(t.id)); break;
    default:
        if (cmd >= ID_ADDPL_BASE && cmd < ID_ADDPL_BASE + (int)g.playlists.size()) AddToPlaylist(t, g.playlists[cmd - ID_ADDPL_BASE].id);
    }
    Redraw();
}

static void PlaylistMenu(const string& id, POINT pt) {
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, ID_PL_RENAME, L"Rename\x2026");
    AppendMenuW(m, MF_STRING, ID_PL_COVER, L"Change Cover\x2026");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_PL_DELETE, L"Delete Playlist");
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_hwnd, nullptr);
    DestroyMenu(m);
    Playlist* p = FindPlaylist(id);
    if (!p) return;
    if (cmd == ID_PL_RENAME) RenamePlaylist(id);
    else if (cmd == ID_PL_COVER) g.coverSheet = id;
    else if (cmd == ID_PL_DELETE) {
        wstring msg = L"Delete \"" + W(p->name) + L"\"?";
        if (MessageBoxW(g_hwnd, msg.c_str(), L"Mosics", MB_OKCANCEL | MB_ICONQUESTION) != IDOK) return;
        if (g.page == Page::Playlist && g.pageId == id) Navigate(Page::Home);
        DeletePlaylist(id);
    }
    Redraw();
}

static void TrayMenu() {
    POINT pt;
    GetCursorPos(&pt);
    const Track* t = g.current();
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING | MF_DISABLED, 0, t ? W(t->title).c_str() : L"Nothing playing");
    if (t) AppendMenuW(m, MF_STRING | MF_DISABLED, 0, W(t->artist).c_str());
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    UINT on = t ? MF_STRING : MF_STRING | MF_GRAYED;
    AppendMenuW(m, on, ID_TRAY_PLAY, g.playing ? L"Pause" : L"Play");
    AppendMenuW(m, on, ID_TRAY_NEXT, L"Next");
    AppendMenuW(m, on, ID_TRAY_PREV, L"Previous");
    if (t) AppendMenuW(m, MF_STRING, ID_TRAY_LIKE, IsLiked(t->id) ? L"Remove from Liked Songs" : L"Save to Liked Songs");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_TRAY_OPEN, L"Open Mosics");
    AppendMenuW(m, MF_STRING, ID_TRAY_QUIT, L"Quit Mosics");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | MF_DISABLED, 0, (L"Mosics " + W(MOSICS_VERSION)).c_str());
    SetMenuDefaultItem(m, ID_TRAY_OPEN, FALSE);
    SetForegroundWindow(g_hwnd);   // so the menu closes when you click elsewhere
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_hwnd, nullptr);
    DestroyMenu(m);
    PostMessageW(g_hwnd, WM_NULL, 0, 0);
    switch (cmd) {
    case ID_TRAY_PLAY: TogglePlay(); break;
    case ID_TRAY_NEXT: Next(); break;
    case ID_TRAY_PREV: ReadClock(); Previous(); break;
    case ID_TRAY_LIKE: if (const Track* c = g.current()) ToggleLike(*c); break;
    case ID_TRAY_OPEN: ShowMain(); break;
    case ID_TRAY_QUIT: Quit(); break;
    }
}

// ───────────────────────────── Actions ─────────────────────────────

static vector<const Shelf*> HomeMixes() {
    vector<const Shelf*> out;
    for (auto& s : g.shelves) if (s.id != "recent") out.push_back(&s);
    return out;
}

static void PlayRow(int i) {
    if (i < 0 || i >= (int)g_view.size()) return;
    if (g_viewKind == kQueue) JumpUpNext(i); else PlayTracks(g_view, i);
}

static void ScrollShelf(int shelf, float by) {
    g_shelfTarget[shelf] = std::clamp(g_shelfTarget[shelf] + by, 0.0f, g_shelfMax[shelf]);
    StartAnimating();
}

static void Activate(int act, int arg) {
    switch (act) {
    case aNav: Navigate((Page)arg); break;
    case aSidePlaylist: if (arg < (int)g.playlists.size()) Navigate(Page::Playlist, g.playlists[arg].id); break;
    case aNewPlaylist: {
        string id = NewPlaylist("");
        Navigate(Page::Playlist, id);
        RenamePlaylist(id);
        break;
    }
    case aRowPlay: PlayRow(arg); break;
    case aRowLike: if (arg < (int)g_view.size()) ToggleLike(g_view[arg]); break;
    case aPlayPause: TogglePlay(); break;
    case aNext: Next(); break;
    case aPrev: ReadClock(); Previous(); break;
    case aShuffle: ToggleShuffle(); break;
    case aRepeat: CycleRepeat(); break;
    case aLikeCurrent: if (const Track* t = g.current()) ToggleLike(*t); break;
    case aQueuePage: Navigate(Page::Queue); break;
    case aOpenNow: case aHomeSleeve: if (g.current()) { g.nowPlaying = true; SetFocus(g_hwnd); PostMessageW(g_hwnd, WM_APP_LAYOUT, 0, 0); } break;
    case aCloseNow: g.nowPlaying = false; PostMessageW(g_hwnd, WM_APP_LAYOUT, 0, 0); break;
    case aPlayAll: if (!g_view.empty()) { g.shuffle = false; SaveSettings(); PlayTracks(g_view, 0); } break;
    case aShuffleAll:
        if (!g_view.empty()) {
            g.shuffle = true;
            SaveSettings();
            PlayTracks(g_view, (int)(std::mt19937{std::random_device{}()}() % g_view.size()));
        }
        break;
    case aMoreMenu: {
        POINT pt;
        GetCursorPos(&pt);
        PlaylistMenu(g.pageId, pt);
        break;
    }
    case aEditCover: g.coverSheet = g.pageId; break;
    case aSaveMix:
        if (const Shelf* s = FindShelf(g.pageId)) {
            vector<Track> tracks = s->tracks;
            string id = NewPlaylist(s->title);
            for (auto& t : tracks) AddToPlaylist(t, id);
            Navigate(Page::Playlist, id);
        }
        break;
    case aNewMixes: RefreshShelves(true); break;
    case aSeeAll: Navigate(Page::Recent); break;
    case aRecentTile: if (arg < (int)g.recent.size()) PlayTracks(g.recent, arg); break;
    case aMixTile: {
        auto mixes = HomeMixes();
        if (arg < (int)mixes.size()) Navigate(Page::Mix, mixes[arg]->id);
        break;
    }
    case aSearchPill: Navigate(Page::Search); break;
    case aKind:
        g.searchKind = arg;
        SaveSettings();
        StartSearch();
        break;
    case aClearSearch:
        g_ignoreEdit = true;
        SetWindowTextW(g_search, L"");
        g_ignoreEdit = false;
        g.query.clear();
        SaveSettings();
        StartSearch();
        SetFocus(g_search);
        break;
    case aSearchField: SetFocus(g_search); break;
    case aAutoplay: g.autoplay = !g.autoplay; SaveSettings(); break;
    case aClearQueue: ClearUpNext(); break;
    case aStartRadio: if (const Track* t = g.current()) StartRadio(*t); break;
    case aShelfPrev: ScrollShelf(arg, -(g_shelfBox[arg].w - 72) * 0.8f); break;
    case aShelfNext: ScrollShelf(arg, (g_shelfBox[arg].w - 72) * 0.8f); break;
    case aSheetStyle: SetCover(g.coverSheet, Playlist::Style, arg); break;
    case aSheetArtwork: SetCover(g.coverSheet, Playlist::Artwork); break;
    case aSheetImage: ChooseCoverImage(g.coverSheet); break;
    case aSheetReset: SetCover(g.coverSheet, Playlist::Auto); break;
    case aSheetDone: g.coverSheet.clear(); break;
    case aSetupGo:
        if (g_setup.phase == 2) { g_setup.visible = false; g.error.clear(); RefreshShelves(false); }
        else StartSetup();
        break;
    case aSetupLater:
        g_setup.visible = false;
        if (HelpersReady() && !IsInstalledCopy()) { g.declinedInstall = DeclineKey(); SaveSettings(); }
        break;
    default: break;
    }
    Redraw();
}

static void ContextMenuAt(float x, float y, POINT screen) {
    const Hit* h = HitAt(x, y);
    if (!h) return;
    int act = h->act, arg = h->arg;
    if ((act == aRow || act == aRowPlay || act == aRowLike) && arg < (int)g_view.size()) {
        Track t = g_view[arg];
        TrackMenu(t, g_viewKind, arg, screen);
    } else if (act == aRecentTile && arg < (int)g.recent.size()) {
        Track t = g.recent[arg];
        TrackMenu(t, kPlain, -1, screen);
    } else if (act == aUpNextRow) {
        vector<Track> up = g.upNext();
        if (arg < (int)up.size()) TrackMenu(up[arg], kQueue, arg, screen);
    } else if (act == aSidePlaylist && arg < (int)g.playlists.size()) {
        PlaylistMenu(g.playlists[arg].id, screen);
    } else if ((act == aOpenNow || act == aHomeSleeve) && g.current()) {
        Track t = *g.current();
        TrackMenu(t, kPlain, -1, screen);
    }
}

// ───────────────────────────── Mouse ─────────────────────────────

static void DragTo(float x, float y) {
    switch (g_drag.act) {
    case aSeekBar: case aSeekNow: {
        const ClipRect& t = g_seekTrack[g_drag.act == aSeekNow];
        g_drag.value = std::clamp((x - t.x) / std::max(1.0f, t.w), 0.0f, 1.0f) * std::max(g.dur, 1.0);
        Redraw();
        break;
    }
    case aVolume: SetVolume(g_drag.v0 + ((g_drag.y0 - y) + (x - g_drag.x0)) / 160.0f); break;
    case aScrollbar:
        g_scroll = g_scrollTarget = std::clamp(g_drag.v0 + (y - g_drag.y0) * (g_contentH / std::max(1.0f, g_viewH)), 0.0f,
                                               std::max(0.0f, g_contentH - g_viewH));
        Redraw();
        PostMessageW(g_hwnd, WM_APP_LAYOUT, 0, 0);
        break;
    }
}

static void EndDrag() {
    if (g_drag.act == aSeekBar || g_drag.act == aSeekNow) SeekTo(g_drag.value);
    if (g_drag.act == aVolume) SaveSettings();
    g_drag.act = aNone;
    ReleaseCapture();
    Redraw();
}

static void MouseDown(float x, float y) {
    const Hit* h = HitAt(x, y);
    if (!h || h->act != aSearchField) SetFocus(g_hwnd);
    if (!h) return;
    if (h->act == aSeekBar || h->act == aSeekNow || h->act == aVolume || h->act == aScrollbar) {
        g_drag = {h->act, x, y, h->act == aVolume ? g.volume : g_scroll, g.pos};
        if (h->act == aScrollbar) {   // clicking the track jumps there first
            float th = std::max(36.0f, g_viewH * g_viewH / std::max(1.0f, g_contentH));
            float ty = (g_scroll / std::max(1.0f, g_contentH - g_viewH)) * (g_viewH - th);
            if (y < ty || y > ty + th) {
                g_scroll = g_scrollTarget = std::clamp((y - th / 2) / std::max(1.0f, g_viewH - th) * (g_contentH - g_viewH), 0.0f,
                                                       std::max(0.0f, g_contentH - g_viewH));
                g_drag.v0 = g_scroll;
            }
        }
        SetCapture(g_hwnd);
        DragTo(x, y);
        return;
    }
    g_pressAct = h->act;
    g_pressArg = h->arg;
}

static void MouseUp(float x, float y) {
    if (g_drag.act != aNone) { EndDrag(); return; }
    const Hit* h = HitAt(x, y);
    int act = g_pressAct, arg = g_pressArg;
    g_pressAct = aNone;
    if (h && h->act == act && h->arg == arg && act != aNone && act != aBlock) Activate(act, arg);
}

static void Hover(float x, float y) {
    const Hit* h = HitAt(x, y);
    int act = h ? h->act : aNone, arg = h ? h->arg : 0;
    if (act != g_hoverAct || arg != g_hoverArg) {
        g_hoverAct = act;
        g_hoverArg = arg;
        Redraw();
    }
}

static void Wheel(float x, float y, int delta, bool horizontal) {
    if (!g.coverSheet.empty() || g_setup.visible || g.nowPlaying) return;
    auto in = [&](const ClipRect& c) { return x >= c.x && x < c.x + c.w && y >= c.y && y < c.y + c.h; };
    if (in(g_volumeKnob)) { SetVolume(g.volume + delta / 120.0f * 0.05f); SaveSettings(); return; }
    bool shift = GetKeyState(VK_SHIFT) < 0;
    if (g.page == Page::Home && (horizontal || shift)) {
        for (int i = 0; i < 2; ++i)
            if (in(g_shelfBox[i])) { ScrollShelf(i, (horizontal ? delta : -delta) / 120.0f * 90); return; }
    }
    if (horizontal) return;
    if (in(g_sideList)) {
        g_sideScroll = std::clamp(g_sideScroll - delta / 120.0f * 60, 0.0f, std::max(0.0f, g_sideH - g_sideList.h));
        Redraw();
        return;
    }
    if (in(g_content)) {
        g_scrollTarget = std::clamp(g_scrollTarget - delta / 120.0f * 90, 0.0f, std::max(0.0f, g_contentH - g_viewH));
        StartAnimating();
    }
}

// ───────────────────────────── Window procedure ─────────────────────────────

static void SetScale(UINT dpi) {
    g_k = dpi / 96.0f;
    ClearFonts();
    g_shapes.clear();
    g_variants.clear();
    if (g_editFont) DeleteObject(g_editFont);
    g_editFont = CreateFontW(-IPX(16), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS,
                             CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    if (g_search) SendMessageW(g_search, WM_SETFONT, (WPARAM)g_editFont, TRUE);
}

static void LayoutChildren() {
    if (g_searchBox.w > 0 && g_visible) {
        int x = IPX(g_searchBox.x), y = IPX(g_searchBox.y), w = IPX(g_searchBox.w), h = IPX(g_searchBox.h);
        RECT cur;
        GetWindowRect(g_search, &cur);
        MapWindowPoints(nullptr, g_hwnd, (POINT*)&cur, 2);
        if (cur.left != x || cur.top != y || cur.right - cur.left != w || cur.bottom - cur.top != h)
            SetWindowPos(g_search, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
        if (!IsWindowVisible(g_search)) ShowWindow(g_search, SW_SHOWNA);
        if (g_focusSearch) {
            g_focusSearch = false;
            SetFocus(g_search);
            SendMessageW(g_search, EM_SETSEL, 0, -1);
        }
    } else if (IsWindowVisible(g_search)) {
        if (GetFocus() == g_search) SetFocus(g_hwnd);
        ShowWindow(g_search, SW_HIDE);
    }
    if (g_listNearEnd && g.page == Page::Search && !g.nowPlaying) LoadMoreResults();
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == g_taskbarCreated && m) { AddTray(); return 0; }   // Explorer restarted
    switch (m) {
    case WM_CREATE: {
        g_hwnd = h;
        SetScale(GetDpiForWindow(h));
        COLORREF caption = CR(pal::paper), text = CR(pal::ink);
        DwmSetWindowAttribute(h, 35 /* DWMWA_CAPTION_COLOR */, &caption, sizeof caption);
        DwmSetWindowAttribute(h, 36 /* DWMWA_TEXT_COLOR */, &text, sizeof text);
        g_search = CreateWindowExW(0, L"EDIT", W(g.query).c_str(), WS_CHILD | ES_AUTOHSCROLL, 0, 0, 0, 0, h,
                                   (HMENU)IDC_SEARCH, g_inst, nullptr);
        SendMessageW(g_search, WM_SETFONT, (WPARAM)g_editFont, TRUE);
        SendMessageW(g_search, EM_SETCUEBANNER, TRUE, (LPARAM)L"Songs, artists, albums\x2026");
        SendMessageW(g_search, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);
        SetTimer(h, TIMER_TICK, 500, nullptr);
        RegisterHotKey(h, HOTKEY_PLAY, MOD_NOREPEAT, VK_MEDIA_PLAY_PAUSE);
        RegisterHotKey(h, HOTKEY_NEXT, MOD_NOREPEAT, VK_MEDIA_NEXT_TRACK);
        RegisterHotKey(h, HOTKEY_PREV, MOD_NOREPEAT, VK_MEDIA_PREV_TRACK);
        return 0;
    }
    case WM_SIZE: {
        g_W = LOWORD(l) / g_k;
        g_H = HIWORD(l) / g_k;
        if (w == SIZE_MINIMIZED) { g_spinJobs.clear(); UpdateSpinTimer(); }
        Redraw();
        return 0;
    }
    case WM_GETMINMAXINFO: {
        auto* mm = (MINMAXINFO*)l;
        mm->ptMinTrackSize = {IPX(960), IPX(640)};
        return 0;
    }
    case WM_DPICHANGED: {
        SetScale(HIWORD(w));
        auto* r = (RECT*)l;
        SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        Redraw();
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc;
        GetClientRect(h, &rc);
        EnsureBuffer(dc, rc.right, rc.bottom);
        BeginRender(ps.rcPaint);
        PaintAll();
        EndRender();
        BitBlt(dc, ps.rcPaint.left, ps.rcPaint.top, ps.rcPaint.right - ps.rcPaint.left, ps.rcPaint.bottom - ps.rcPaint.top,
               g_mem, ps.rcPaint.left, ps.rcPaint.top, SRCCOPY);
        EndPaint(h, &ps);
        float maxScroll = std::max(0.0f, g_contentH - g_viewH);
        if (g_scroll > maxScroll || g_scrollTarget > maxScroll) {
            g_scroll = std::min(g_scroll, maxScroll);
            g_scrollTarget = std::min(g_scrollTarget, maxScroll);
            Redraw();
        }
        UpdateSpinTimer();
        PostMessageW(h, WM_APP_LAYOUT, 0, 0);
        return 0;
    }
    case WM_APP_LAYOUT: LayoutChildren(); return 0;
    case WM_CTLCOLOREDIT:
        SetTextColor((HDC)w, CR(pal::ink));
        SetBkColor((HDC)w, CR(pal::card));
        return (LRESULT)g_brCard;
    case WM_SETCURSOR:
        if (LOWORD(l) == HTCLIENT && (HWND)w == h) {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(h, &pt);
            const Hit* at = HitAt(pt.x / g_k, pt.y / g_k);
            SetCursor(LoadCursor(nullptr, at && at->hand ? IDC_HAND : IDC_ARROW));
            return TRUE;
        }
        break;
    case WM_MOUSEMOVE: {
        float x = GET_X_LPARAM(l) / g_k, y = GET_Y_LPARAM(l) / g_k;
        if (g_drag.act != aNone) { DragTo(x, y); return 0; }
        Hover(x, y);
        TRACKMOUSEEVENT tme{sizeof tme, TME_LEAVE, h, 0};
        TrackMouseEvent(&tme);
        return 0;
    }
    case WM_MOUSELEAVE:
        if (g_hoverAct != aNone) { g_hoverAct = aNone; Redraw(); }
        return 0;
    case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: {
        float x = GET_X_LPARAM(l) / g_k, y = GET_Y_LPARAM(l) / g_k;
        if (m == WM_LBUTTONDBLCLK) {
            const Hit* hit = HitAt(x, y);
            if (hit && hit->act == aRow) { PlayRow(hit->arg); return 0; }
            if (hit && hit->act == aUpNextRow) { JumpUpNext(hit->arg); return 0; }
        }
        MouseDown(x, y);
        return 0;
    }
    case WM_LBUTTONUP: MouseUp(GET_X_LPARAM(l) / g_k, GET_Y_LPARAM(l) / g_k); return 0;
    case WM_CAPTURECHANGED: if (g_drag.act != aNone && (HWND)l != h) { g_drag.act = aNone; Redraw(); } return 0;
    case WM_RBUTTONUP: {
        POINT pt{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
        POINT screen = pt;
        ClientToScreen(h, &screen);
        ContextMenuAt(pt.x / g_k, pt.y / g_k, screen);
        return 0;
    }
    case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL: {
        POINT pt{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
        ScreenToClient(h, &pt);
        Wheel(pt.x / g_k, pt.y / g_k, GET_WHEEL_DELTA_WPARAM(w), m == WM_MOUSEHWHEEL);
        return 0;
    }
    case WM_TIMER:
        switch (w) {
        case TIMER_TICK:
            ReadClock();
            if (g_visible && (int)g.pos != g_lastSecond) {
                g_lastSecond = (int)g.pos;
                for (auto& b : g_tickBoxes) InvalidateDip(b);
            }
            break;
        case TIMER_SEARCH:
            KillTimer(h, TIMER_SEARCH);
            SaveSettings();
            StartSearch();
            break;
        case TIMER_SPIN: SpinTick(); break;
        case TIMER_ANIM: {
            bool moving = StepTweens();
            float maxScroll = std::max(0.0f, g_contentH - g_viewH);
            g_scrollTarget = std::clamp(g_scrollTarget, 0.0f, maxScroll);
            if (std::fabs(g_scrollTarget - g_scroll) > 0.5f) { g_scroll += (g_scrollTarget - g_scroll) * 0.3f; moving = true; }
            else g_scroll = g_scrollTarget;
            Redraw();
            if (!moving) { KillTimer(h, TIMER_ANIM); g_animating = false; }
            break;
        }
        case TIMER_ART: KillTimer(h, TIMER_ART); Redraw(); break;
        }
        return 0;
    case WM_HOTKEY:
        if (w == HOTKEY_PLAY) TogglePlay();
        if (w == HOTKEY_NEXT) Next();
        if (w == HOTKEY_PREV) { ReadClock(); Previous(); }
        return 0;
    case WM_APPCOMMAND:
        switch (GET_APPCOMMAND_LPARAM(l)) {
        case APPCOMMAND_MEDIA_PLAY_PAUSE: TogglePlay(); return TRUE;
        case APPCOMMAND_MEDIA_NEXTTRACK: Next(); return TRUE;
        case APPCOMMAND_MEDIA_PREVIOUSTRACK: ReadClock(); Previous(); return TRUE;
        }
        break;
    case WM_COMMAND:
        if (LOWORD(w) == IDC_SEARCH && HIWORD(w) == EN_CHANGE && !g_ignoreEdit) {
            int n = GetWindowTextLengthW(g_search);
            wstring text(n, 0);
            GetWindowTextW(g_search, text.data(), n + 1);
            g.query = U8(text);
            SetTimer(h, TIMER_SEARCH, 350, nullptr);
            Redraw();
        }
        return 0;
    case WM_APP_TRAY:
        switch (LOWORD(l)) {
        case WM_LBUTTONUP: case WM_LBUTTONDBLCLK: case NIN_SELECT: ShowMain(); break;
        case WM_RBUTTONUP: case WM_CONTEXTMENU: TrayMenu(); break;
        }
        return 0;
    case WM_APP_SHOW: ShowMain(); return 0;
    case WM_APP_QUIT: Quit(); return 0;
    case WM_APP_SEARCH: {
        auto* r = (SearchResult*)l;
        OnSearch(r);
        delete r;
        return 0;
    }
    case WM_APP_READY: {
        auto* r = (PrepResult*)l;
        OnAudioReady(r);
        delete r;
        return 0;
    }
    case WM_APP_ENDED: OnTrackEnded(); return 0;
    case WM_APP_MFERROR: {
        g.loading = false;
        g.playing = false;
        char buf[96];
        snprintf(buf, sizeof buf, "Playback error (0x%08lX).", (unsigned long)w);
        g.error = buf;
        Redraw();
        return 0;
    }
    case WM_APP_ART: {
        auto* r = (ArtResult*)l;
        if (g_visible) {
            ArtLoaded(r->url, r->bmp);
            SetTimer(h, TIMER_ART, 40, nullptr);   // one redraw for a burst of covers
        } else {
            delete r->bmp;   // the window is closed: don't keep it
            g_art.erase(r->url);
        }
        delete r;
        return 0;
    }
    case WM_APP_RADIO: {
        auto* r = (RadioResult*)l;
        OnRadio(r);
        delete r;
        return 0;
    }
    case WM_APP_SHELVES: {
        auto* s = (vector<Shelf>*)l;
        OnShelves(s);
        delete s;
        return 0;
    }
    case WM_APP_SETUP: {
        auto* s = (SetupMsg*)l;
        OnSetupMsg(s);
        delete s;
        return 0;
    }
    case WM_CLOSE:
        if (g_quitting) break;
        HideToTray();
        return 0;
    case WM_QUERYENDSESSION: return TRUE;
    case WM_ENDSESSION:
        if (w) { SaveLibrary(); SaveSettings(); }
        return 0;
    case WM_DESTROY:
        SaveLibrary();
        SaveSettings();
        UnloadItem();
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

// ───────────────────────────── Keyboard ─────────────────────────────

// Returns true if the key was handled.
static bool HandleKey(const MSG& msg) {
    if (msg.message != WM_KEYDOWN && msg.message != WM_SYSKEYDOWN) return false;
    if (GetForegroundWindow() != g_hwnd) return false;
    WPARAM k = msg.wParam;
    bool ctrl = GetKeyState(VK_CONTROL) < 0, inSearch = GetFocus() == g_search;
    if (k == VK_ESCAPE) {
        if (!g.coverSheet.empty()) g.coverSheet.clear();
        else if (g_setup.visible && g_setup.phase != 1) g_setup.visible = false;
        else if (g.nowPlaying) g.nowPlaying = false;
        else return false;
        Redraw();
        PostMessageW(g_hwnd, WM_APP_LAYOUT, 0, 0);
        return true;
    }
    if (k == VK_SPACE && !inSearch && !ctrl) { TogglePlay(); return true; }
    if (ctrl && k == VK_RETURN) { TogglePlay(); return true; }
    if (ctrl && k == VK_RIGHT && !inSearch) { Next(); return true; }
    if (ctrl && k == VK_LEFT && !inSearch) { ReadClock(); Previous(); return true; }
    if (ctrl && (k == VK_UP || k == VK_DOWN)) { SetVolume(g.volume + (k == VK_UP ? 0.1f : -0.1f)); SaveSettings(); return true; }
    if (ctrl && k == 'F') { Navigate(Page::Search); return true; }
    if (ctrl && k == 'L') { if (const Track* t = g.current()) ToggleLike(*t); return true; }
    if (ctrl && k == 'S') { ToggleShuffle(); return true; }
    if (ctrl && k == 'R') { CycleRepeat(); return true; }
    if (inSearch && k == VK_RETURN) { KillTimer(g_hwnd, TIMER_SEARCH); SaveSettings(); StartSearch(); return true; }
    if (!inSearch && !g.nowPlaying && (k == VK_NEXT || k == VK_PRIOR || k == VK_HOME || k == VK_END)) {
        float page = g_viewH * 0.85f, maxScroll = std::max(0.0f, g_contentH - g_viewH);
        g_scrollTarget = k == VK_HOME ? 0 : k == VK_END ? maxScroll : g_scrollTarget + (k == VK_NEXT ? page : -page);
        g_scrollTarget = std::clamp(g_scrollTarget, 0.0f, maxScroll);
        StartAnimating();
        return true;
    }
    return false;
}

// ───────────────────────────── Startup ─────────────────────────────

static void PickIconFont() {
    HDC dc = GetDC(nullptr);
    LOGFONTW lf{};
    lf.lfCharSet = DEFAULT_CHARSET;
    wcscpy_s(lf.lfFaceName, L"Segoe Fluent Icons");
    bool found = false;
    EnumFontFamiliesExW(dc, &lf, [](const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM p) -> int {
        *(bool*)p = true;
        return 0;
    }, (LPARAM)&found, 0);
    ReleaseDC(nullptr, dc);
    if (!found) g_iconFace = L"Segoe MDL2 Assets";   // Windows 10
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR cmdLine, int show) {
    g_inst = inst;
    wstring self = SelfPath();
    g_appDir = self.substr(0, self.rfind(L'\\'));
    if (cmdLine && wcsstr(cmdLine, L"--uninstall")) {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        return Uninstall();
    }

    HANDLE single = CreateMutexW(nullptr, TRUE, L"Local\\MosicsSingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND other = FindWindowW(L"MosicsMain", nullptr);
        // A newer Mosics.exe was downloaded while the installed one plays in the tray:
        // close the old one so setup can install this version over it.
        bool upgrade = other && !IsInstalledCopy() && FileExists(InstalledExe()) && FileVersion(InstalledExe()) != MOSICS_VERSION;
        if (!upgrade) {
            if (other) {
                AllowSetForegroundWindow(ASFW_ANY);
                PostMessageW(other, WM_APP_SHOW, 0, 0);
            }
            return 0;
        }
        DWORD pid = 0;
        GetWindowThreadProcessId(other, &pid);
        HANDLE old = OpenProcess(SYNCHRONIZE, FALSE, pid);
        DWORD_PTR r;
        SendMessageTimeoutW(other, WM_APP_QUIT, 0, 0, SMTO_ABORTIFHUNG, 5000, &r);
        if (old) { WaitForSingleObject(old, 8000); CloseHandle(old); }
        CloseHandle(single);
        single = CreateMutexW(nullptr, TRUE, L"Local\\MosicsSingleInstance");
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    Gdiplus::GdiplusStartupInput gsi;
    Gdiplus::GdiplusStartup(&g_gdiplusToken, &gsi, nullptr);
    INITCOMMONCONTROLSEX icc{sizeof icc, ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    g_dataDir = KnownFolder(FOLDERID_RoamingAppData) + L"\\Mosics";
    g_localDir = KnownFolder(FOLDERID_LocalAppData) + L"\\Mosics";
    g_cacheDir = g_localDir + L"\\cache";
    g_binDir = g_localDir + L"\\bin";
    g_artDir = g_localDir + L"\\art";
    g_coversDir = g_dataDir + L"\\covers";
    for (const wstring& d : {g_dataDir, g_cacheDir, g_binDir, g_artDir, g_coversDir}) SHCreateDirectoryExW(nullptr, d.c_str(), nullptr);
    {   // yt-dlp looks for deno on PATH: put our helper folders first.
        wchar_t path[32767];
        DWORD n = GetEnvironmentVariableW(L"PATH", path, 32767);
        SetEnvironmentVariableW(L"PATH", (g_binDir + L";" + g_appDir + L";" + wstring(path, n)).c_str());
    }

    LoadState();
    LoadShelves();
    PickIconFont();
    HttpInit();
    g_artSlots = CreateSemaphoreW(nullptr, 6, 6, nullptr);
    g_artReady = [](string url, Bitmap* bmp) {
        if (g_hwnd) PostMessageW(g_hwnd, WM_APP_ART, 0, (LPARAM) new ArtResult{std::move(url), bmp});
        else delete bmp;
    };

    InitMediaFoundation();

    g_brCard = CreateSolidBrush(CR(pal::card));
    g_brPaper = CreateSolidBrush(CR(pal::paper));
    g_iconSmall = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                    GetSystemMetrics(SM_CYSMICON), 0);
    g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

    WNDCLASSEXW wc{sizeof wc};
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = g_brPaper;
    wc.lpszClassName = L"MosicsMain";
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.hIconSm = g_iconSmall;
    RegisterClassExW(&wc);

    UINT dpi = GetDpiForSystem();
    HWND hwnd = CreateWindowExW(0, L"MosicsMain", L"Mosics", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT,
                                CW_USEDEFAULT, MulDiv(1180, dpi, 96), MulDiv(780, dpi, 96), nullptr, nullptr, inst, nullptr);
    AddTray();
    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);
    SetVolume(g.volume);

    if (SetupNeeded()) ShowSetup();
    RefreshShelves(false);
    // Housekeeping in the background: trim the caches, keep our own yt-dlp fresh.
    RunAsync([] {
        TrimFolder(g_cacheDir, 400ull << 20);
        TrimFolder(g_artDir, 100ull << 20);
    });
    wstring ytdlp = FindTool(L"yt-dlp.exe");
    bool ours = !ytdlp.empty() && (ytdlp.rfind(g_binDir, 0) == 0 || ytdlp.rfind(g_appDir, 0) == 0);
    if (ours && NowSeconds() - g.ytdlpCheckedAt > 86400) {
        g.ytdlpCheckedAt = NowSeconds();
        SaveSettings();
        RunAsync([ytdlp] { string out; RunHidden(ytdlp, L"-U", out, 120000); });
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (HandleKey(msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_mp) { g_mp->Shutdown(); g_mp->Release(); }
    PurgeArt();
    FreeBuffer();
    Gdiplus::GdiplusShutdown(g_gdiplusToken);
    if (pMFShutdown) pMFShutdown();
    CoUninitialize();
    if (single) CloseHandle(single);
    return 0;
}
