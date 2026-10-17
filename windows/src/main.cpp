// Mosics for Windows — a tiny audio-only music player.
// Pure Win32 + Media Foundation (MFPlay) + WinHTTP + GDI+. Search via YouTube Music, audio via yt-dlp.
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
#include <dwmapi.h>
#include <mfapi.h>
#include <mfplay.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <uxtheme.h>
#include <winhttp.h>

#include <algorithm>
#include <functional>
#include <random>
#include <string>
#include <vector>
namespace Gdiplus { using std::max; using std::min; }
#include <gdiplus.h>

#include "audiofile.h"
#include "json.h"
#include "ytmusic.h"

#ifndef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
#define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY 4
#endif
#ifndef WINHTTP_OPTION_DECOMPRESSION
#define WINHTTP_OPTION_DECOMPRESSION 118
#define WINHTTP_DECOMPRESSION_FLAG_ALL 3
#endif
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

using std::string;
using std::vector;
using std::wstring;

// ───────────────────────────── Constants ─────────────────────────────

enum : UINT {
    WM_APP_SEARCH = WM_APP + 1,   // lParam: SearchResult*
    WM_APP_READY,                 // lParam: PrepResult*
    WM_APP_ENDED,
    WM_APP_ART,                   // lParam: ArtResult*
    WM_APP_RADIO,                 // lParam: RadioResult*
    WM_APP_MFERROR,               // wParam: HRESULT
};

enum : int {
    IDC_SIDEBAR = 100, IDC_SEARCH, IDC_LIST, IDC_SEEK, IDC_VOLUME,
    IDC_SHUFFLE, IDC_PREV, IDC_PLAY, IDC_NEXT, IDC_REPEAT, IDC_LIKE, IDC_QUEUEBTN,
    IDC_HEAD1, IDC_HEAD2, IDC_NEWPL,
    ID_PLAY = 1000, ID_PLAYNEXT, ID_ADDQUEUE, ID_RADIO, ID_LIKE, ID_REMOVE, ID_COPYLINK, ID_NEWPL_WITH,
    ID_PL_RENAME, ID_PL_DELETE,
    ID_ADDPL_BASE = 2000,
    TIMER_TICK = 1, TIMER_SEARCH = 2,
    HOTKEY_PLAY = 1, HOTKEY_NEXT, HOTKEY_PREV,
};

namespace color {
const COLORREF bg = RGB(18, 18, 18), side = RGB(0, 0, 0), bar = RGB(24, 24, 24), field = RGB(42, 42, 42),
               hover = RGB(40, 40, 40), text = RGB(255, 255, 255), dim = RGB(179, 179, 179),
               faint = RGB(110, 110, 110), accent = RGB(30, 215, 96), error = RGB(241, 94, 108);
}

// Segoe Fluent Icons / Segoe MDL2 Assets glyphs
namespace glyph {
const wchar_t play[] = L"", pause[] = L"", prev[] = L"", next[] = L"",
              shuffle[] = L"", repeat[] = L"", repeatOne[] = L"", heart[] = L"",
              heartFill[] = L"", volume[] = L"", mute[] = L"", list[] = L"", add[] = L"";
}

// ───────────────────────────── State ─────────────────────────────

struct Playlist {
    string name;
    vector<Track> tracks;
};

enum class Page { Search, Recent, Liked, Queue, Playlist };
enum class Repeat { Off, All, One };

struct App {
    // persisted
    vector<Track> liked, recent;
    vector<Playlist> playlists;
    float volume = 0.8f;
    bool shuffle = false, autoplay = true;
    Repeat repeat = Repeat::Off;
    double ytdlpCheckedAt = 0;

    // session
    vector<Track> queue;
    int qi = -1;                 // index into queue of the current track
    vector<Track> results;       // last search results
    vector<Track> view;          // rows shown in the list
    Page page = Page::Search;
    int pagePlaylist = -1;
    bool playing = false, loading = false, dragging = false, waitingForRadio = false;
    double pos = 0, dur = 0;
    wstring status;
    bool statusIsError = false;
    int searchSeq = 0, loadSeq = 0;
    Gdiplus::Bitmap* art = nullptr;
    string artId;

    const Track* current() const { return qi >= 0 && qi < (int)queue.size() ? &queue[qi] : nullptr; }
} g;

HINSTANCE g_inst;
HWND g_hwnd, g_sidebar, g_search, g_list, g_seek, g_vol;
HWND g_btn[16];   // indexed by IDC_* - IDC_SHUFFLE
HFONT g_font, g_fontBold, g_fontTitle, g_fontSmall, g_fontIcon, g_fontIconBig;
HBRUSH g_brBg, g_brSide, g_brBar, g_brField;
UINT g_dpi = 96;
wstring g_iconFace = L"Segoe Fluent Icons";
wstring g_appDir, g_dataDir, g_cacheDir;
IMFPMediaPlayer* g_mp;
IMFPMediaItem* g_item;   // currently loaded item
HINTERNET g_http;
ULONG_PTR g_gdiplusToken;
int g_hoverBtn = -1;

static int S(int v) { return MulDiv(v, (int)g_dpi, 96); }

// ───────────────────────────── Small utilities ─────────────────────────────

static wstring W(const string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

static string U8(const wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

static wstring FormatTime(double t) {
    if (!(t >= 0)) t = 0;
    int s = (int)t;
    wchar_t buf[32];
    if (s >= 3600) swprintf(buf, 32, L"%d:%02d:%02d", s / 3600, s / 60 % 60, s % 60);
    else swprintf(buf, 32, L"%d:%02d", s / 60, s % 60);
    return buf;
}

static double ParseDuration(const string& d) {
    double total = 0, part = 0;
    for (char c : d) {
        if (c == ':') { total = total * 60 + part; part = 0; }
        else if (c >= '0' && c <= '9') part = part * 10 + (c - '0');
    }
    return total * 60 + part;
}

static bool FileExists(const wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static bool ReadFileAll(const wstring& path, string& out) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size;
    GetFileSizeEx(f, &size);
    out.resize((size_t)size.QuadPart);
    DWORD got = 0;
    bool ok = out.empty() || (ReadFile(f, out.data(), (DWORD)out.size(), &got, nullptr) && got == out.size());
    CloseHandle(f);
    return ok;
}

// Writes atomically: temp file + rename, so a crash never leaves a half-written file.
static bool WriteFileAll(const wstring& path, const string& data) {
    wstring tmp = path + L".tmp";
    HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD put = 0;
    bool ok = WriteFile(f, data.data(), (DWORD)data.size(), &put, nullptr) && put == data.size();
    CloseHandle(f);
    return ok && MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
}

static void RunAsync(std::function<void()> fn) {
    auto* heap = new std::function<void()>(std::move(fn));
    HANDLE t = CreateThread(nullptr, 0, [](LPVOID p) -> DWORD {
        auto* f = static_cast<std::function<void()>*>(p);
        (*f)();
        delete f;
        return 0;
    }, heap, 0, nullptr);
    if (t) CloseHandle(t); else delete heap;
}

static double NowSeconds() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    return (double)(((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime) / 1e7;
}

// ───────────────────────────── HTTP (WinHTTP) ─────────────────────────────

static bool HttpRequest(const wstring& url, const wchar_t* method, const string& body, const wchar_t* headers,
                        string& out) {
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof uc;
    wchar_t host[256], path[4096], extra[4096];
    uc.lpszHostName = host; uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path; uc.dwUrlPathLength = 4096;
    uc.lpszExtraInfo = extra; uc.dwExtraInfoLength = 4096;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) return false;
    wstring object = wstring(path, uc.dwUrlPathLength) + wstring(extra, uc.dwExtraInfoLength);

    bool ok = false;
    HINTERNET c = WinHttpConnect(g_http, wstring(host, uc.dwHostNameLength).c_str(), uc.nPort, 0);
    HINTERNET r = c ? WinHttpOpenRequest(c, method, object.c_str(), nullptr, WINHTTP_NO_REFERER,
                                         WINHTTP_DEFAULT_ACCEPT_TYPES,
                                         uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)
                    : nullptr;
    if (r) {
        DWORD decompress = WINHTTP_DECOMPRESSION_FLAG_ALL;
        WinHttpSetOption(r, WINHTTP_OPTION_DECOMPRESSION, &decompress, sizeof decompress);
        if (WinHttpSendRequest(r, headers ? headers : WINHTTP_NO_ADDITIONAL_HEADERS, headers ? (DWORD)-1L : 0,
                               (LPVOID)body.data(), (DWORD)body.size(), (DWORD)body.size(), 0) &&
            WinHttpReceiveResponse(r, nullptr)) {
            DWORD status = 0, len = sizeof status;
            WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                                &status, &len, WINHTTP_NO_HEADER_INDEX);
            out.clear();
            char buf[16384];
            DWORD got = 0;
            while (WinHttpReadData(r, buf, sizeof buf, &got) && got) out.append(buf, got);
            ok = status >= 200 && status < 300;
        }
        WinHttpCloseHandle(r);
    }
    if (c) WinHttpCloseHandle(c);
    return ok;
}

static bool YTMusicPost(const char* endpoint, const string& body, string& out) {
    wstring url = L"https://music.youtube.com/youtubei/v1/" + W(endpoint) + L"?prettyPrint=false";
    return HttpRequest(url, L"POST", body,
                       L"Content-Type: application/json\r\nOrigin: https://music.youtube.com\r\n", out);
}

// ───────────────────────────── yt-dlp + audio cache ─────────────────────────────

static wstring FindYtDlp() {
    wstring local = g_appDir + L"\\yt-dlp.exe";
    if (FileExists(local)) return local;
    wchar_t buf[MAX_PATH];
    if (SearchPathW(nullptr, L"yt-dlp.exe", nullptr, MAX_PATH, buf, nullptr)) return buf;
    wchar_t* la = nullptr;
    size_t n = 0;
    if (_wdupenv_s(&la, &n, L"LOCALAPPDATA") == 0 && la) {
        wstring winget = wstring(la) + L"\\Microsoft\\WinGet\\Links\\yt-dlp.exe";
        free(la);
        if (FileExists(winget)) return winget;
    }
    return {};
}

// Runs a console program hidden, captures stdout+stderr. Returns exit code (-1 on failure/timeout).
static int RunHidden(const wstring& exe, const wstring& args, string& output, DWORD timeoutMs) {
    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    HANDLE rd, wr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return -1;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si{};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = si.hStdError = wr;
    si.hStdInput = nullptr;
    PROCESS_INFORMATION pi{};
    wstring cmd = L"\"" + exe + L"\" " + args;
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(rd);
        CloseHandle(wr);
        return -1;
    }
    CloseHandle(wr);
    ULONGLONG deadline = GetTickCount64() + timeoutMs;
    char buf[4096];
    for (;;) {
        DWORD avail = 0;
        if (!PeekNamedPipe(rd, nullptr, 0, nullptr, &avail, nullptr)) break;   // pipe closed: process done
        if (avail) {
            DWORD got = 0;
            if (!ReadFile(rd, buf, std::min<DWORD>(avail, sizeof buf), &got, nullptr) || !got) break;
            output.append(buf, got);
        } else if (GetTickCount64() > deadline) {
            TerminateProcess(pi.hProcess, 1);
            break;
        } else {
            Sleep(30);
        }
    }
    CloseHandle(rd);
    WaitForSingleObject(pi.hProcess, 5000);
    DWORD code = (DWORD)-1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int)code;
}

static wstring CachedAudio(const string& id) {
    for (const wchar_t* ext : {L".aac", L".m4a", L".ts", L".webm"}) {
        wstring p = g_cacheDir + L"\\" + W(id) + ext;
        if (FileExists(p)) return p;
    }
    return {};
}

static void Touch(const wstring& path) {
    HANDLE f = CreateFileW(path.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    SetFileTime(f, nullptr, nullptr, &now);
    CloseHandle(f);
}

// Downloads (once) the audio for `id` into the cache. Blocking — call from a worker thread.
static bool PrepareAudio(const string& id, wstring& path, string& err) {
    // One download per song even if "play" and "prefetch" race for it.
    HANDLE lock = CreateMutexW(nullptr, FALSE, (L"Local\\Mosics-" + W(id)).c_str());
    if (lock) WaitForSingleObject(lock, INFINITE);
    struct Unlock { HANDLE h; ~Unlock() { if (h) { ReleaseMutex(h); CloseHandle(h); } } } unlock{lock};

    path = CachedAudio(id);
    if (!path.empty()) { Touch(path); return true; }

    wstring exe = FindYtDlp();
    if (exe.empty()) {
        err = "yt-dlp not found. Put yt-dlp.exe and deno.exe next to Mosics.exe (see README).";
        return false;
    }
    wstring tmp = g_cacheDir + L"\\" + W(id) + L".download";
    DeleteFileW(tmp.c_str());
    wstring outTemplate = tmp;   // yt-dlp treats % as a template character
    for (size_t p = 0; (p = outTemplate.find(L'%', p)) != wstring::npos; p += 2) outTemplate.insert(p, L"%");
    wstring args = L"-f \"234/233/140/bestaudio[ext=m4a]/bestaudio\" -N 4 --no-part --no-playlist --no-warnings "
                   L"--no-progress --no-mtime --fixup never -o \"" + outTemplate +
                   L"\" -- " + W(id);
    string output;
    int code = RunHidden(exe, args, output, 180000);
    string data;
    if (code != 0 || !ReadFileAll(tmp, data) || data.empty()) {
        DeleteFileW(tmp.c_str());
        size_t e = output.rfind("ERROR:");
        err = e == string::npos ? "Couldn't download this song." : output.substr(e + 7);
        while (!err.empty() && (err.back() == '\n' || err.back() == '\r')) err.pop_back();
        return false;
    }
    DeleteFileW(tmp.c_str());
    string ext = audiofile::normalize(data);
    if (ext.empty()) { err = "Unrecognised audio format."; return false; }
    path = g_cacheDir + L"\\" + W(id) + W(ext);
    if (!WriteFileAll(path, data)) { err = "Couldn't write to the cache folder."; return false; }
    return true;
}

// Keep the cache under ~400 MB by deleting the least recently played files.
static void TrimCache() {
    struct F { wstring path; ULONGLONG time, size; };
    vector<F> files;
    ULONGLONG total = 0;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((g_cacheDir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        ULONGLONG size = ((ULONGLONG)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
        ULONGLONG t = ((ULONGLONG)fd.ftLastWriteTime.dwHighDateTime << 32) | fd.ftLastWriteTime.dwLowDateTime;
        files.push_back({g_cacheDir + L"\\" + fd.cFileName, t, size});
        total += size;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(files.begin(), files.end(), [](const F& a, const F& b) { return a.time < b.time; });
    const ULONGLONG limit = 400ull << 20;
    for (auto& f : files) {
        if (total <= limit) break;
        if (DeleteFileW(f.path.c_str())) total -= f.size;
    }
}

// ───────────────────────────── Library persistence ─────────────────────────────

static Json TracksToJson(const vector<Track>& ts) {
    Json a = Json::array();
    for (auto& t : ts) a.push(t.toJson());
    return a;
}

static vector<Track> TracksFromJson(const Json& j) {
    vector<Track> v;
    for (auto& t : j.items()) v.push_back(Track::fromJson(t));
    return v;
}

static void SaveLibrary() {
    Json pls = Json::array();
    for (auto& p : g.playlists) pls.push(Json::object().set("name", p.name).set("tracks", TracksToJson(p.tracks)));
    Json j = Json::object()
                 .set("liked", TracksToJson(g.liked))
                 .set("recent", TracksToJson(g.recent))
                 .set("playlists", pls)
                 .set("volume", (double)g.volume)
                 .set("shuffle", g.shuffle)
                 .set("autoplay", g.autoplay)
                 .set("repeat", (int)g.repeat)
                 .set("ytdlpCheckedAt", g.ytdlpCheckedAt);
    WriteFileAll(g_dataDir + L"\\library.json", j.dump());
}

static void LoadLibraryState() {
    string text;
    if (!ReadFileAll(g_dataDir + L"\\library.json", text)) return;
    Json j = Json::parse(text);
    g.liked = TracksFromJson(j["liked"]);
    g.recent = TracksFromJson(j["recent"]);
    for (auto& p : j["playlists"].items()) g.playlists.push_back({p["name"].str(), TracksFromJson(p["tracks"])});
    g.volume = (float)j["volume"].num(0.8);
    g.shuffle = j["shuffle"].boolean(false);
    g.autoplay = j["autoplay"].boolean(true);
    g.repeat = (Repeat)std::clamp((int)j["repeat"].num(0), 0, 2);
    g.ytdlpCheckedAt = j["ytdlpCheckedAt"].num(0);
}

static bool IsLiked(const string& id) {
    return std::any_of(g.liked.begin(), g.liked.end(), [&](const Track& t) { return t.id == id; });
}

static void ToggleLike(const Track& t) {
    auto it = std::find_if(g.liked.begin(), g.liked.end(), [&](const Track& x) { return x.id == t.id; });
    if (it != g.liked.end()) g.liked.erase(it); else g.liked.insert(g.liked.begin(), t);
    SaveLibrary();
}

static void AddRecent(const Track& t) {
    g.recent.erase(std::remove_if(g.recent.begin(), g.recent.end(), [&](const Track& x) { return x.id == t.id; }),
                   g.recent.end());
    g.recent.insert(g.recent.begin(), t);
    if (g.recent.size() > 100) g.recent.resize(100);
    SaveLibrary();
}

// ───────────────────────────── Forward declarations ─────────────────────────────

static void RefreshView();
static void RefreshSidebar();
static void UpdateNowPlaying();
static void InvalidateBar();
static void Layout();

// ───────────────────────────── Player (MFPlay) ─────────────────────────────

class PlayerCallback : public IMFPMediaPlayerCallback {
public:
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (riid == IID_IUnknown || riid == __uuidof(IMFPMediaPlayerCallback)) {
            *ppv = static_cast<IMFPMediaPlayerCallback*>(this);
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return 2; }
    STDMETHODIMP_(ULONG) Release() override { return 1; }

    // MFPlay delivers events on the thread that created the player (our UI thread).
    void STDMETHODCALLTYPE OnMediaPlayerEvent(MFP_EVENT_HEADER* e) override {
        switch (e->eEventType) {
        case MFP_EVENT_TYPE_MEDIAITEM_SET:
            if (FAILED(e->hrEvent)) { PostMessageW(g_hwnd, WM_APP_MFERROR, (WPARAM)e->hrEvent, 0); break; }
            g_mp->SetVolume(g.volume);
            if (g.playing) g_mp->Play();
            g.loading = false;
            InvalidateBar();
            break;
        case MFP_EVENT_TYPE_PLAYBACK_ENDED:
            if (MFP_GET_PLAYBACK_ENDED_EVENT(e)->pMediaItem == g_item) PostMessageW(g_hwnd, WM_APP_ENDED, 0, 0);
            break;
        case MFP_EVENT_TYPE_ERROR:
            PostMessageW(g_hwnd, WM_APP_MFERROR, (WPARAM)e->hrEvent, 0);
            break;
        default:
            break;
        }
    }
} g_callback;

struct SearchResult { int seq; vector<Track> tracks; bool ok; };
struct PrepResult { int seq; string id; wstring path; string err; };
struct ArtResult { string id; string bytes; };
struct RadioResult { string seed; vector<Track> tracks; };

static void SetStatus(const wstring& s, bool isError) {
    g.status = s;
    g.statusIsError = isError;
    InvalidateBar();
}

static void FetchRadio(const string& seed) {
    RunAsync([seed] {
        string body;
        if (!YTMusicPost("next", ytmusic::radioBody(seed), body)) return;
        PostMessageW(g_hwnd, WM_APP_RADIO, 0, (LPARAM) new RadioResult{seed, ytmusic::parseRadio(body)});
    });
}

static void PrefetchNext() {
    if (g.qi < 0 || g.qi + 1 >= (int)g.queue.size()) return;
    string id = g.queue[g.qi + 1].id;
    if (!CachedAudio(id).empty()) return;
    RunAsync([id] { wstring p; string e; PrepareAudio(id, p, e); });
}

static void UnloadItem() {
    if (g_mp) { g_mp->Stop(); g_mp->ClearMediaItem(); }
    if (g_item) { g_item->Release(); g_item = nullptr; }
}

static void LoadIndex(int i) {
    if (i < 0 || i >= (int)g.queue.size()) return;
    g.qi = i;
    g.waitingForRadio = false;
    const Track t = g.queue[i];
    int seq = ++g.loadSeq;
    UnloadItem();
    g.pos = 0;
    g.dur = ParseDuration(t.duration);
    g.playing = true;
    g.loading = true;
    SetStatus(L"Loading…", false);
    AddRecent(t);
    UpdateNowPlaying();

    // Artwork
    if (g.artId != t.id) {
        delete g.art;
        g.art = nullptr;
        g.artId = t.id;
        string id = t.id, url = t.thumbAt(226);
        RunAsync([id, url] {
            string bytes;
            if (!url.empty() && HttpRequest(W(url), L"GET", "", nullptr, bytes))
                PostMessageW(g_hwnd, WM_APP_ART, 0, (LPARAM) new ArtResult{id, std::move(bytes)});
        });
    }
    // Audio
    RunAsync([seq, id = t.id] {
        auto* r = new PrepResult{seq, id, {}, {}};
        PrepareAudio(id, r->path, r->err);
        PostMessageW(g_hwnd, WM_APP_READY, 0, (LPARAM)r);
    });
    if (g.autoplay && g.repeat == Repeat::Off && i + 1 >= (int)g.queue.size()) FetchRadio(t.id);
}

static void OnAudioReady(PrepResult* r) {
    if (r->seq != g.loadSeq) return;   // user already moved on
    if (!r->err.empty()) {
        g.loading = false;
        g.playing = false;
        SetStatus(W(r->err), true);
        UpdateNowPlaying();
        return;
    }
    IMFPMediaItem* item = nullptr;
    HRESULT hr = g_mp->CreateMediaItemFromURL(r->path.c_str(), TRUE, 0, &item);
    if (SUCCEEDED(hr)) hr = g_mp->SetMediaItem(item);
    if (FAILED(hr)) {
        if (item) item->Release();
        // A corrupt cache entry: drop it so the next attempt re-downloads.
        DeleteFileW(r->path.c_str());
        g.loading = false;
        g.playing = false;
        SetStatus(L"Couldn't play this file. Try again.", true);
        return;
    }
    g_item = item;
    SetStatus(L"", false);
    PrefetchNext();
}

static void PlayList(const vector<Track>& tracks, int start) {
    if (start < 0 || start >= (int)tracks.size()) return;
    g.queue = tracks;
    if (g.shuffle) {
        Track first = g.queue[start];
        g.queue.erase(g.queue.begin() + start);
        std::shuffle(g.queue.begin(), g.queue.end(), std::mt19937{std::random_device{}()});
        g.queue.insert(g.queue.begin(), first);
        start = 0;
    }
    LoadIndex(start);
    if (g.page == Page::Queue) RefreshView();
}

static void PlayNext(const Track& t) {
    if (g.qi < 0) return PlayList({t}, 0);
    g.queue.insert(g.queue.begin() + g.qi + 1, t);
    if (g.page == Page::Queue) RefreshView();
}

static void AddToQueue(const Track& t) {
    if (g.qi < 0) return PlayList({t}, 0);
    g.queue.push_back(t);
    if (g.page == Page::Queue) RefreshView();
}

static void TogglePlay() {
    if (!g.current()) return;
    g.playing = !g.playing;
    if (g_item) { if (g.playing) g_mp->Play(); else g_mp->Pause(); }
    UpdateNowPlaying();
}

static void Next() {
    if (g.qi < 0) return;
    if (g.qi + 1 < (int)g.queue.size()) LoadIndex(g.qi + 1);
    else if (g.repeat == Repeat::All && !g.queue.empty()) LoadIndex(0);
    else if (g.autoplay) {
        g.waitingForRadio = true;
        SetStatus(L"Finding similar songs…", false);
        FetchRadio(g.queue[g.qi].id);
    }
}

static void Prev() {
    if (g.qi < 0) return;
    if (g.pos > 3 || g.qi == 0) {
        PROPVARIANT pv;
        PropVariantInit(&pv);
        pv.vt = VT_I8;
        pv.hVal.QuadPart = 0;
        if (g_item) g_mp->SetPosition(MFP_POSITIONTYPE_100NS, &pv);
        g.pos = 0;
        InvalidateBar();
    } else {
        LoadIndex(g.qi - 1);
    }
}

static void SeekTo(double seconds) {
    if (!g_item) return;
    PROPVARIANT pv;
    PropVariantInit(&pv);
    pv.vt = VT_I8;
    pv.hVal.QuadPart = (LONGLONG)(seconds * 1e7);
    g_mp->SetPosition(MFP_POSITIONTYPE_100NS, &pv);
    g.pos = seconds;
    InvalidateBar();
}

static void OnTrackEnded() {
    if (g.repeat == Repeat::One) { SeekTo(0); g_mp->Play(); return; }
    if (g.qi + 1 < (int)g.queue.size() || g.repeat == Repeat::All || g.autoplay) {
        Next();
    } else {
        g.playing = false;
        SeekTo(0);
        g_mp->Pause();
        UpdateNowPlaying();
    }
}

static void OnRadio(RadioResult* r) {
    const Track* cur = g.current();
    if (!cur) return;
    int added = 0;
    for (auto& t : r->tracks) {
        if (added >= 25) break;
        if (std::any_of(g.queue.begin(), g.queue.end(), [&](const Track& q) { return q.id == t.id; })) continue;
        g.queue.push_back(t);
        ++added;
    }
    if (g.page == Page::Queue) RefreshView();
    // If playback already ran off the end while we were fetching, continue now.
    if (g.waitingForRadio && added) LoadIndex(g.qi + 1);
    else PrefetchNext();
}

static void Tick() {
    if (!g_item || g.dragging) return;
    PROPVARIANT pv;
    PropVariantInit(&pv);
    if (SUCCEEDED(g_mp->GetPosition(MFP_POSITIONTYPE_100NS, &pv)) && pv.vt == VT_I8) g.pos = pv.hVal.QuadPart / 1e7;
    PropVariantClear(&pv);
    PropVariantInit(&pv);
    if (SUCCEEDED(g_mp->GetDuration(MFP_POSITIONTYPE_100NS, &pv)) && (pv.vt == VT_UI8 || pv.vt == VT_I8) && pv.uhVal.QuadPart)
        g.dur = pv.uhVal.QuadPart / 1e7;
    PropVariantClear(&pv);
    SendMessageW(g_seek, TBM_SETPOS, TRUE, g.dur > 0 ? (LPARAM)(g.pos / g.dur * 1000) : 0);
    InvalidateBar();
}

// ───────────────────────────── Search ─────────────────────────────

static void StartSearch() {
    wchar_t buf[512];
    GetWindowTextW(g_search, buf, 512);
    string q = U8(buf);
    while (!q.empty() && q.back() == ' ') q.pop_back();
    while (!q.empty() && q.front() == ' ') q.erase(q.begin());
    int seq = ++g.searchSeq;
    if (q.empty()) {
        g.results.clear();
        if (g.page == Page::Search) RefreshView();
        return;
    }
    RunAsync([seq, q] {
        string body;
        auto* r = new SearchResult{seq, {}, YTMusicPost("search", ytmusic::searchBody(q), body)};
        if (r->ok) r->tracks = ytmusic::parseSearch(body);
        PostMessageW(g_hwnd, WM_APP_SEARCH, 0, (LPARAM)r);
    });
}

// ───────────────────────────── View / pages ─────────────────────────────

static wstring PageTitle() {
    switch (g.page) {
    case Page::Search: return L"Search";
    case Page::Recent: return L"Recently Played";
    case Page::Liked: return L"Liked Songs";
    case Page::Queue: return L"Queue";
    case Page::Playlist:
        return g.pagePlaylist >= 0 && g.pagePlaylist < (int)g.playlists.size() ? W(g.playlists[g.pagePlaylist].name) : L"";
    }
    return L"";
}

static void RefreshView() {
    switch (g.page) {
    case Page::Search: g.view = g.results; break;
    case Page::Recent: g.view = g.recent; break;
    case Page::Liked: g.view = g.liked; break;
    case Page::Queue: g.view = g.queue; break;
    case Page::Playlist:
        g.view = g.pagePlaylist >= 0 && g.pagePlaylist < (int)g.playlists.size() ? g.playlists[g.pagePlaylist].tracks
                                                                                  : vector<Track>{};
        break;
    }
    ListView_SetItemCountEx(g_list, (int)g.view.size(), LVSICF_NOSCROLL);
    InvalidateRect(g_list, nullptr, FALSE);
    bool queue = g.page == Page::Queue;
    SetWindowTextW(g_btn[IDC_HEAD1 - IDC_SHUFFLE], queue ? L"Clear queue" : L"Play");
    SetWindowTextW(g_btn[IDC_HEAD2 - IDC_SHUFFLE], queue ? (g.autoplay ? L"Autoplay: On" : L"Autoplay: Off") : L"Shuffle");
    EnableWindow(g_btn[IDC_HEAD1 - IDC_SHUFFLE], !g.view.empty());
    EnableWindow(g_btn[IDC_HEAD2 - IDC_SHUFFLE], queue || !g.view.empty());
    InvalidateRect(g_btn[IDC_HEAD1 - IDC_SHUFFLE], nullptr, FALSE);
    InvalidateRect(g_btn[IDC_HEAD2 - IDC_SHUFFLE], nullptr, FALSE);
    RECT rc;
    GetClientRect(g_hwnd, &rc);
    RECT head{S(220), 0, rc.right, S(140)};
    InvalidateRect(g_hwnd, &head, FALSE);
}

static void ShowPage(Page p, int playlist = -1) {
    g.page = p;
    g.pagePlaylist = playlist;
    ListView_SetItemState(g_list, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    RefreshView();
    ListView_EnsureVisible(g_list, 0, FALSE);
    int sel = p == Page::Search ? 0 : p == Page::Recent ? 1 : p == Page::Liked ? 2 : p == Page::Queue ? 3 : 5 + playlist;
    SendMessageW(g_sidebar, LB_SETCURSEL, sel, 0);
}

static void RefreshSidebar() {
    SendMessageW(g_sidebar, WM_SETREDRAW, FALSE, 0);
    SendMessageW(g_sidebar, LB_RESETCONTENT, 0, 0);
    for (const wchar_t* s : {L"Search", L"Recently Played", L"Liked Songs", L"Queue", L"PLAYLISTS"})
        SendMessageW(g_sidebar, LB_ADDSTRING, 0, (LPARAM)s);
    for (auto& p : g.playlists) SendMessageW(g_sidebar, LB_ADDSTRING, 0, (LPARAM)W(p.name).c_str());
    SendMessageW(g_sidebar, WM_SETREDRAW, TRUE, 0);
    int sel = g.page == Page::Search ? 0 : g.page == Page::Recent ? 1 : g.page == Page::Liked ? 2
            : g.page == Page::Queue ? 3 : 5 + g.pagePlaylist;
    SendMessageW(g_sidebar, LB_SETCURSEL, sel, 0);
    InvalidateRect(g_sidebar, nullptr, TRUE);
}

static void UpdateNowPlaying() {
    const Track* t = g.current();
    wstring title = t ? W(t->title) + L" · " + W(t->artist) + L" — Mosics" : L"Mosics";
    SetWindowTextW(g_hwnd, title.c_str());
    InvalidateBar();
    for (int id : {IDC_PLAY, IDC_LIKE, IDC_SHUFFLE, IDC_REPEAT}) InvalidateRect(g_btn[id - IDC_SHUFFLE], nullptr, FALSE);
    InvalidateRect(g_list, nullptr, FALSE);
}

static void InvalidateBar() {
    if (!g_hwnd) return;
    RECT rc;
    GetClientRect(g_hwnd, &rc);
    rc.top = rc.bottom - S(88);
    InvalidateRect(g_hwnd, &rc, FALSE);
}

// ───────────────────────────── Prompt dialog ─────────────────────────────

static bool PromptText(HWND owner, const wchar_t* title, wstring& value) {
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
            case WM_CTLCOLORSTATIC: case WM_CTLCOLOREDIT:
                SetTextColor((HDC)w, color::text);
                SetBkColor((HDC)w, m == WM_CTLCOLOREDIT ? color::field : color::bar);
                return (LRESULT)(m == WM_CTLCOLOREDIT ? g_brField : g_brBar);
            }
            return DefWindowProcW(h, m, w, l);
        };
        wc.hInstance = g_inst;
        wc.hbrBackground = g_brBar;
        wc.lpszClassName = L"MosicsPrompt";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        RegisterClassW(&wc);
        registered = true;
    }
    RECT orc;
    GetWindowRect(owner, &orc);
    int w = S(360), h = S(150);
    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"MosicsPrompt", title, WS_POPUP | WS_CAPTION | WS_SYSMENU,
                               (orc.left + orc.right - w) / 2, (orc.top + orc.bottom - h) / 2, w, h, owner, nullptr,
                               g_inst, nullptr);
    BOOL dark = TRUE;
    DwmSetWindowAttribute(dlg, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof dark);
    SetWindowLongPtrW(dlg, GWLP_USERDATA, (LONG_PTR)&st);
    st.edit = CreateWindowExW(0, L"EDIT", value.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | WS_BORDER,
                              S(16), S(16), w - S(48), S(28), dlg, (HMENU)100, g_inst, nullptr);
    HWND ok = CreateWindowExW(0, L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                              w - S(212), S(60), S(90), S(30), dlg, (HMENU)IDOK, g_inst, nullptr);
    HWND cancel = CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                  w - S(114), S(60), S(90), S(30), dlg, (HMENU)IDCANCEL, g_inst, nullptr);
    for (HWND c : {st.edit, ok, cancel}) SendMessageW(c, WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(st.edit, EM_SETSEL, 0, -1);
    EnableWindow(owner, FALSE);
    ShowWindow(dlg, SW_SHOW);
    SetFocus(st.edit);
    MSG msg;
    while (!st.done && GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(dlg, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    EnableWindow(owner, TRUE);
    DestroyWindow(dlg);
    SetForegroundWindow(owner);
    return st.ok;
}

// ───────────────────────────── Context menus ─────────────────────────────

static vector<int> SelectedRows() {
    vector<int> rows;
    for (int i = -1; (i = ListView_GetNextItem(g_list, i, LVNI_SELECTED)) != -1;) rows.push_back(i);
    return rows;
}

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

static void TrackMenu(POINT pt) {
    vector<int> rows = SelectedRows();
    if (rows.empty()) return;
    vector<Track> picked;
    for (int r : rows) picked.push_back(g.view[r]);
    const Track& first = picked[0];

    HMENU m = CreatePopupMenu(), pl = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, ID_PLAY, L"Play");
    AppendMenuW(m, MF_STRING, ID_PLAYNEXT, L"Play next");
    AppendMenuW(m, MF_STRING, ID_ADDQUEUE, L"Add to queue");
    AppendMenuW(m, MF_STRING, ID_RADIO, L"Start radio");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_LIKE, IsLiked(first.id) ? L"Remove from Liked Songs" : L"Save to Liked Songs");
    AppendMenuW(pl, MF_STRING, ID_NEWPL_WITH, L"New playlist…");
    if (!g.playlists.empty()) AppendMenuW(pl, MF_SEPARATOR, 0, nullptr);
    for (size_t i = 0; i < g.playlists.size(); ++i)
        AppendMenuW(pl, MF_STRING, ID_ADDPL_BASE + i, W(g.playlists[i].name).c_str());
    AppendMenuW(m, MF_POPUP, (UINT_PTR)pl, L"Add to playlist");
    if (g.page == Page::Playlist) AppendMenuW(m, MF_STRING, ID_REMOVE, L"Remove from this playlist");
    if (g.page == Page::Queue) AppendMenuW(m, MF_STRING, ID_REMOVE, L"Remove from queue");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_COPYLINK, L"Copy song link");

    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_hwnd, nullptr);
    DestroyMenu(m);   // also destroys the submenu

    switch (cmd) {
    case ID_PLAY:
        if (g.page == Page::Queue) LoadIndex(rows[0]); else PlayList(picked, 0);
        break;
    case ID_PLAYNEXT:
        for (auto it = picked.rbegin(); it != picked.rend(); ++it) PlayNext(*it);
        break;
    case ID_ADDQUEUE:
        for (auto& t : picked) AddToQueue(t);
        break;
    case ID_RADIO:
        PlayList({first}, 0);
        FetchRadio(first.id);
        break;
    case ID_LIKE: {
        bool like = !IsLiked(first.id);
        for (auto& t : picked) if (IsLiked(t.id) != like) ToggleLike(t);
        if (g.page == Page::Liked) RefreshView();
        UpdateNowPlaying();
        break;
    }
    case ID_NEWPL_WITH: {
        wstring name = W(first.title);
        if (!PromptText(g_hwnd, L"New playlist", name) || name.empty()) break;
        g.playlists.push_back({U8(name), picked});
        SaveLibrary();
        RefreshSidebar();
        break;
    }
    case ID_REMOVE:
        if (g.page == Page::Playlist) {
            auto& tracks = g.playlists[g.pagePlaylist].tracks;
            for (auto it = rows.rbegin(); it != rows.rend(); ++it) tracks.erase(tracks.begin() + *it);
            SaveLibrary();
        } else if (g.page == Page::Queue) {
            for (auto it = rows.rbegin(); it != rows.rend(); ++it) {
                if (*it == g.qi) continue;   // keep the playing song
                g.queue.erase(g.queue.begin() + *it);
                if (*it < g.qi) --g.qi;
            }
        }
        ListView_SetItemState(g_list, -1, 0, LVIS_SELECTED);
        RefreshView();
        break;
    case ID_COPYLINK:
        CopyText(L"https://music.youtube.com/watch?v=" + W(first.id));
        break;
    default:
        if (cmd >= ID_ADDPL_BASE && cmd < ID_ADDPL_BASE + (int)g.playlists.size()) {
            auto& tracks = g.playlists[cmd - ID_ADDPL_BASE].tracks;
            for (auto& t : picked)
                if (std::none_of(tracks.begin(), tracks.end(), [&](const Track& x) { return x.id == t.id; }))
                    tracks.push_back(t);
            SaveLibrary();
            if (g.page == Page::Playlist && g.pagePlaylist == cmd - ID_ADDPL_BASE) RefreshView();
        }
    }
}

static void NewPlaylist() {
    wstring name = L"Playlist " + std::to_wstring(g.playlists.size() + 1);
    if (!PromptText(g_hwnd, L"New playlist", name) || name.empty()) return;
    g.playlists.push_back({U8(name), {}});
    SaveLibrary();
    RefreshSidebar();
    ShowPage(Page::Playlist, (int)g.playlists.size() - 1);
}

static void PlaylistMenu(int index, POINT pt) {
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, ID_PL_RENAME, L"Rename…");
    AppendMenuW(m, MF_STRING, ID_PL_DELETE, L"Delete");
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_hwnd, nullptr);
    DestroyMenu(m);
    if (cmd == ID_PL_RENAME) {
        wstring name = W(g.playlists[index].name);
        if (PromptText(g_hwnd, L"Rename playlist", name) && !name.empty()) {
            g.playlists[index].name = U8(name);
            SaveLibrary();
            RefreshSidebar();
            RefreshView();
        }
    } else if (cmd == ID_PL_DELETE) {
        wstring msg = L"Delete \"" + W(g.playlists[index].name) + L"\"?";
        if (MessageBoxW(g_hwnd, msg.c_str(), L"Mosics", MB_OKCANCEL | MB_ICONQUESTION) != IDOK) return;
        g.playlists.erase(g.playlists.begin() + index);
        SaveLibrary();
        if (g.page == Page::Playlist) {
            if (g.pagePlaylist == index) g.page = Page::Liked;
            else if (g.pagePlaylist > index) --g.pagePlaylist;
        }
        RefreshSidebar();
        ShowPage(g.page, g.pagePlaylist);
    }
}

// ───────────────────────────── Drawing ─────────────────────────────

static void FillRectColor(HDC dc, const RECT& r, COLORREF c) {
    SetDCBrushColor(dc, c);
    FillRect(dc, &r, (HBRUSH)GetStockObject(DC_BRUSH));
}

static void DrawTextIn(HDC dc, const wstring& s, RECT r, HFONT font, COLORREF c, UINT flags) {
    SelectObject(dc, font);
    SetTextColor(dc, c);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, s.c_str(), (int)s.size(), &r, flags | DT_NOPREFIX);
}

static void PaintMain(HDC dc, const RECT& rc) {
    int side = S(220), barH = S(88);
    RECT sideRc{0, 0, side, rc.bottom - barH};
    FillRectColor(dc, sideRc, color::side);
    RECT main{side, 0, rc.right, rc.bottom - barH};
    FillRectColor(dc, main, color::bg);

    // Search field background
    RECT sr{side + S(20), S(16), rc.right - S(20), S(56)};
    HBRUSH b = CreateSolidBrush(color::field);
    HPEN oldPen = (HPEN)SelectObject(dc, GetStockObject(NULL_PEN));
    HBRUSH oldBr = (HBRUSH)SelectObject(dc, b);
    RoundRect(dc, sr.left, sr.top, sr.right, sr.bottom, S(40), S(40));
    SelectObject(dc, oldBr);
    SelectObject(dc, oldPen);
    DeleteObject(b);
    RECT ic{sr.left + S(12), sr.top, sr.left + S(36), sr.bottom};
    DrawTextIn(dc, L"", ic, g_fontIcon, color::dim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    // Page title + count
    RECT tr{side + S(24), S(70), rc.right - S(260), S(106)};
    DrawTextIn(dc, PageTitle(), tr, g_fontTitle, color::text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    wstring sub = std::to_wstring(g.view.size()) + (g.view.size() == 1 ? L" song" : L" songs");
    if (g.page == Page::Search && g.view.empty()) sub = L"Audio only — no video, no ads.";
    RECT cr{side + S(24), S(106), rc.right - S(260), S(128)};
    DrawTextIn(dc, sub, cr, g_fontSmall, color::dim, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    // Sidebar footer "+ New playlist" is a button; draw a hairline above the bar.
    RECT bar{0, rc.bottom - barH, rc.right, rc.bottom};
    FillRectColor(dc, bar, color::bar);
    RECT line{0, bar.top, rc.right, bar.top + 1};
    FillRectColor(dc, line, RGB(40, 40, 40));

    // Now playing (left)
    const Track* t = g.current();
    RECT art{S(16), bar.top + S(16), S(16) + S(56), bar.top + S(72)};
    if (g.art) {
        Gdiplus::Graphics gr(dc);
        gr.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        gr.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        Gdiplus::GraphicsPath path;
        int r = S(6), x = art.left, y = art.top, w = S(56);
        path.AddArc(x, y, 2 * r, 2 * r, 180, 90);
        path.AddArc(x + w - 2 * r, y, 2 * r, 2 * r, 270, 90);
        path.AddArc(x + w - 2 * r, y + w - 2 * r, 2 * r, 2 * r, 0, 90);
        path.AddArc(x, y + w - 2 * r, 2 * r, 2 * r, 90, 90);
        path.CloseFigure();
        gr.SetClip(&path);
        gr.DrawImage(g.art, x, y, w, w);
    } else {
        FillRectColor(dc, art, RGB(40, 40, 40));
        DrawTextIn(dc, L"", art, g_fontIconBig, color::faint, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    int textL = art.right + S(12), textR = S(16) + S(290);
    RECT t1{textL, bar.top + S(20), textR, bar.top + S(42)};
    RECT t2{textL, bar.top + S(44), textR, bar.top + S(66)};
    DrawTextIn(dc, t ? W(t->title) : L"Not playing", t1, g_fontBold, color::text,
               DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    if (!g.status.empty() && (g.statusIsError || g.loading))
        DrawTextIn(dc, g.status, t2, g_fontSmall, g.statusIsError ? color::error : color::dim,
                   DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    else
        DrawTextIn(dc, t ? W(t->artist) : L"Search for something to play", t2, g_fontSmall, color::dim,
                   DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);

    // Times either side of the seek bar
    RECT seek;
    GetWindowRect(g_seek, &seek);
    MapWindowPoints(nullptr, g_hwnd, (POINT*)&seek, 2);
    RECT lt{seek.left - S(52), seek.top, seek.left - S(6), seek.bottom};
    RECT rt{seek.right + S(6), seek.top, seek.right + S(52), seek.bottom};
    DrawTextIn(dc, FormatTime(g.pos), lt, g_fontSmall, color::dim, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    DrawTextIn(dc, FormatTime(g.dur), rt, g_fontSmall, color::dim, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    // Volume icon
    RECT vol;
    GetWindowRect(g_vol, &vol);
    MapWindowPoints(nullptr, g_hwnd, (POINT*)&vol, 2);
    RECT vi{vol.left - S(28), vol.top, vol.left - S(4), vol.bottom};
    DrawTextIn(dc, g.volume <= 0.001f ? glyph::mute : glyph::volume, vi, g_fontIcon, color::dim,
               DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

static void DrawButton(const DRAWITEMSTRUCT* d) {
    HDC dc = d->hDC;
    RECT r = d->rcItem;
    int id = (int)d->CtlID;
    bool hot = g_hoverBtn == id, down = d->itemState & ODS_SELECTED, disabled = d->itemState & ODS_DISABLED;
    HWND parent = GetParent(d->hwndItem);
    COLORREF bg = parent == g_hwnd && id != IDC_NEWPL && id != IDC_HEAD1 && id != IDC_HEAD2 ? color::bar
                : id == IDC_NEWPL ? color::side : color::bg;
    FillRectColor(dc, r, bg);
    Gdiplus::Graphics gr(dc);
    gr.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    auto icon = [&](const wchar_t* gl, COLORREF c, HFONT f) {
        DrawTextIn(dc, gl, r, f, c, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    };
    COLORREF base = disabled ? color::faint : hot ? color::text : color::dim;
    switch (id) {
    case IDC_PLAY: {
        int w = r.right - r.left, h = r.bottom - r.top, s = std::min(w, h) - 2;
        Gdiplus::SolidBrush brush(disabled ? Gdiplus::Color(255, 90, 90, 90)
                                  : hot || down ? Gdiplus::Color(255, 255, 255, 255) : Gdiplus::Color(255, 235, 235, 235));
        gr.FillEllipse(&brush, (w - s) / 2, (h - s) / 2, s, s);
        icon(g.playing ? glyph::pause : glyph::play, RGB(0, 0, 0), g_fontIcon);
        break;
    }
    case IDC_SHUFFLE: icon(glyph::shuffle, g.shuffle ? color::accent : base, g_fontIcon); break;
    case IDC_REPEAT:
        icon(g.repeat == Repeat::One ? glyph::repeatOne : glyph::repeat, g.repeat != Repeat::Off ? color::accent : base,
             g_fontIcon);
        break;
    case IDC_PREV: icon(glyph::prev, base, g_fontIcon); break;
    case IDC_NEXT: icon(glyph::next, base, g_fontIcon); break;
    case IDC_QUEUEBTN: icon(glyph::list, g.page == Page::Queue ? color::accent : base, g_fontIcon); break;
    case IDC_LIKE: {
        const Track* t = g.current();
        bool liked = t && IsLiked(t->id);
        if (t) icon(liked ? glyph::heartFill : glyph::heart, liked ? color::accent : base, g_fontIcon);
        break;
    }
    case IDC_NEWPL: {
        RECT ir{r.left + S(14), r.top, r.left + S(40), r.bottom};
        DrawTextIn(dc, glyph::add, ir, g_fontIcon, hot ? color::text : color::dim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        RECT tr{r.left + S(46), r.top, r.right, r.bottom};
        DrawTextIn(dc, L"New playlist", tr, g_font, hot ? color::text : color::dim, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        break;
    }
    case IDC_HEAD1:
    case IDC_HEAD2: {
        bool primary = id == IDC_HEAD1 && g.page != Page::Queue;
        int w = r.right - r.left, h = r.bottom - r.top;
        Gdiplus::GraphicsPath path;
        int rad = h - 2;
        path.AddArc(1, 1, rad, rad, 90, 180);
        path.AddArc(w - rad - 2, 1, rad, rad, 270, 180);
        path.CloseFigure();
        if (primary) {
            Gdiplus::SolidBrush brush(disabled ? Gdiplus::Color(255, 60, 60, 60)
                                      : hot ? Gdiplus::Color(255, 60, 230, 120) : Gdiplus::Color(255, 30, 215, 96));
            gr.FillPath(&brush, &path);
        } else {
            Gdiplus::Pen pen(hot ? Gdiplus::Color(255, 255, 255, 255) : Gdiplus::Color(255, 120, 120, 120), 1.0f);
            gr.DrawPath(&pen, &path);
        }
        wchar_t text[64];
        GetWindowTextW(d->hwndItem, text, 64);
        DrawTextIn(dc, text, r, g_fontBold, primary ? RGB(0, 0, 0) : disabled ? color::faint : color::text,
                   DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        break;
    }
    }
}

static void DrawSidebarItem(const DRAWITEMSTRUCT* d) {
    if (d->itemID == (UINT)-1) return;
    HDC dc = d->hDC;
    RECT r = d->rcItem;
    FillRectColor(dc, r, color::side);
    int i = (int)d->itemID;
    wchar_t text[256];
    SendMessageW(g_sidebar, LB_GETTEXT, i, (LPARAM)text);
    if (i == 4) {   // section header
        RECT tr{r.left + S(20), r.top + S(8), r.right, r.bottom};
        DrawTextIn(dc, text, tr, g_fontSmall, color::faint, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        return;
    }
    bool sel = d->itemState & ODS_SELECTED;
    if (sel) {
        RECT hr{r.left + S(8), r.top + S(2), r.right - S(8), r.bottom - S(2)};
        SetDCBrushColor(dc, RGB(40, 40, 40));
        SelectObject(dc, GetStockObject(DC_BRUSH));
        SelectObject(dc, GetStockObject(NULL_PEN));
        RoundRect(dc, hr.left, hr.top, hr.right, hr.bottom, S(8), S(8));
    }
    static const wchar_t* icons[] = {L"", L"", L"", L""};
    const wchar_t* ic = i < 4 ? icons[i] : L"";
    RECT ir{r.left + S(18), r.top, r.left + S(44), r.bottom};
    DrawTextIn(dc, ic, ir, g_fontIcon, i == 2 ? color::accent : sel ? color::text : color::dim,
               DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    RECT tr{r.left + S(52), r.top, r.right - S(12), r.bottom};
    DrawTextIn(dc, text, tr, sel ? g_fontBold : g_font, sel ? color::text : color::dim,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

// Custom-drawn trackbar: thin rounded channel, green fill up to the thumb, small white knob.
static LRESULT DrawSlider(NMCUSTOMDRAW* cd) {
    switch (cd->dwDrawStage) {
    case CDDS_PREPAINT: return CDRF_NOTIFYITEMDRAW;
    case CDDS_ITEMPREPAINT: {
        HWND tb = cd->hdr.hwndFrom;
        if (cd->dwItemSpec == TBCD_TICS) return CDRF_SKIPDEFAULT;
        RECT client;
        GetClientRect(tb, &client);
        if (cd->dwItemSpec == TBCD_CHANNEL) {
            FillRectColor(cd->hdc, client, color::bar);
            RECT thumb;
            SendMessageW(tb, TBM_GETTHUMBRECT, 0, (LPARAM)&thumb);
            int cy = (client.top + client.bottom) / 2, h = S(4), mid = (thumb.left + thumb.right) / 2;
            int x0 = client.left + S(6), x1 = client.right - S(6);
            Gdiplus::Graphics gr(cd->hdc);
            gr.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            Gdiplus::SolidBrush track(Gdiplus::Color(255, 77, 77, 77));
            Gdiplus::SolidBrush fill(Gdiplus::Color(255, 30, 215, 96));
            gr.FillRectangle(&track, x0, cy - h / 2, x1 - x0, h);
            if (mid > x0) gr.FillRectangle(&fill, x0, cy - h / 2, std::min(mid, x1) - x0, h);
            return CDRF_SKIPDEFAULT;
        }
        if (cd->dwItemSpec == TBCD_THUMB) {
            RECT t = cd->rc;
            int cx = (t.left + t.right) / 2, cy = (client.top + client.bottom) / 2, rad = S(6);
            Gdiplus::Graphics gr(cd->hdc);
            gr.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            Gdiplus::SolidBrush knob(Gdiplus::Color(255, 255, 255, 255));
            gr.FillEllipse(&knob, cx - rad, cy - rad, 2 * rad, 2 * rad);
            return CDRF_SKIPDEFAULT;
        }
        return CDRF_DODEFAULT;
    }
    }
    return CDRF_DODEFAULT;
}

static LRESULT DrawListItem(NMLVCUSTOMDRAW* cd) {
    switch (cd->nmcd.dwDrawStage) {
    case CDDS_PREPAINT: return CDRF_NOTIFYITEMDRAW;
    case CDDS_ITEMPREPAINT: return CDRF_NOTIFYSUBITEMDRAW;
    case CDDS_ITEMPREPAINT | CDDS_SUBITEM: {
        size_t row = cd->nmcd.dwItemSpec;
        const Track* cur = g.current();
        bool isCurrent = cur && row < g.view.size() && g.view[row].id == cur->id &&
                         (g.page != Page::Queue || (int)row == g.qi);
        cd->clrText = isCurrent ? color::accent : cd->iSubItem == 0 ? color::text : color::dim;
        return CDRF_NEWFONT;
    }
    }
    return CDRF_DODEFAULT;
}

// ───────────────────────────── Layout & fonts ─────────────────────────────

static HFONT MakeFont(const wchar_t* face, int px, int weight) {
    return CreateFontW(-S(px), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, face);
}

static void CreateFonts() {
    for (HFONT f : {g_font, g_fontBold, g_fontTitle, g_fontSmall, g_fontIcon, g_fontIconBig}) if (f) DeleteObject(f);
    g_font = MakeFont(L"Segoe UI", 14, FW_NORMAL);
    g_fontBold = MakeFont(L"Segoe UI Semibold", 14, FW_SEMIBOLD);
    g_fontTitle = MakeFont(L"Segoe UI", 30, FW_BOLD);
    g_fontSmall = MakeFont(L"Segoe UI", 12, FW_NORMAL);
    g_fontIcon = MakeFont(g_iconFace.c_str(), 16, FW_NORMAL);
    g_fontIconBig = MakeFont(g_iconFace.c_str(), 22, FW_NORMAL);
    for (HWND h : {g_search, g_list, g_sidebar}) if (h) SendMessageW(h, WM_SETFONT, (WPARAM)g_font, TRUE);
    if (g_sidebar) SendMessageW(g_sidebar, LB_SETITEMHEIGHT, 0, S(40));
    if (g_list) {   // a 1px-wide image list is the standard trick for taller ListView rows
        HIMAGELIST old = ListView_SetImageList(g_list, ImageList_Create(1, S(36), ILC_COLOR32, 1, 0), LVSIL_SMALL);
        if (old) ImageList_Destroy(old);
    }
}

static void Layout() {
    RECT rc;
    GetClientRect(g_hwnd, &rc);
    int side = S(220), barH = S(88), top = rc.bottom - barH;
    HDWP dw = BeginDeferWindowPos(16);
    auto put = [&](HWND h, int x, int y, int w, int hh) {
        dw = DeferWindowPos(dw, h, nullptr, x, y, std::max(w, 0), std::max(hh, 0), SWP_NOZORDER | SWP_NOACTIVATE);
    };
    put(g_sidebar, 0, S(12), side, top - S(12) - S(52));
    put(g_btn[IDC_NEWPL - IDC_SHUFFLE], 0, top - S(48), side, S(40));
    put(g_search, side + S(44), S(26), rc.right - side - S(80), S(22));
    put(g_btn[IDC_HEAD1 - IDC_SHUFFLE], rc.right - S(244), S(80), S(104), S(36));
    put(g_btn[IDC_HEAD2 - IDC_SHUFFLE], rc.right - S(130), S(80), S(110), S(36));
    put(g_list, side + S(12), S(140), rc.right - side - S(24), top - S(140));

    int cx = rc.right / 2;
    put(g_btn[IDC_SHUFFLE - IDC_SHUFFLE], cx - S(116), top + S(10), S(36), S(36));
    put(g_btn[IDC_PREV - IDC_SHUFFLE], cx - S(70), top + S(10), S(36), S(36));
    put(g_btn[IDC_PLAY - IDC_SHUFFLE], cx - S(20), top + S(8), S(40), S(40));
    put(g_btn[IDC_NEXT - IDC_SHUFFLE], cx + S(34), top + S(10), S(36), S(36));
    put(g_btn[IDC_REPEAT - IDC_SHUFFLE], cx + S(80), top + S(10), S(36), S(36));
    int seekW = std::min(S(520), int(rc.right) - S(720));
    seekW = std::max(seekW, S(160));
    put(g_seek, cx - seekW / 2, top + S(52), seekW, S(24));
    put(g_btn[IDC_LIKE - IDC_SHUFFLE], S(16) + S(290), top + S(26), S(36), S(36));
    put(g_btn[IDC_QUEUEBTN - IDC_SHUFFLE], rc.right - S(200), top + S(26), S(36), S(36));
    put(g_vol, rc.right - S(132), top + S(32), S(116), S(24));
    EndDeferWindowPos(dw);

    ListView_SetColumnWidth(g_list, 3, S(64));
    int listW = rc.right - side - S(24) - GetSystemMetrics(SM_CXVSCROLL) - S(64);
    ListView_SetColumnWidth(g_list, 0, listW * 40 / 100);
    ListView_SetColumnWidth(g_list, 1, listW * 28 / 100);
    ListView_SetColumnWidth(g_list, 2, listW - listW * 40 / 100 - listW * 28 / 100);
    InvalidateRect(g_hwnd, nullptr, TRUE);
}

// ───────────────────────────── Window procedure ─────────────────────────────

static WNDPROC g_btnProc;
static LRESULT CALLBACK ButtonHoverProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_MOUSEMOVE && g_hoverBtn != GetDlgCtrlID(h)) {
        g_hoverBtn = GetDlgCtrlID(h);
        InvalidateRect(h, nullptr, FALSE);
        TRACKMOUSEEVENT tme{sizeof tme, TME_LEAVE, h, 0};
        TrackMouseEvent(&tme);
    } else if (m == WM_MOUSELEAVE) {
        g_hoverBtn = -1;
        InvalidateRect(h, nullptr, FALSE);
    } else if (m == WM_SETCURSOR) {
        SetCursor(LoadCursor(nullptr, IDC_HAND));
        return TRUE;
    }
    return CallWindowProcW(g_btnProc, h, m, w, l);
}

// The ListView's header notifies the ListView, not us — subclass to recolour header text.
static WNDPROC g_listProc;
static LRESULT CALLBACK ListProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_NOTIFY) {
        auto* hdr = (NMHDR*)l;
        if (hdr->hwndFrom == ListView_GetHeader(h) && hdr->code == NM_CUSTOMDRAW) {
            auto* cd = (NMCUSTOMDRAW*)l;
            if (cd->dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
            if (cd->dwDrawStage == CDDS_ITEMPREPAINT) {
                FillRectColor(cd->hdc, cd->rc, color::bg);
                RECT tr = cd->rc;
                tr.left += S(8);
                wchar_t text[64] = L"";
                HDITEMW item{};
                item.mask = HDI_TEXT;
                item.pszText = text;
                item.cchTextMax = 64;
                Header_GetItem(hdr->hwndFrom, cd->dwItemSpec, &item);
                DrawTextIn(cd->hdc, text, tr, g_fontSmall, color::dim,
                           (cd->dwItemSpec == 3 ? DT_RIGHT : DT_LEFT) | DT_VCENTER | DT_SINGLELINE);
                RECT line{cd->rc.left, cd->rc.bottom - 1, cd->rc.right, cd->rc.bottom};
                FillRectColor(cd->hdc, line, RGB(40, 40, 40));
                return CDRF_SKIPDEFAULT;
            }
        }
    }
    return CallWindowProcW(g_listProc, h, m, w, l);
}

// Clicking anywhere on a slider jumps there (the stock trackbar only pages by a fixed step).
static WNDPROC g_sliderProc;
static LRESULT CALLBACK SliderProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_LBUTTONDOWN) {
        RECT ch, th;
        SendMessageW(h, TBM_GETCHANNELRECT, 0, (LPARAM)&ch);
        SendMessageW(h, TBM_GETTHUMBRECT, 0, (LPARAM)&th);
        int x = GET_X_LPARAM(l), half = (th.right - th.left) / 2;
        int lo = (int)SendMessageW(h, TBM_GETRANGEMIN, 0, 0), hi = (int)SendMessageW(h, TBM_GETRANGEMAX, 0, 0);
        int span = std::max(1, (int)(ch.right - ch.left) - 2 * half);
        int pos = lo + MulDiv(std::clamp(x - (int)ch.left - half, 0, span), hi - lo, span);
        SendMessageW(h, TBM_SETPOS, TRUE, pos);
        SendMessageW(GetParent(h), WM_HSCROLL, MAKEWPARAM(TB_THUMBTRACK, pos), (LPARAM)h);
        // Fall through: the thumb is now under the cursor, so the default handler starts a drag.
    }
    return CallWindowProcW(g_sliderProc, h, m, w, l);
}

static HWND MakeButton(int id, const wchar_t* text = L"") {
    HWND b = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 0, 0, g_hwnd,
                             (HMENU)(INT_PTR)id, g_inst, nullptr);
    g_btnProc = (WNDPROC)SetWindowLongPtrW(b, GWLP_WNDPROC, (LONG_PTR)ButtonHoverProc);
    g_btn[id - IDC_SHUFFLE] = b;
    return b;
}

static void CreateControls() {
    g_sidebar = CreateWindowExW(0, L"LISTBOX", nullptr,
                                WS_CHILD | WS_VISIBLE | LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS |
                                    LBS_NOINTEGRALHEIGHT | WS_VSCROLL,
                                0, 0, 0, 0, g_hwnd, (HMENU)IDC_SIDEBAR, g_inst, nullptr);
    SetWindowTheme(g_sidebar, L"DarkMode_Explorer", nullptr);

    g_search = CreateWindowExW(0, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0, 0, g_hwnd,
                               (HMENU)IDC_SEARCH, g_inst, nullptr);
    SendMessageW(g_search, EM_SETCUEBANNER, TRUE, (LPARAM)L"What do you want to listen to?");

    g_list = CreateWindowExW(0, WC_LISTVIEWW, nullptr,
                             WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS, 0, 0, 0, 0,
                             g_hwnd, (HMENU)IDC_LIST, g_inst, nullptr);
    ListView_SetExtendedListViewStyle(g_list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    SetWindowTheme(g_list, L"DarkMode_Explorer", nullptr);
    ListView_SetBkColor(g_list, color::bg);
    ListView_SetTextBkColor(g_list, color::bg);
    ListView_SetTextColor(g_list, color::text);
    const wchar_t* cols[] = {L"Title", L"Artist", L"Album", L"Time"};
    for (int i = 0; i < 4; ++i) {
        LVCOLUMNW c{};
        c.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
        c.fmt = i == 3 ? LVCFMT_RIGHT : LVCFMT_LEFT;
        c.pszText = (LPWSTR)cols[i];
        c.cx = 100;
        ListView_InsertColumn(g_list, i, &c);
    }
    g_listProc = (WNDPROC)SetWindowLongPtrW(g_list, GWLP_WNDPROC, (LONG_PTR)ListProc);
    SetWindowTheme(ListView_GetHeader(g_list), L"DarkMode_ItemsView", nullptr);

    g_seek = CreateWindowExW(0, TRACKBAR_CLASSW, nullptr, WS_CHILD | WS_VISIBLE | TBS_NOTICKS | TBS_BOTH, 0, 0, 0, 0,
                             g_hwnd, (HMENU)IDC_SEEK, g_inst, nullptr);
    SendMessageW(g_seek, TBM_SETRANGE, FALSE, MAKELPARAM(0, 1000));
    SendMessageW(g_seek, TBM_SETPAGESIZE, 0, 50);
    g_vol = CreateWindowExW(0, TRACKBAR_CLASSW, nullptr, WS_CHILD | WS_VISIBLE | TBS_NOTICKS | TBS_BOTH, 0, 0, 0, 0,
                            g_hwnd, (HMENU)IDC_VOLUME, g_inst, nullptr);
    SendMessageW(g_vol, TBM_SETRANGE, FALSE, MAKELPARAM(0, 100));
    SendMessageW(g_vol, TBM_SETPOS, TRUE, (LPARAM)(g.volume * 100));
    g_sliderProc = (WNDPROC)SetWindowLongPtrW(g_seek, GWLP_WNDPROC, (LONG_PTR)SliderProc);
    SetWindowLongPtrW(g_vol, GWLP_WNDPROC, (LONG_PTR)SliderProc);

    for (int id = IDC_SHUFFLE; id <= IDC_NEWPL; ++id) MakeButton(id);
    CreateFonts();
    RefreshSidebar();
    RefreshView();
}

static void PlayRow(int row) {
    if (row < 0 || row >= (int)g.view.size()) return;
    if (g.page == Page::Queue) LoadIndex(row); else PlayList(g.view, row);
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE: {
        g_hwnd = h;
        g_dpi = GetDpiForWindow(h);
        BOOL dark = TRUE;
        DwmSetWindowAttribute(h, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof dark);
        CreateControls();
        SendMessageW(h, WM_CHANGEUISTATE, MAKEWPARAM(UIS_SET, UISF_HIDEFOCUS), 0);
        SetTimer(h, TIMER_TICK, 250, nullptr);
        RegisterHotKey(h, HOTKEY_PLAY, MOD_NOREPEAT, VK_MEDIA_PLAY_PAUSE);
        RegisterHotKey(h, HOTKEY_NEXT, MOD_NOREPEAT, VK_MEDIA_NEXT_TRACK);
        RegisterHotKey(h, HOTKEY_PREV, MOD_NOREPEAT, VK_MEDIA_PREV_TRACK);
        return 0;
    }
    case WM_SIZE: Layout(); return 0;
    case WM_GETMINMAXINFO: {
        auto* mm = (MINMAXINFO*)l;
        mm->ptMinTrackSize = {S(820), S(520)};
        return 0;
    }
    case WM_DPICHANGED: {
        g_dpi = HIWORD(w);
        CreateFonts();
        auto* r = (RECT*)l;
        SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        Layout();
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc;
        GetClientRect(h, &rc);
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
        HGDIOBJ old = SelectObject(mem, bmp);
        PaintMain(mem, rc);
        BitBlt(dc, ps.rcPaint.left, ps.rcPaint.top, ps.rcPaint.right - ps.rcPaint.left,
               ps.rcPaint.bottom - ps.rcPaint.top, mem, ps.rcPaint.left, ps.rcPaint.top, SRCCOPY);
        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_CTLCOLOREDIT:
        SetTextColor((HDC)w, color::text);
        SetBkColor((HDC)w, color::field);
        return (LRESULT)g_brField;
    case WM_CTLCOLORLISTBOX:
        SetBkColor((HDC)w, color::side);
        return (LRESULT)g_brSide;
    case WM_CTLCOLORSTATIC:   // trackbars
        SetBkColor((HDC)w, color::bar);
        return (LRESULT)g_brBar;
    case WM_MEASUREITEM: {
        auto* mi = (MEASUREITEMSTRUCT*)l;
        if (mi->CtlID == IDC_SIDEBAR) mi->itemHeight = S(40);
        return TRUE;
    }
    case WM_DRAWITEM: {
        auto* d = (DRAWITEMSTRUCT*)l;
        if (d->CtlID == IDC_SIDEBAR) DrawSidebarItem(d); else DrawButton(d);
        return TRUE;
    }
    case WM_TIMER:
        if (w == TIMER_TICK) Tick();
        if (w == TIMER_SEARCH) { KillTimer(h, TIMER_SEARCH); StartSearch(); }
        return 0;
    case WM_HOTKEY:
        if (w == HOTKEY_PLAY) TogglePlay();
        if (w == HOTKEY_NEXT) Next();
        if (w == HOTKEY_PREV) Prev();
        return 0;
    case WM_APPCOMMAND:
        switch (GET_APPCOMMAND_LPARAM(l)) {
        case APPCOMMAND_MEDIA_PLAY_PAUSE: TogglePlay(); return TRUE;
        case APPCOMMAND_MEDIA_NEXTTRACK: Next(); return TRUE;
        case APPCOMMAND_MEDIA_PREVIOUSTRACK: Prev(); return TRUE;
        }
        break;
    case WM_HSCROLL: {
        HWND tb = (HWND)l;
        int code = LOWORD(w);
        if (tb == g_seek) {
            if (code == TB_THUMBTRACK) { g.dragging = true; g.pos = SendMessageW(g_seek, TBM_GETPOS, 0, 0) / 1000.0 * g.dur; InvalidateBar(); }
            else if (code == TB_ENDTRACK || code == TB_THUMBPOSITION) {
                g.dragging = false;
                SeekTo(SendMessageW(g_seek, TBM_GETPOS, 0, 0) / 1000.0 * g.dur);
            }
        } else if (tb == g_vol) {
            g.volume = SendMessageW(g_vol, TBM_GETPOS, 0, 0) / 100.0f;
            if (g_mp) g_mp->SetVolume(g.volume);
            InvalidateBar();
            if (code == TB_ENDTRACK) SaveLibrary();
        }
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(w), code = HIWORD(w);
        if (id == IDC_SEARCH && code == EN_CHANGE) {
            if (g.page != Page::Search) ShowPage(Page::Search);
            SetTimer(h, TIMER_SEARCH, 400, nullptr);
            return 0;
        }
        if (id == IDC_SIDEBAR && code == LBN_SELCHANGE) {
            int sel = (int)SendMessageW(g_sidebar, LB_GETCURSEL, 0, 0);
            if (sel == 4) { RefreshSidebar(); return 0; }
            if (sel >= 5) ShowPage(Page::Playlist, sel - 5);
            else ShowPage((Page)sel);
            InvalidateRect(g_btn[IDC_QUEUEBTN - IDC_SHUFFLE], nullptr, FALSE);
            return 0;
        }
        switch (id) {
        case IDC_PLAY: TogglePlay(); break;
        case IDC_NEXT: Next(); break;
        case IDC_PREV: Prev(); break;
        case IDC_SHUFFLE:
            g.shuffle = !g.shuffle;
            if (g.shuffle && g.qi >= 0 && g.qi + 1 < (int)g.queue.size())
                std::shuffle(g.queue.begin() + g.qi + 1, g.queue.end(), std::mt19937{std::random_device{}()});
            if (g.page == Page::Queue) RefreshView();
            SaveLibrary();
            UpdateNowPlaying();
            break;
        case IDC_REPEAT:
            g.repeat = (Repeat)(((int)g.repeat + 1) % 3);
            SaveLibrary();
            UpdateNowPlaying();
            break;
        case IDC_LIKE:
            if (const Track* t = g.current()) {
                ToggleLike(*t);
                if (g.page == Page::Liked) RefreshView();
                UpdateNowPlaying();
            }
            break;
        case IDC_QUEUEBTN:
            ShowPage(Page::Queue);
            InvalidateRect(g_btn[IDC_QUEUEBTN - IDC_SHUFFLE], nullptr, FALSE);
            break;
        case IDC_NEWPL: NewPlaylist(); break;
        case IDC_HEAD1:
            if (g.page == Page::Queue) {
                if (g.qi >= 0) g.queue.erase(g.queue.begin() + g.qi + 1, g.queue.end());
                RefreshView();
            } else {
                bool was = g.shuffle;
                g.shuffle = false;
                PlayList(g.view, 0);
                g.shuffle = was;
            }
            break;
        case IDC_HEAD2:
            if (g.page == Page::Queue) {
                g.autoplay = !g.autoplay;
                SaveLibrary();
                RefreshView();
                InvalidateRect(g_btn[IDC_HEAD2 - IDC_SHUFFLE], nullptr, FALSE);
            } else if (!g.view.empty()) {
                g.shuffle = true;
                SaveLibrary();
                PlayList(g.view, (int)(GetTickCount64() % g.view.size()));
                UpdateNowPlaying();
            }
            break;
        }
        return 0;
    }
    case WM_CONTEXTMENU: {
        POINT pt{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
        if ((HWND)w == g_list) {
            if (pt.x == -1) {   // keyboard: menu key
                int row = ListView_GetNextItem(g_list, -1, LVNI_FOCUSED);
                RECT r{};
                ListView_GetItemRect(g_list, std::max(row, 0), &r, LVIR_LABEL);
                pt = {r.left, r.bottom};
                ClientToScreen(g_list, &pt);
            }
            TrackMenu(pt);
        } else if ((HWND)w == g_sidebar) {
            POINT cp = pt;
            ScreenToClient(g_sidebar, &cp);
            DWORD hit = (DWORD)SendMessageW(g_sidebar, LB_ITEMFROMPOINT, 0, MAKELPARAM(cp.x, cp.y));
            int item = LOWORD(hit);
            if (!HIWORD(hit) && item >= 5) PlaylistMenu(item - 5, pt);
        }
        return 0;
    }
    case WM_NOTIFY: {
        auto* hdr = (NMHDR*)l;
        if (hdr->hwndFrom == g_seek || hdr->hwndFrom == g_vol) {
            if (hdr->code == NM_CUSTOMDRAW) return DrawSlider((NMCUSTOMDRAW*)l);
            return 0;
        }
        if (hdr->hwndFrom != g_list) return 0;
        switch (hdr->code) {
        case LVN_GETDISPINFOW: {
            auto* di = (NMLVDISPINFOW*)l;
            if (!(di->item.mask & LVIF_TEXT) || di->item.iItem >= (int)g.view.size()) return 0;
            const Track& t = g.view[di->item.iItem];
            const string& s = di->item.iSubItem == 0 ? t.title : di->item.iSubItem == 1 ? t.artist
                            : di->item.iSubItem == 2 ? t.album : t.duration;
            wstring ws = W(s);
            lstrcpynW(di->item.pszText, ws.c_str(), di->item.cchTextMax);
            return 0;
        }
        case NM_CUSTOMDRAW: return DrawListItem((NMLVCUSTOMDRAW*)l);
        case NM_DBLCLK: PlayRow(((NMITEMACTIVATE*)l)->iItem); return 0;
        case NM_RETURN: PlayRow(ListView_GetNextItem(g_list, -1, LVNI_FOCUSED)); return 0;
        case LVN_KEYDOWN:
            if (((NMLVKEYDOWN*)l)->wVKey == VK_DELETE && (g.page == Page::Playlist || g.page == Page::Queue)) {
                vector<int> rows = SelectedRows();
                if (g.page == Page::Playlist) {
                    auto& tracks = g.playlists[g.pagePlaylist].tracks;
                    for (auto it = rows.rbegin(); it != rows.rend(); ++it) tracks.erase(tracks.begin() + *it);
                    SaveLibrary();
                } else {
                    for (auto it = rows.rbegin(); it != rows.rend(); ++it) {
                        if (*it == g.qi) continue;
                        g.queue.erase(g.queue.begin() + *it);
                        if (*it < g.qi) --g.qi;
                    }
                }
                ListView_SetItemState(g_list, -1, 0, LVIS_SELECTED);
                RefreshView();
            }
            return 0;
        }
        return 0;
    }
    case WM_APP_SEARCH: {
        auto* r = (SearchResult*)l;
        if (r->seq == g.searchSeq) {
            g.results = std::move(r->tracks);
            if (!r->ok) SetStatus(L"Search failed — check your internet connection.", true);
            else if (g.statusIsError && g.status.rfind(L"Search failed", 0) == 0) SetStatus(L"", false);
            if (g.page == Page::Search) RefreshView();
            ListView_EnsureVisible(g_list, 0, FALSE);
        }
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
        wchar_t buf[96];
        swprintf(buf, 96, L"Playback error (0x%08lX).", (unsigned long)w);
        SetStatus(buf, true);
        UpdateNowPlaying();
        return 0;
    }
    case WM_APP_ART: {
        auto* r = (ArtResult*)l;
        if (r->id == g.artId) {
            IStream* s = SHCreateMemStream((const BYTE*)r->bytes.data(), (UINT)r->bytes.size());
            if (s) {
                auto* bmp = Gdiplus::Bitmap::FromStream(s);
                s->Release();
                if (bmp && bmp->GetLastStatus() == Gdiplus::Ok) { delete g.art; g.art = bmp; } else delete bmp;
                InvalidateBar();
            }
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
    case WM_DESTROY:
        SaveLibrary();
        UnloadItem();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

// ───────────────────────────── Startup ─────────────────────────────

static wstring KnownFolder(REFKNOWNFOLDERID id) {
    PWSTR p = nullptr;
    wstring out;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_CREATE, nullptr, &p))) out = p;
    CoTaskMemFree(p);
    return out;
}

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

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
    g_inst = inst;
    HANDLE single = CreateMutexW(nullptr, TRUE, L"Local\\MosicsSingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND other = FindWindowW(L"MosicsMain", nullptr)) {
            ShowWindow(other, SW_RESTORE);
            SetForegroundWindow(other);
        }
        return 0;
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    MFStartup(MF_VERSION, MFSTARTUP_LITE);
    Gdiplus::GdiplusStartupInput gsi;
    Gdiplus::GdiplusStartup(&g_gdiplusToken, &gsi, nullptr);
    INITCOMMONCONTROLSEX icc{sizeof icc, ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    g_appDir = exe;
    g_appDir.resize(g_appDir.rfind(L'\\'));
    g_dataDir = KnownFolder(FOLDERID_RoamingAppData) + L"\\Mosics";
    g_cacheDir = KnownFolder(FOLDERID_LocalAppData) + L"\\Mosics\\cache";
    SHCreateDirectoryExW(nullptr, g_dataDir.c_str(), nullptr);
    SHCreateDirectoryExW(nullptr, g_cacheDir.c_str(), nullptr);
    // Let yt-dlp find a deno.exe sitting next to Mosics.exe.
    wchar_t path[32767];
    DWORD n = GetEnvironmentVariableW(L"PATH", path, 32767);
    SetEnvironmentVariableW(L"PATH", (g_appDir + L";" + wstring(path, n)).c_str());

    LoadLibraryState();
    PickIconFont();

    g_http = WinHttpOpen(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0 Safari/537.36",
                         WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!g_http)
        g_http = WinHttpOpen(L"Mozilla/5.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    WinHttpSetTimeouts(g_http, 10000, 10000, 15000, 30000);

    if (FAILED(MFPCreateMediaPlayer(nullptr, FALSE, 0, &g_callback, nullptr, &g_mp))) {
        MessageBoxW(nullptr, L"Media Foundation is not available on this PC.\n"
                             L"On Windows N editions, install the Media Feature Pack.", L"Mosics", MB_ICONERROR);
        return 1;
    }

    g_brBg = CreateSolidBrush(color::bg);
    g_brSide = CreateSolidBrush(color::side);
    g_brBar = CreateSolidBrush(color::bar);
    g_brField = CreateSolidBrush(color::field);

    WNDCLASSEXW wc{sizeof wc};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = g_brBg;
    wc.lpszClassName = L"MosicsMain";
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.hIconSm = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                   GetSystemMetrics(SM_CYSMICON), 0);
    RegisterClassExW(&wc);

    UINT dpi = GetDpiForSystem();
    HWND hwnd = CreateWindowExW(0, L"MosicsMain", L"Mosics", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT,
                                CW_USEDEFAULT, MulDiv(1120, dpi, 96), MulDiv(720, dpi, 96), nullptr, nullptr, inst,
                                nullptr);
    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);
    SetFocus(g_search);

    // Housekeeping in the background: trim the audio cache, keep a bundled yt-dlp fresh.
    RunAsync(TrimCache);
    if (FindYtDlp().empty()) {
        SetStatus(L"yt-dlp not found — put yt-dlp.exe and deno.exe next to Mosics.exe.", true);
    } else if (FileExists(g_appDir + L"\\yt-dlp.exe") && NowSeconds() - g.ytdlpCheckedAt > 86400) {
        g.ytdlpCheckedAt = NowSeconds();
        SaveLibrary();
        wstring ytdlp = g_appDir + L"\\yt-dlp.exe";
        RunAsync([ytdlp] { string out; RunHidden(ytdlp, L"-U", out, 120000); });
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (msg.message == WM_KEYDOWN) {
            bool ctrl = GetKeyState(VK_CONTROL) & 0x8000;
            bool inSearch = GetFocus() == g_search;
            if (msg.wParam == VK_SPACE && !inSearch && !ctrl && GetForegroundWindow() == g_hwnd) { TogglePlay(); continue; }
            if (ctrl && msg.wParam == VK_RIGHT && !inSearch) { Next(); continue; }
            if (ctrl && msg.wParam == VK_LEFT && !inSearch) { Prev(); continue; }
            if (ctrl && msg.wParam == 'F') { SetFocus(g_search); SendMessageW(g_search, EM_SETSEL, 0, -1); continue; }
            if (ctrl && msg.wParam == 'L') { SendMessageW(g_hwnd, WM_COMMAND, IDC_LIKE, 0); continue; }
            if (ctrl && (msg.wParam == VK_UP || msg.wParam == VK_DOWN)) {
                g.volume = std::clamp(g.volume + (msg.wParam == VK_UP ? 0.05f : -0.05f), 0.0f, 1.0f);
                SendMessageW(g_vol, TBM_SETPOS, TRUE, (LPARAM)(g.volume * 100));
                if (g_mp) g_mp->SetVolume(g.volume);
                InvalidateBar();
                continue;
            }
            if (inSearch && msg.wParam == VK_RETURN) { KillTimer(g_hwnd, TIMER_SEARCH); StartSearch(); continue; }
            if (inSearch && msg.wParam == VK_DOWN) { SetFocus(g_list); continue; }
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_mp) { g_mp->Shutdown(); g_mp->Release(); }
    delete g.art;
    Gdiplus::GdiplusShutdown(g_gdiplusToken);
    MFShutdown();
    CoUninitialize();
    if (single) CloseHandle(single);
    return 0;
}
