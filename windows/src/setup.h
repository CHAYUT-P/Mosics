// One-click setup: installs Mosics for this user (Start menu, desktop, "Apps & features" entry — no admin)
// and downloads the two free helpers it plays music through: yt-dlp (fetches the audio) and Deno
// (the JavaScript runtime yt-dlp needs for YouTube). Also the matching uninstaller (`Mosics.exe --uninstall`).
#pragma once
#include <shobjidl.h>

#include "app.h"

struct SetupState {
    bool visible = false;
    int phase = 0;              // 0 intro, 1 working, 2 done, 3 failed
    bool need[3] = {};          // install, yt-dlp, deno
    int state[3] = {};          // 0 waiting, 1 working, 2 done, 3 failed
    double frac[3] = {};
    string error;
} g_setup;

struct SetupMsg { int step, state; double frac; string error; bool finished; };

static wstring SelfPath() {
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    return exe;
}
static wstring InstallDir() { return KnownFolder(FOLDERID_LocalAppData) + L"\\Programs\\Mosics"; }
static wstring InstalledExe() { return InstallDir() + L"\\Mosics.exe"; }
static wstring StartMenuLink() { return KnownFolder(FOLDERID_Programs) + L"\\Mosics.lnk"; }
static wstring DesktopLink() { return KnownFolder(FOLDERID_Desktop) + L"\\Mosics.lnk"; }
static const wchar_t kUninstallKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Mosics";

static bool IsInstalledCopy() { return lstrcmpiW(g_appDir.c_str(), InstallDir().c_str()) == 0; }

// Shown on first launch, after a download of a new version, or when a helper is missing.
static bool SetupNeeded() {
    return !HelpersReady() || (!IsInstalledCopy() && !FileExists(g_appDir + L"\\portable.txt") &&
                               g.declinedInstall != U8(SelfPath()));
}

static bool MakeShortcut(const wstring& link, const wstring& target) {
    IShellLinkW* sl = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (void**)&sl))) return false;
    wstring dir = target.substr(0, target.rfind(L'\\'));
    sl->SetPath(target.c_str());
    sl->SetWorkingDirectory(dir.c_str());
    sl->SetDescription(L"Mosics \x2014 a tiny audio-only music player");
    sl->SetIconLocation(target.c_str(), 0);
    IPersistFile* pf = nullptr;
    bool ok = SUCCEEDED(sl->QueryInterface(IID_IPersistFile, (void**)&pf)) && SUCCEEDED(pf->Save(link.c_str(), TRUE));
    if (pf) pf->Release();
    sl->Release();
    return ok;
}

static void RegString(HKEY k, const wchar_t* name, const wstring& v) {
    RegSetValueExW(k, name, 0, REG_SZ, (const BYTE*)v.c_str(), (DWORD)((v.size() + 1) * sizeof(wchar_t)));
}
static void RegDword(HKEY k, const wchar_t* name, DWORD v) { RegSetValueExW(k, name, 0, REG_DWORD, (const BYTE*)&v, sizeof v); }

static bool InstallSelf(string& err) {
    wstring dir = InstallDir(), dest = InstalledExe();
    SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    if (!IsInstalledCopy()) {
        if (!CopyFileW(SelfPath().c_str(), dest.c_str(), FALSE)) {
            err = "Couldn't copy Mosics into " + U8(dir) + ". Is another copy of Mosics still open?";
            return false;
        }
        DeleteFileW((dest + L":Zone.Identifier").c_str());   // it's installed now; don't warn on every launch
    }
    MakeShortcut(StartMenuLink(), dest);
    MakeShortcut(DesktopLink(), dest);
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kUninstallKey, 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) == ERROR_SUCCESS) {
        RegString(k, L"DisplayName", L"Mosics");
        RegString(k, L"DisplayVersion", W(MOSICS_VERSION));
        RegString(k, L"Publisher", L"Mosics");
        RegString(k, L"DisplayIcon", dest);
        RegString(k, L"InstallLocation", dir);
        RegString(k, L"UninstallString", L"\"" + dest + L"\" --uninstall");
        RegDword(k, L"NoModify", 1);
        RegDword(k, L"NoRepair", 1);
        RegDword(k, L"EstimatedSize", 150 * 1024);   // KB, including yt-dlp and Deno
        RegCloseKey(k);
    }
    return true;
}

static void Post(SetupMsg m) { PostMessageW(g_hwnd, WM_APP_SETUP, 0, (LPARAM) new SetupMsg(std::move(m))); }

static bool Download(int step, const wchar_t* url, const wstring& dest) {
    int lastPct = -1;
    return HttpDownload(url, dest, [&](ULONGLONG got, ULONGLONG total) {
        int pct = total ? (int)(got * 100 / total) : 0;
        if (pct != lastPct) { lastPct = pct; Post({step, 1, total ? (double)got / total : 0, "", false}); }
        return true;
    });
}

static void RunSetup(bool install, bool ytdlp, bool deno) {
    RunAsync([install, ytdlp, deno] {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);   // for the shortcuts
        auto fail = [](int step, string why) { Post({step, 3, 0, std::move(why), true}); };
        SHCreateDirectoryExW(nullptr, g_binDir.c_str(), nullptr);

        if (install) {
            Post({0, 1, 0, "", false});
            string err;
            if (!InstallSelf(err)) { fail(0, err); CoUninitialize(); return; }
            Post({0, 2, 1, "", false});
        }
        if (ytdlp) {
            Post({1, 1, 0, "", false});
            if (!Download(1, L"https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp.exe", g_binDir + L"\\yt-dlp.exe")) {
                fail(1, "Couldn't download yt-dlp. Check your internet connection and try again.");
                CoUninitialize();
                return;
            }
            Post({1, 2, 1, "", false});
        }
        if (deno) {
            Post({2, 1, 0, "", false});
            wstring zip = g_binDir + L"\\deno.zip";
            if (!Download(2, L"https://github.com/denoland/deno/releases/latest/download/deno-x86_64-pc-windows-msvc.zip", zip)) {
                fail(2, "Couldn't download Deno. Check your internet connection and try again.");
                CoUninitialize();
                return;
            }
            // Windows 10 1803+ ships tar.exe, which reads zip files; PowerShell is the fallback.
            string out;
            wchar_t sys[MAX_PATH];
            GetSystemDirectoryW(sys, MAX_PATH);
            RunHidden(wstring(sys) + L"\\tar.exe", L"-xf \"" + zip + L"\" -C \"" + g_binDir + L"\"", out, 180000);
            if (!FileExists(g_binDir + L"\\deno.exe"))
                RunHidden(wstring(sys) + L"\\WindowsPowerShell\\v1.0\\powershell.exe",
                          L"-NoProfile -NonInteractive -Command \"Expand-Archive -LiteralPath '" + zip +
                              L"' -DestinationPath '" + g_binDir + L"' -Force\"",
                          out, 180000);
            DeleteFileW(zip.c_str());
            if (!FileExists(g_binDir + L"\\deno.exe")) {
                fail(2, "Couldn't unpack Deno.");
                CoUninitialize();
                return;
            }
            Post({2, 2, 1, "", false});
        }
        CoUninitialize();
        Post({-1, 2, 1, "", true});
    });
}

static void StartSetup() {
    g_setup.need[0] = !IsInstalledCopy() && !FileExists(g_appDir + L"\\portable.txt");
    g_setup.need[1] = FindTool(L"yt-dlp.exe").empty();
    g_setup.need[2] = FindTool(L"deno.exe").empty();
    for (int i = 0; i < 3; ++i) { g_setup.state[i] = g_setup.need[i] ? 0 : 2; g_setup.frac[i] = 0; }
    g_setup.phase = 1;
    g_setup.error.clear();
    RunSetup(g_setup.need[0], g_setup.need[1], g_setup.need[2]);
    Redraw();
}

static void OnSetupMsg(SetupMsg* m) {
    if (m->step >= 0) {
        g_setup.state[m->step] = m->state;
        g_setup.frac[m->step] = m->frac;
    }
    if (m->finished) {
        g_setup.phase = m->state == 3 ? 3 : 2;
        g_setup.error = m->error;
    }
    Redraw();
}

// ───────────────────────────── Uninstall ─────────────────────────────

static void DeleteTree(const wstring& dir) {
    wstring from = dir + L'\0';   // double-null terminated
    SHFILEOPSTRUCTW op{};
    op.wFunc = FO_DELETE;
    op.pFrom = from.c_str();
    op.fFlags = FOF_NO_UI;
    SHFileOperationW(&op);
}

static int Uninstall() {
    if (MessageBoxW(nullptr, L"Remove Mosics from this PC?", L"Uninstall Mosics", MB_YESNO | MB_ICONQUESTION) != IDYES)
        return 0;
    bool wipe = MessageBoxW(nullptr,
                            L"Also delete your liked songs, playlists and history?\n\n"
                            L"Choose No to keep them in case you install Mosics again.",
                            L"Uninstall Mosics", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES;
    if (HWND running = FindWindowW(L"MosicsMain", nullptr)) {
        DWORD_PTR r;
        SendMessageTimeoutW(running, WM_APP_QUIT, 0, 0, SMTO_ABORTIFHUNG, 5000, &r);
        Sleep(500);
    }
    DeleteFileW(StartMenuLink().c_str());
    DeleteFileW(DesktopLink().c_str());
    RegDeleteKeyW(HKEY_CURRENT_USER, kUninstallKey);
    DeleteTree(KnownFolder(FOLDERID_LocalAppData) + L"\\Mosics");   // helpers, song cache, artwork cache
    if (wipe) DeleteTree(KnownFolder(FOLDERID_RoamingAppData) + L"\\Mosics");
    if (IsInstalledCopy()) {
        // A running program can't delete itself: let cmd remove the folder once we've exited.
        wstring cmd = L"cmd.exe /c ping 127.0.0.1 -n 3 > nul & rmdir /s /q \"" + InstallDir() + L"\"";
        STARTUPINFOW si{sizeof si};
        PROCESS_INFORMATION pi{};
        if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
    } else {
        DeleteTree(InstallDir());
    }
    MessageBoxW(nullptr, L"Mosics has been removed.", L"Uninstall Mosics", MB_OK | MB_ICONINFORMATION);
    return 0;
}
