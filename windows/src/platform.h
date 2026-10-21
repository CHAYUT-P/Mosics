// Win32 helpers: strings, files, HTTP (WinHTTP), hidden child processes and worker threads.
#pragma once
#include <windows.h>
#include <shlobj.h>
#include <winhttp.h>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#ifndef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
#define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY 4
#endif
#ifndef WINHTTP_OPTION_DECOMPRESSION
#define WINHTTP_OPTION_DECOMPRESSION 118
#define WINHTTP_DECOMPRESSION_FLAG_ALL 3
#endif

using std::string;
using std::vector;
using std::wstring;

// ───────────────────────────── Strings ─────────────────────────────

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

static string Trim(string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    return s.substr(i);
}

static uint64_t Fnv64(const string& s) {
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; }
    return h;
}

static wstring FormatTime(double t) {
    if (!(t >= 0)) t = 0;
    int s = (int)t;
    wchar_t buf[32];
    if (s >= 3600) swprintf(buf, 32, L"%d:%02d:%02d", s / 3600, s / 60 % 60, s % 60);
    else swprintf(buf, 32, L"%d:%02d", s / 60, s % 60);
    return buf;
}

static wstring FormatLength(double seconds) {
    int m = (int)seconds / 60;
    return m >= 60 ? std::to_wstring(m / 60) + L" hr " + std::to_wstring(m % 60) + L" min" : std::to_wstring(m) + L" min";
}

static double NowSeconds() {   // Unix time
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    return (double)(((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime) / 1e7 - 11644473600.0;
}

static string NewUuid() {   // uppercase, like Swift's UUID().uuidString
    GUID g;
    CoCreateGuid(&g);
    char buf[40];
    snprintf(buf, sizeof buf, "%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X", (unsigned long)g.Data1, g.Data2,
             g.Data3, g.Data4[0], g.Data4[1], g.Data4[2], g.Data4[3], g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]);
    return buf;
}

// ───────────────────────────── Files ─────────────────────────────

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

static void Touch(const wstring& path) {
    HANDLE f = CreateFileW(path.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    SetFileTime(f, nullptr, nullptr, &now);
    CloseHandle(f);
}

// Deletes the least recently used files in `dir` until it's under `limit` bytes.
static void TrimFolder(const wstring& dir, ULONGLONG limit) {
    struct F { wstring path; ULONGLONG time, size; };
    vector<F> files;
    ULONGLONG total = 0;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        ULONGLONG size = ((ULONGLONG)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
        ULONGLONG t = ((ULONGLONG)fd.ftLastWriteTime.dwHighDateTime << 32) | fd.ftLastWriteTime.dwLowDateTime;
        files.push_back({dir + L"\\" + fd.cFileName, t, size});
        total += size;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(files.begin(), files.end(), [](const F& a, const F& b) { return a.time < b.time; });
    for (auto& f : files) {
        if (total <= limit) break;
        if (DeleteFileW(f.path.c_str())) total -= f.size;
    }
}

static wstring KnownFolder(REFKNOWNFOLDERID id) {
    PWSTR p = nullptr;
    wstring out;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_CREATE, nullptr, &p))) out = p;
    CoTaskMemFree(p);
    return out;
}

// ───────────────────────────── Threads ─────────────────────────────

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

// Runs fn(0..n-1) on at most `limit` worker threads and waits for all of them.
static void ParallelFor(int n, int limit, const std::function<void(int)>& fn) {
    struct Ctx { const std::function<void(int)>* fn; LONG next; LONG n; } c{&fn, -1, n};
    vector<HANDLE> threads;
    for (int i = 0; i < std::min(n, limit); ++i) {
        HANDLE t = CreateThread(nullptr, 0, [](LPVOID p) -> DWORD {
            auto* c = static_cast<Ctx*>(p);
            for (LONG i; (i = InterlockedIncrement(&c->next)) < c->n;) (*c->fn)((int)i);
            return 0;
        }, &c, 0, nullptr);
        if (t) threads.push_back(t);
    }
    if (threads.empty()) for (int i = 0; i < n; ++i) fn(i);
    for (size_t i = 0; i < threads.size(); i += MAXIMUM_WAIT_OBJECTS)
        WaitForMultipleObjects((DWORD)std::min<size_t>(MAXIMUM_WAIT_OBJECTS, threads.size() - i), threads.data() + i, TRUE, INFINITE);
    for (HANDLE t : threads) CloseHandle(t);
}

// ───────────────────────────── HTTP ─────────────────────────────

static HINTERNET g_http;

static void HttpInit() {
    g_http = WinHttpOpen(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0 Safari/537.36",
                         WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!g_http)   // before Windows 8.1
        g_http = WinHttpOpen(L"Mozilla/5.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    WinHttpSetTimeouts(g_http, 10000, 10000, 15000, 30000);
}

// Sends a request; `sink` receives the body in chunks (return false to cancel). `total` is Content-Length (0 if unknown).
static bool HttpStream(const wstring& url, const wchar_t* method, const string& body, const wchar_t* headers,
                       const std::function<bool(const char*, DWORD, ULONGLONG total)>& sink) {
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof uc;
    wchar_t host[256], path[4096], extra[8192];
    uc.lpszHostName = host; uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path; uc.dwUrlPathLength = 4096;
    uc.lpszExtraInfo = extra; uc.dwExtraInfoLength = 8192;
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
            wchar_t lenText[32] = L"";
            DWORD lenSize = sizeof lenText;
            ULONGLONG total = 0;
            if (WinHttpQueryHeaders(r, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, lenText, &lenSize,
                                    WINHTTP_NO_HEADER_INDEX))
                total = _wcstoui64(lenText, nullptr, 10);
            ok = status >= 200 && status < 300;
            vector<char> chunk(64 * 1024);
            DWORD got = 0;
            while (ok && WinHttpReadData(r, chunk.data(), (DWORD)chunk.size(), &got) && got)
                if (!sink(chunk.data(), got, total)) ok = false;
        }
        WinHttpCloseHandle(r);
    }
    if (c) WinHttpCloseHandle(c);
    return ok;
}

static bool HttpRequest(const wstring& url, const wchar_t* method, const string& body, const wchar_t* headers,
                        string& out) {
    out.clear();
    return HttpStream(url, method, body, headers, [&](const char* p, DWORD n, ULONGLONG) { out.append(p, n); return true; });
}

static bool HttpGet(const string& url, string& out) { return HttpRequest(W(url), L"GET", "", nullptr, out); }

// Downloads to a file (via a temp name), reporting progress. Returns false on failure or cancel.
static bool HttpDownload(const wstring& url, const wstring& dest,
                         const std::function<bool(ULONGLONG got, ULONGLONG total)>& progress) {
    wstring tmp = dest + L".part";
    HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    ULONGLONG got = 0;
    bool ok = HttpStream(url, L"GET", "", nullptr, [&](const char* p, DWORD n, ULONGLONG total) {
        DWORD put = 0;
        if (!WriteFile(f, p, n, &put, nullptr) || put != n) return false;
        got += n;
        return progress(got, total);
    });
    CloseHandle(f);
    if (!ok || got == 0 || !MoveFileExW(tmp.c_str(), dest.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    return true;
}

// ───────────────────────────── Processes ─────────────────────────────

// Runs a console program hidden and captures stdout+stderr. Returns its exit code (-1 on failure/timeout).
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
