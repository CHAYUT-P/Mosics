// State, library, player (MFPlay + yt-dlp), search and Home mixes.
// Mirrors Library.swift, Player.swift, YTMusic.swift and Suggestions.swift in the Mac app.
#pragma once
#include <mfapi.h>
#include <mfplay.h>

#include <map>
#include <random>

#include "audiofile.h"
#include "gfx.h"
#include "json.h"
#include "mixes.h"
#include "platform.h"
#include "ytmusic.h"

#ifndef MOSICS_VERSION
#define MOSICS_VERSION "dev"
#endif

enum : UINT {
    WM_APP_SEARCH = WM_APP + 1,   // lParam: SearchResult*
    WM_APP_READY,                 // lParam: PrepResult*
    WM_APP_ENDED,
    WM_APP_ART,                   // lParam: ArtResult*
    WM_APP_RADIO,                 // lParam: RadioResult*
    WM_APP_MFERROR,               // wParam: HRESULT
    WM_APP_SHELVES,               // lParam: vector<Shelf>*
    WM_APP_SETUP,                 // lParam: SetupMsg*
    WM_APP_TRAY,
    WM_APP_SHOW,                  // another launch asked us to come forward
    WM_APP_QUIT,                  // the uninstaller asks us to exit
    WM_APP_LAYOUT,                // reposition child controls after a paint
};

struct Playlist {
    enum Cover { Auto, Style, Artwork, Image };
    string id, name;
    vector<Track> tracks;
    Cover cover = Auto;
    int style = 0;
    string image;   // file name in the covers folder
};

enum class Page { Home, Search, Liked, Recent, Queue, Playlist, Mix };
enum class Repeat { Off, All, One };

struct App {
    // Library (library.json — same format as the Mac app)
    vector<Track> liked, recent;
    vector<Playlist> playlists;
    // Settings (settings.json)
    float volume = 0.8f;
    bool shuffle = false, autoplay = true, trayHintShown = false;
    Repeat repeat = Repeat::Off;
    double ytdlpCheckedAt = 0;
    int searchKind = 0;   // 0 songs, 1 videos
    string declinedInstall;   // exe path the user chose to keep running without installing

    // Player
    vector<Track> queue;
    int qi = -1;
    bool playing = false, loading = false, waitingForRadio = false;
    double pos = 0, dur = 0;
    string error;
    int loadSeq = 0;

    // Navigation
    Page page = Page::Home;
    string pageId;          // playlist id or mix id
    bool nowPlaying = false;
    string coverSheet;      // playlist whose cover is being edited

    // Search
    string query;
    vector<Track> results;
    string next;
    bool searching = false, loadingMore = false;
    string searchError;
    int searchSeq = 0;

    // Home
    vector<Shelf> shelves;
    bool shelvesLoading = false;
    double shelvesAt = 0;

    const Track* current() const { return qi >= 0 && qi < (int)queue.size() ? &queue[qi] : nullptr; }
    vector<Track> upNext() const {
        return qi >= 0 && qi + 1 < (int)queue.size() ? vector<Track>(queue.begin() + qi + 1, queue.end()) : vector<Track>{};
    }
} g;

static HWND g_hwnd;
static wstring g_appDir, g_dataDir, g_localDir, g_cacheDir, g_binDir, g_coversDir;
static IMFPMediaPlayer* g_mp;
static IMFPMediaItem* g_item;

static void Redraw() { if (g_hwnd) InvalidateRect(g_hwnd, nullptr, FALSE); }
static void ShowSetup();   // ui.h

// ───────────────────────────── Library ─────────────────────────────

static Json TracksJson(const vector<Track>& ts) {
    Json a = Json::array();
    for (auto& t : ts) a.push(t.toJson());
    return a;
}

static vector<Track> TracksFrom(const Json& j) {
    vector<Track> v;
    for (auto& t : j.items()) if (!t["id"].str().empty()) v.push_back(Track::fromJson(t));
    return v;
}

// Swift encodes `enum PlaylistCover { case style(Int), artwork, image(String) }` as {"style":{"_0":3}} etc.
static Json CoverJson(const Playlist& p) {
    switch (p.cover) {
    case Playlist::Style: return Json::object().set("style", Json::object().set("_0", p.style));
    case Playlist::Artwork: return Json::object().set("artwork", Json::object());
    case Playlist::Image: return Json::object().set("image", Json::object().set("_0", p.image));
    default: return Json();
    }
}

static void SaveLibrary() {
    Json pls = Json::array();
    for (auto& p : g.playlists) {
        Json o = Json::object().set("id", p.id).set("name", p.name).set("tracks", TracksJson(p.tracks));
        if (p.cover != Playlist::Auto) o.set("cover", CoverJson(p));
        pls.push(o);
    }
    Json j = Json::object().set("liked", TracksJson(g.liked)).set("playlists", pls).set("recent", TracksJson(g.recent));
    WriteFileAll(g_dataDir + L"\\library.json", j.dump());
}

static void SaveSettings() {
    Json j = Json::object()
                 .set("volume", (double)g.volume)
                 .set("shuffle", g.shuffle)
                 .set("autoplay", g.autoplay)
                 .set("repeat", (int)g.repeat)
                 .set("ytdlpCheckedAt", g.ytdlpCheckedAt)
                 .set("lastQuery", g.query)
                 .set("searchKind", g.searchKind)
                 .set("trayHintShown", g.trayHintShown)
                 .set("declinedInstall", g.declinedInstall);
    WriteFileAll(g_dataDir + L"\\settings.json", j.dump());
}

static void LoadState() {
    string text;
    Json lib = ReadFileAll(g_dataDir + L"\\library.json", text) ? Json::parse(text) : Json();
    g.liked = TracksFrom(lib["liked"]);
    g.recent = TracksFrom(lib["recent"]);
    bool migrated = false;
    for (auto& pj : lib["playlists"].items()) {
        Playlist p;
        p.id = pj["id"].str();
        if (p.id.empty()) { p.id = NewUuid(); migrated = true; }   // from Mosics for Windows 1.0
        p.name = pj["name"].str();
        p.tracks = TracksFrom(pj["tracks"]);
        const Json& c = pj["cover"];
        if (c["style"].isObject()) { p.cover = Playlist::Style; p.style = (int)c["style"]["_0"].num(0); }
        else if (c["artwork"].isObject()) p.cover = Playlist::Artwork;
        else if (c["image"].isObject()) { p.cover = Playlist::Image; p.image = c["image"]["_0"].str(); }
        g.playlists.push_back(p);
    }
    Json set = ReadFileAll(g_dataDir + L"\\settings.json", text) ? Json::parse(text) : lib;   // 1.0 kept these in library.json
    g.volume = std::clamp((float)set["volume"].num(0.8), 0.0f, 1.0f);
    g.shuffle = set["shuffle"].boolean(false);
    g.autoplay = set["autoplay"].boolean(true);
    g.repeat = (Repeat)std::clamp((int)set["repeat"].num(0), 0, 2);
    g.ytdlpCheckedAt = set["ytdlpCheckedAt"].num(0);
    g.query = set["lastQuery"].str();
    g.searchKind = std::clamp((int)set["searchKind"].num(0), 0, 1);
    g.trayHintShown = set["trayHintShown"].boolean(false);
    g.declinedInstall = set["declinedInstall"].str();
    if (migrated || !lib["volume"].isNull()) { SaveLibrary(); SaveSettings(); }
}

static bool IsLiked(const string& id) {
    return std::any_of(g.liked.begin(), g.liked.end(), [&](const Track& t) { return t.id == id; });
}

static void ToggleLike(const Track& t) {
    auto it = std::find_if(g.liked.begin(), g.liked.end(), [&](const Track& x) { return x.id == t.id; });
    if (it != g.liked.end()) g.liked.erase(it); else g.liked.insert(g.liked.begin(), t);
    SaveLibrary();
    Redraw();
}

static void AddRecent(const Track& t) {
    g.recent.erase(std::remove_if(g.recent.begin(), g.recent.end(), [&](const Track& x) { return x.id == t.id; }),
                   g.recent.end());
    g.recent.insert(g.recent.begin(), t);
    if (g.recent.size() > 100) g.recent.resize(100);
    SaveLibrary();
}

static Playlist* FindPlaylist(const string& id) {
    for (auto& p : g.playlists) if (p.id == id) return &p;
    return nullptr;
}

static string NewPlaylist(const string& name) {
    Playlist p;
    p.id = NewUuid();
    p.name = name.empty() ? "Playlist " + std::to_string(g.playlists.size() + 1) : name;
    g.playlists.push_back(p);
    SaveLibrary();
    return p.id;
}

static void AddToPlaylist(const Track& t, const string& id) {
    Playlist* p = FindPlaylist(id);
    if (!p || std::any_of(p->tracks.begin(), p->tracks.end(), [&](const Track& x) { return x.id == t.id; })) return;
    p->tracks.push_back(t);
    SaveLibrary();
}

static wstring CoverPath(const string& file) { return g_coversDir + L"\\" + W(file); }
static string CoverURL(const string& file) { return "file:" + U8(CoverPath(file)); }

static void SetCover(const string& id, Playlist::Cover cover, int style = 0, const string& image = "") {
    Playlist* p = FindPlaylist(id);
    if (!p) return;
    if (p->cover == Playlist::Image && !(cover == Playlist::Image && image == p->image)) {
        DeleteFileW(CoverPath(p->image).c_str());
    }
    p->cover = cover;
    p->style = style;
    p->image = image;
    SaveLibrary();
    Redraw();
}

static void DeletePlaylist(const string& id) {
    Playlist* p = FindPlaylist(id);
    if (!p) return;
    if (p->cover == Playlist::Image) DeleteFileW(CoverPath(p->image).c_str());
    g.playlists.erase(g.playlists.begin() + (p - g.playlists.data()));
    SaveLibrary();
}

static int PlaylistStyle(const Playlist& p) {
    return p.cover == Playlist::Style ? ((p.style % kSleeveCount) + kSleeveCount) % kSleeveCount : SleeveForName(p.name);
}

// The picture used for the record label: the chosen image, or the first song's cover.
static string PlaylistLabelArt(const Playlist& p) {
    if (p.cover == Playlist::Image) return CoverURL(p.image);
    if (p.cover == Playlist::Artwork && !p.tracks.empty()) return p.tracks[0].thumbAt(226);
    return "";
}

// ───────────────────────────── yt-dlp & the audio cache ─────────────────────────────

static wstring FindTool(const wchar_t* exe) {
    for (const wstring& dir : {g_binDir, g_appDir}) {
        wstring p = dir + L"\\" + exe;
        if (FileExists(p)) return p;
    }
    wchar_t buf[MAX_PATH];
    if (SearchPathW(nullptr, exe, nullptr, MAX_PATH, buf, nullptr)) return buf;
    wstring winget = KnownFolder(FOLDERID_LocalAppData) + L"\\Microsoft\\WinGet\\Links\\" + exe;
    if (FileExists(winget)) return winget;
    return {};
}

static bool HelpersReady() { return !FindTool(L"yt-dlp.exe").empty() && !FindTool(L"deno.exe").empty(); }

static wstring CachedAudio(const string& id) {
    for (const wchar_t* ext : {L".aac", L".m4a", L".ts", L".webm"}) {
        wstring p = g_cacheDir + L"\\" + W(id) + ext;
        if (FileExists(p)) return p;
    }
    return {};
}

// Downloads (once) the audio for `id` into the cache. Blocking — call from a worker thread.
static bool PrepareAudio(const string& id, wstring& path, string& err) {
    // One download per song even if "play" and "prefetch" race for it.
    HANDLE lock = CreateMutexW(nullptr, FALSE, (L"Local\\Mosics-" + W(id)).c_str());
    if (lock) WaitForSingleObject(lock, INFINITE);
    struct Unlock { HANDLE h; ~Unlock() { if (h) { ReleaseMutex(h); CloseHandle(h); } } } unlock{lock};

    path = CachedAudio(id);
    if (!path.empty()) { Touch(path); return true; }

    wstring exe = FindTool(L"yt-dlp.exe");
    if (exe.empty()) { err = "setup"; return false; }
    wstring tmp = g_cacheDir + L"\\" + W(id) + L".download";
    DeleteFileW(tmp.c_str());
    wstring outTemplate = tmp;   // yt-dlp treats % as a template character
    for (size_t p = 0; (p = outTemplate.find(L'%', p)) != wstring::npos; p += 2) outTemplate.insert(p, L"%");
    wstring args = L"-f \"234/233/140/bestaudio[ext=m4a]/bestaudio\" -N 4 --no-part --no-playlist --no-warnings "
                   L"--no-progress --no-mtime --fixup never -o \"" + outTemplate + L"\" -- " + W(id);
    string output;
    int code = RunHidden(exe, args, output, 180000);
    string data;
    if (code != 0 || !ReadFileAll(tmp, data) || data.empty()) {
        DeleteFileW(tmp.c_str());
        size_t e = output.rfind("ERROR:");
        err = e == string::npos ? "Couldn't load this song." : output.substr(e + 7);
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

// ───────────────────────────── YouTube Music ─────────────────────────────

static bool YTMusicPost(const char* endpoint, const string& body, string& out, const string& query = "") {
    wstring url = L"https://music.youtube.com/youtubei/v1/" + W(endpoint) + L"?prettyPrint=false" + W(query);
    return HttpRequest(url, L"POST", body, L"Content-Type: application/json\r\nOrigin: https://music.youtube.com\r\n", out);
}

static bool SearchSync(const string& q, ytmusic::Kind kind, ytmusic::Page& page) {
    string body;
    if (!YTMusicPost("search", ytmusic::searchBody(q, kind), body)) return false;
    page = ytmusic::parseSearch(body);
    return true;
}

static vector<Track> RadioSync(const string& id) {
    string body;
    return YTMusicPost("next", ytmusic::radioBody(id), body) ? ytmusic::parseRadio(body) : vector<Track>{};
}

// ───────────────────────────── Player ─────────────────────────────

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
            Redraw();
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

struct PrepResult { int seq; string id; wstring path; string err; };
struct RadioResult { string seed; vector<Track> tracks; bool thenPlay; };

static void UpdateTitle();   // main.cpp

static void FetchRadio(const string& seed) {
    RunAsync([seed] { PostMessageW(g_hwnd, WM_APP_RADIO, 0, (LPARAM) new RadioResult{seed, RadioSync(seed), false}); });
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
    g.dur = t.seconds();
    g.playing = true;
    g.loading = true;
    g.error.clear();
    AddRecent(t);
    UpdateTitle();
    Redraw();
    RunAsync([seq, id = t.id] {
        auto* r = new PrepResult{seq, id, {}, {}};
        PrepareAudio(id, r->path, r->err);
        PostMessageW(g_hwnd, WM_APP_READY, 0, (LPARAM)r);
    });
    if (g.autoplay && g.repeat == Repeat::Off && i + 1 >= (int)g.queue.size()) FetchRadio(t.id);
}

static void OnAudioReady(PrepResult* r) {
    if (r->seq != g.loadSeq) return;   // the user already moved on
    if (!r->err.empty()) {
        g.loading = false;
        g.playing = false;
        if (r->err == "setup") {
            g.error = "Mosics needs a one-time setup to play music.";
            ShowSetup();
        } else {
            g.error = r->err;
        }
        Redraw();
        return;
    }
    IMFPMediaItem* item = nullptr;
    HRESULT hr = g_mp->CreateMediaItemFromURL(r->path.c_str(), TRUE, 0, &item);
    if (SUCCEEDED(hr)) hr = g_mp->SetMediaItem(item);
    if (FAILED(hr)) {
        if (item) item->Release();
        DeleteFileW(r->path.c_str());   // a corrupt cache entry: the next attempt downloads again
        g.loading = false;
        g.playing = false;
        g.error = "Couldn't play this file. Try again.";
        Redraw();
        return;
    }
    g_item = item;
    PrefetchNext();
}

static void PlayTracks(const vector<Track>& tracks, int start = 0) {
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
}

static void PlayNext(const Track& t) {
    if (g.qi < 0) return PlayTracks({t});
    g.queue.insert(g.queue.begin() + g.qi + 1, t);
    Redraw();
}

static void AddToQueue(const Track& t) {
    if (g.qi < 0) return PlayTracks({t});
    g.queue.push_back(t);
    Redraw();
}

static void StartRadio(const Track& t) {
    PlayTracks({t});
    FetchRadio(t.id);
}

static void JumpUpNext(int offset) { if (g.qi >= 0) LoadIndex(g.qi + 1 + offset); }

static void RemoveUpNext(int offset) {
    int i = g.qi + 1 + offset;
    if (g.qi >= 0 && i < (int)g.queue.size()) g.queue.erase(g.queue.begin() + i);
    Redraw();
}

static void ClearUpNext() {
    if (g.qi >= 0) g.queue.erase(g.queue.begin() + g.qi + 1, g.queue.end());
    Redraw();
}

static void TogglePlay() {
    if (!g.current()) return;
    g.playing = !g.playing;
    if (g_item) { if (g.playing) g_mp->Play(); else g_mp->Pause(); }
    Redraw();
}

static void Next() {
    if (g.qi < 0) return;
    if (g.qi + 1 < (int)g.queue.size()) LoadIndex(g.qi + 1);
    else if (g.repeat == Repeat::All && !g.queue.empty()) LoadIndex(0);
    else if (g.autoplay) {
        g.waitingForRadio = true;
        g.loading = true;
        Redraw();
        FetchRadio(g.queue[g.qi].id);
    }
}

static void SeekTo(double seconds) {
    g.pos = seconds;
    if (g_item) {
        PROPVARIANT pv;
        PropVariantInit(&pv);
        pv.vt = VT_I8;
        pv.hVal.QuadPart = (LONGLONG)(seconds * 1e7);
        g_mp->SetPosition(MFP_POSITIONTYPE_100NS, &pv);
    }
    Redraw();
}

static void Previous() {
    if (g.qi < 0) return;
    if (g.pos > 3 || g.qi == 0) SeekTo(0); else LoadIndex(g.qi - 1);
}

static void ToggleShuffle() {
    g.shuffle = !g.shuffle;
    if (g.shuffle && g.qi >= 0 && g.qi + 1 < (int)g.queue.size())
        std::shuffle(g.queue.begin() + g.qi + 1, g.queue.end(), std::mt19937{std::random_device{}()});
    SaveSettings();
    Redraw();
}

static void CycleRepeat() {
    g.repeat = (Repeat)(((int)g.repeat + 1) % 3);
    SaveSettings();
    Redraw();
}

static void SetVolume(float v) {
    g.volume = std::clamp(v, 0.0f, 1.0f);
    if (g_mp) g_mp->SetVolume(g.volume);
    Redraw();
}

static void OnTrackEnded() {
    if (g.repeat == Repeat::One) { SeekTo(0); g_mp->Play(); return; }
    if (g.qi + 1 < (int)g.queue.size() || g.repeat == Repeat::All || g.autoplay) {
        Next();
    } else {
        g.playing = false;
        SeekTo(0);
        g_mp->Pause();
        Redraw();
    }
}

static void OnRadio(RadioResult* r) {
    if (!g.current()) return;
    int added = 0;
    for (auto& t : r->tracks) {
        if (added >= 25) break;
        if (std::any_of(g.queue.begin(), g.queue.end(), [&](const Track& q) { return q.id == t.id; })) continue;
        g.queue.push_back(t);
        ++added;
    }
    // If playback already ran off the end while we were fetching, carry on now.
    if (g.waitingForRadio) {
        g.waitingForRadio = false;
        g.loading = false;
        if (added) LoadIndex(g.qi + 1);
    } else {
        PrefetchNext();
    }
    Redraw();
}

// Polls the playback clock (MFPlay has no time events).
static void ReadClock() {
    if (!g_item) return;
    PROPVARIANT pv;
    PropVariantInit(&pv);
    if (SUCCEEDED(g_mp->GetPosition(MFP_POSITIONTYPE_100NS, &pv)) && pv.vt == VT_I8) g.pos = pv.hVal.QuadPart / 1e7;
    PropVariantClear(&pv);
    PropVariantInit(&pv);
    if (SUCCEEDED(g_mp->GetDuration(MFP_POSITIONTYPE_100NS, &pv)) && (pv.vt == VT_UI8 || pv.vt == VT_I8) && pv.uhVal.QuadPart)
        g.dur = pv.uhVal.QuadPart / 1e7;
    PropVariantClear(&pv);
}

// ───────────────────────────── Search ─────────────────────────────

struct SearchResult { int seq; bool more; bool ok; ytmusic::Page page; };

static void StartSearch() {
    string q = Trim(g.query);
    int seq = ++g.searchSeq;
    g.loadingMore = false;
    if (q.empty()) {
        g.results.clear();
        g.next.clear();
        g.searching = false;
        g.searchError.clear();
        Redraw();
        return;
    }
    g.searching = true;
    Redraw();
    auto kind = g.searchKind ? ytmusic::Kind::Videos : ytmusic::Kind::Songs;
    RunAsync([seq, q, kind] {
        auto* r = new SearchResult{seq, false, false, {}};
        r->ok = SearchSync(q, kind, r->page);
        PostMessageW(g_hwnd, WM_APP_SEARCH, 0, (LPARAM)r);
    });
}

static void LoadMoreResults() {
    if (g.next.empty() || g.loadingMore || g.searching || g.results.size() >= 500) return;
    g.loadingMore = true;
    int seq = g.searchSeq;
    string token = g.next;
    RunAsync([seq, token] {
        auto* r = new SearchResult{seq, true, false, {}};
        string body;
        r->ok = YTMusicPost("search", ytmusic::moreBody(), body, ytmusic::moreQuery(token));
        if (r->ok) r->page = ytmusic::parseMore(body);
        PostMessageW(g_hwnd, WM_APP_SEARCH, 0, (LPARAM)r);
    });
}

static void OnSearch(SearchResult* r) {
    if (r->seq != g.searchSeq) return;
    if (r->more) {
        g.loadingMore = false;
        for (auto& t : r->page.tracks)
            if (std::none_of(g.results.begin(), g.results.end(), [&](const Track& x) { return x.id == t.id; }))
                g.results.push_back(t);
        g.next = r->ok ? r->page.next : "";
    } else {
        g.searching = false;
        g.results = std::move(r->page.tracks);
        g.next = r->page.next;
        g.searchError = r->ok ? "" : "Search failed \xE2\x80\x94 check your internet connection.";
    }
    Redraw();
}

// ───────────────────────────── Home mixes ─────────────────────────────

// Genre mixes come from curated artist lists rather than YouTube's radio, because radio drifts
// towards global pop within a few songs. Each refresh samples different artists, so it stays fresh.
static bool BuildMix(const Mix& mix, Shelf& out) {
    auto rng = std::mt19937{std::random_device{}()};
    vector<vector<string>> picked = mix.artists;
    std::shuffle(picked.begin(), picked.end(), rng);
    if (picked.size() > 12) picked.resize(12);
    vector<vector<Track>> perArtist(picked.size());
    ParallelFor((int)picked.size(), 4, [&](int i) {
        ytmusic::Page page;
        if (!SearchSync(picked[i][0], ytmusic::Kind::Songs, page)) return;
        vector<Track> own, originals;
        for (auto& t : page.tracks)   // only songs really by this artist (search also returns covers and namesakes)
            if (std::any_of(picked[i].begin(), picked[i].end(), [&](const string& n) { return ArtistMatches(t.artist, n); }))
                own.push_back(t);
        for (auto& t : own) if (!IsVariant(t.title)) originals.push_back(t);   // prefer original recordings
        vector<Track> pool = originals.size() >= 2 ? originals : own;
        if (pool.size() > 6) pool.resize(6);
        std::shuffle(pool.begin(), pool.end(), std::mt19937{std::random_device{}()});
        if (pool.size() > 2) pool.resize(2);
        perArtist[i] = pool;
    });
    vector<string> seen;
    out = {mix.id, mix.title, mix.subtitle, {}};
    for (int round = 0; round < 2; ++round)   // round-robin, so the same artist never plays twice in a row
        for (auto& list : perArtist)
            if (round < (int)list.size() && std::find(seen.begin(), seen.end(), list[round].id) == seen.end()) {
                seen.push_back(list[round].id);
                out.tracks.push_back(list[round]);
            }
    return out.tracks.size() >= 4;
}

// "Made for You" and "Because You Like …", from what you like and play.
static vector<Shelf> PersonalShelves(const vector<Track>& liked, const vector<Track>& recent) {
    vector<Shelf> shelves;
    std::map<string, int> score;   // a like counts three times as much as a play
    for (auto& t : liked) score[MainArtist(t)] += 3;
    for (size_t i = 0; i < recent.size() && i < 50; ++i) score[MainArtist(recent[i])] += 1;
    vector<string> known;
    for (auto& t : liked) known.push_back(t.id);
    for (auto& t : recent) known.push_back(t.id);

    vector<Track> seeds;
    auto addSeed = [&](const Track& t) {
        if (seeds.size() < 3 && std::none_of(seeds.begin(), seeds.end(), [&](const Track& s) { return s.id == t.id; }))
            seeds.push_back(t);
    };
    for (size_t i = 0; i < liked.size() && i < 2; ++i) addSeed(liked[i]);
    for (size_t i = 0; i < recent.size() && i < 3; ++i) addSeed(recent[i]);
    if (!seeds.empty()) {
        vector<vector<Track>> radios(seeds.size());
        ParallelFor((int)seeds.size(), 3, [&](int i) {
            radios[i] = RadioSync(seeds[i].id);
            if (!radios[i].empty()) radios[i].erase(radios[i].begin());
        });
        vector<string> seen = known;
        Shelf mix{"made-for-you", "Made for You", "New finds, painted from what you play", {}};
        for (int i = 0; i < 30; ++i)
            for (auto& r : radios)
                if (i < (int)r.size() && std::find(seen.begin(), seen.end(), r[i].id) == seen.end()) {
                    seen.push_back(r[i].id);
                    mix.tracks.push_back(r[i]);
                }
        if (mix.tracks.size() >= 4) {
            if (mix.tracks.size() > 24) mix.tracks.resize(24);
            shelves.push_back(mix);
        }
    }

    string top;
    int best = 0;
    for (auto& [artist, s] : score) if (s > best && !artist.empty()) { best = s; top = artist; }
    if (!top.empty()) {
        const Track* seed = nullptr;
        for (auto& t : liked) if (!seed && MainArtist(t) == top) seed = &t;
        for (auto& t : recent) if (!seed && MainArtist(t) == top) seed = &t;
        vector<Track> radio = seed ? RadioSync(seed->id) : vector<Track>{};
        Shelf because{"because", "Because You Like " + top, "Kindred spirits", {}};
        int sameArtist = 0;
        for (size_t i = 1; i < radio.size(); ++i) {
            if (MainArtist(radio[i]) == top && ++sameArtist > 3) continue;
            because.tracks.push_back(radio[i]);
        }
        if (because.tracks.size() >= 4) {
            if (because.tracks.size() > 24) because.tracks.resize(24);
            shelves.push_back(because);
        }
    }
    return shelves;
}

static void SaveShelves() {
    Json arr = Json::array();
    for (auto& s : g.shelves)
        arr.push(Json::object().set("id", s.id).set("title", s.title).set("subtitle", s.subtitle).set("tracks", TracksJson(s.tracks)));
    WriteFileAll(g_dataDir + L"\\home.json", Json::object().set("at", g.shelvesAt).set("shelves", arr).dump());
}

static void LoadShelves() {
    string text;
    if (!ReadFileAll(g_dataDir + L"\\home.json", text)) return;
    Json j = Json::parse(text);
    g.shelvesAt = j["at"].num(0);
    for (auto& s : j["shelves"].items())
        g.shelves.push_back({s["id"].str(), s["title"].str(), s["subtitle"].str(), TracksFrom(s["tracks"])});
}

// Refreshes if forced, empty, or older than 12 hours.
static void RefreshShelves(bool force) {
    if (g.shelvesLoading) return;
    if (!force && !g.shelves.empty() && NowSeconds() - g.shelvesAt < 12 * 3600) return;
    g.shelvesLoading = true;
    Redraw();
    vector<Track> liked = g.liked, recent = g.recent;
    RunAsync([liked, recent] {
        auto* out = new vector<Shelf>(PersonalShelves(liked, recent));
        for (auto& mix : Mixes()) {   // one mix at a time, a few searches at a time
            Shelf s;
            if (BuildMix(mix, s)) out->push_back(std::move(s));
        }
        PostMessageW(g_hwnd, WM_APP_SHELVES, 0, (LPARAM)out);
    });
}

static void OnShelves(vector<Shelf>* s) {
    g.shelvesLoading = false;
    if (!s->empty()) {   // offline: keep what we had
        g.shelves = std::move(*s);
        g.shelvesAt = NowSeconds();
        SaveShelves();
    }
    Redraw();
}

static const Shelf* FindShelf(const string& id) {
    for (auto& s : g.shelves) if (s.id == id) return &s;
    return nullptr;
}
