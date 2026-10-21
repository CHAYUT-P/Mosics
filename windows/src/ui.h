// The window's contents, drawn by hand to match the Mac app: sidebar, pages, player bar, Now Playing,
// the cover picker and setup. Each paint also records clickable areas ("hits") for the mouse.
// Mirrors Views.swift and Home.swift.
#pragma once
#include <ctime>

#include "app.h"
#include "gfx.h"
#include "setup.h"

constexpr float kSide = 232, kBar = 88;

// ───────────────────────────── Hits, hover, animation ─────────────────────────────

enum Act {
    aNone, aBlock, aNav, aSidePlaylist, aNewPlaylist, aRow, aRowPlay, aRowLike, aPlayPause, aNext, aPrev, aShuffle,
    aRepeat, aLikeCurrent, aQueuePage, aOpenNow, aCloseNow, aSeekBar, aSeekNow, aVolume, aPlayAll, aShuffleAll,
    aMoreMenu, aEditCover, aSaveMix, aNewMixes, aSeeAll, aRecentTile, aMixTile, aSearchPill, aKind, aClearSearch,
    aAutoplay, aClearQueue, aStartRadio, aShelfPrev, aShelfNext, aUpNextRow, aLPSleeve, aHomeSleeve, aSheetStyle,
    aSheetArtwork, aSheetImage, aSheetReset, aSheetDone, aSetupGo, aSetupLater, aSearchField, aScrollbar,
};

struct Hit { float x, y, w, h; int act, arg; bool hand; };
static vector<Hit> g_hits;
static int g_hoverAct = aNone, g_hoverArg = 0;
static float g_W = 0, g_H = 0;   // client size, DIPs

static bool Hov(int act, int arg = 0) { return g_hoverAct == act && g_hoverArg == arg; }

// Hits are confined to the current clip, so scrolled-away items can't be clicked.
static void AddHit(float x, float y, float w, float h, int act, int arg = 0, bool hand = true) {
    if (!R.clips.empty()) {
        const ClipRect& c = R.clips.back();
        float l = std::max(x, c.x), t = std::max(y, c.y), r = std::min(x + w, c.x + c.w), b = std::min(y + h, c.y + c.h);
        if (r <= l || b <= t) return;
        x = l; y = t; w = r - l; h = b - t;
    }
    g_hits.push_back({x, y, w, h, act, arg, hand});
}

static const Hit* HitAt(float x, float y) {
    for (auto it = g_hits.rbegin(); it != g_hits.rend(); ++it)
        if (x >= it->x && x < it->x + it->w && y >= it->y && y < it->y + it->h) return &*it;
    return nullptr;
}

// Is the box inside the current clip at all (regardless of what's being repainted)?
static bool InClip(float x, float y, float w, float h) {
    if (R.clips.empty()) return true;
    const ClipRect& c = R.clips.back();
    return x + w > c.x && x < c.x + c.w && y + h > c.y && y < c.y + c.h;
}

// Small tweens (hover slides, shelf scrolling), stepped by the animation timer.
struct Tween { float v, target; };
static std::unordered_map<string, Tween> g_tweens;
static bool g_animating = false;
static void StartAnimating();   // main.cpp

static float Tw(const string& key, float target) {
    auto it = g_tweens.find(key);
    if (it == g_tweens.end()) { g_tweens[key] = {target, target}; return target; }
    if (it->second.target != target) { it->second.target = target; StartAnimating(); }
    return it->second.v;
}
static float Anim(const string& key, bool on) { return Tw(key, on ? 1.0f : 0.0f); }

static bool StepTweens() {   // returns true while anything still moves
    bool moving = false;
    for (auto& [k, t] : g_tweens) {
        float d = t.target - t.v;
        if (std::fabs(d) < 0.004f * std::max(1.0f, std::fabs(t.target))) t.v = t.target;
        else { t.v += d * 0.28f; moving = true; }
    }
    return moving;
}

// Records that spin are redrawn alone on each spin tick: their box, background colour and drawing.
struct SpinJob { ClipRect box, clip; DWORD bg; std::function<void()> draw; };
static vector<SpinJob> g_spinJobs;
static vector<ClipRect> g_tickBoxes;   // redrawn once a second (clock, level meter)

static void AddSpinJob(float x, float y, float w, float h, DWORD bg, std::function<void()> draw) {
    if (!InClip(x, y, w, h)) return;
    ClipRect clip = R.clips.empty() ? ClipRect{0, 0, g_W, g_H} : R.clips.back();
    g_spinJobs.push_back({{x, y, w, h}, clip, bg, std::move(draw)});
}

// ───────────────────────────── Scroll state ─────────────────────────────

static float g_scroll = 0, g_scrollTarget = 0, g_contentH = 0, g_viewH = 0;
static float g_sideScroll = 0, g_sideH = 0;
static ClipRect g_sideList{}, g_content{};
static float g_shelfTarget[2] = {0, 0};   // Home: mixes, recent
static ClipRect g_shelfBox[2]{};
static float g_shelfMax[2] = {0, 0};
static ClipRect g_searchBox{};            // where the search text box goes (DIPs); w == 0 hides it
static ClipRect g_seekTrack[2]{};         // bar, Now Playing
static ClipRect g_volumeKnob{};
static bool g_listNearEnd = false;

struct Drag { int act = aNone; float x0 = 0, y0 = 0, v0 = 0; double value = 0; } g_drag;

// The tracks shown on the current page (row hits index into this).
enum ListKind { kPlain, kQueue, kPlaylist };
static vector<Track> g_view;
static int g_viewKind = kPlain;

static DWORD Mix(DWORD a, DWORD b, float t) {   // a → b by t
    auto ch = [&](int s) { return (DWORD)std::lround(((a >> s) & 255) * (1 - t) + ((b >> s) & 255) * t) << s; };
    return ch(16) | ch(8) | ch(0);
}

// ───────────────────────────── Widgets ─────────────────────────────

static const wchar_t kPlayIcon[] = L"\x1", kPauseIcon[] = L"\x2";

static void GlyphAt(const wchar_t* g, float cx, float cy, float size, DWORD c) {
    if (g == kPlayIcon) PlayGlyph(cx, cy, size * 0.72f, c);
    else if (g == kPauseIcon) PauseGlyph(cx, cy, size * 0.8f, c);
    else Icon(g, cx, cy, size, c);
}

static float PillWidth(const wstring& label, const wchar_t* icon, bool primary) {
    float tw = label.empty() ? 0 : TextWidth(label, UIF(14, primary ? FW_BOLD : FW_SEMIBOLD));
    return 20 + (icon ? 14 + (label.empty() ? 0 : 8) : 0) + tw + 20;
}

// SwiftUI PillButton: a capsule, red and white (primary) or outlined in ink.
static float Pill(float x, float y, const wstring& label, const wchar_t* icon, bool primary, int act, int arg = 0,
                  bool enabled = true, float h = 42) {
    float w = PillWidth(label, icon, primary);
    bool hover = enabled && Hov(act, arg);
    if (primary) {
        DWORD fill = !enabled ? Mix(pal::accent, pal::paper, 0.55f) : hover ? pal::accentText : pal::accent;
        FillRound(x, y, w, h, h / 2, fill);
    } else {
        if (hover) FillRound(x, y, w, h, h / 2, pal::rowHover);
        StrokeRound(x, y, w, h, h / 2, enabled ? pal::ink : Mix(pal::ink, pal::paper, 0.6f), 1.5f);
    }
    DWORD fg = primary ? pal::onAccent : enabled ? pal::ink : Mix(pal::ink, pal::paper, 0.6f);
    float cx = x + 20;
    if (icon) { GlyphAt(icon, cx + 7, y + h / 2, 14, fg); cx += 14 + (label.empty() ? 0 : 8); }
    Text(label, cx, y, w - (cx - x), h, UIF(14, primary ? FW_BOLD : FW_SEMIBOLD), fg);
    if (enabled) AddHit(x, y, w, h, act, arg);
    return w;
}

// SwiftUI CircleButton: outlined in ink, or filled black with a red glyph.
static void CircleBtn(float x, float y, float size, const wchar_t* glyph, bool filled, int act, int arg = 0,
                      bool enabled = true, float dim = 0) {
    bool hover = enabled && Hov(act, arg);
    DWORD ink = Mix(pal::ink, pal::paper, enabled ? dim : 0.6f);
    if (filled) {
        FillCircle(x + size / 2, y + size / 2, size / 2, hover ? pal::inkSoft : pal::selectedBg);
        GlyphAt(glyph, x + size / 2, y + size / 2, size * 0.36f, pal::barHighlight);
    } else {
        if (hover) FillCircle(x + size / 2, y + size / 2, size / 2, pal::rowHover);
        StrokeCircle(x + size / 2, y + size / 2, size / 2, ink, 1.5f);
        GlyphAt(glyph, x + size / 2, y + size / 2, size * 0.36f, ink);
    }
    if (enabled) AddHit(x, y, size, size, act, arg);
}

static float Link(float x, float y, float h, const wstring& label, int act, bool enabled = true, bool alignRight = false) {
    Fnt f = UIF(13, FW_SEMIBOLD);
    float w = TextWidth(label, f);
    if (alignRight) x -= w;
    DWORD c = !enabled ? pal::muted : Hov(act) ? pal::accent : pal::accentText;
    Text(label, x, y, w + 2, h, f, c);
    if (enabled) AddHit(x - 4, y, w + 8, h, act);
    return w;
}

static void Kicker(const wstring& s, float x, float y, float w) {
    Text(s, x, y, w, 16, UIF(11.5f, FW_BOLD), pal::accentText, kLine, 1.2f);
}

// A progress line you can click or drag anywhere to seek.
static void Scrubber(float x, float y, float w, double time, double dur, DWORD track, DWORD fill, DWORD knob,
                     DWORD label, float hgt, int act, int slot) {
    if (g_drag.act == act) time = g_drag.value;
    float tx = x + 50, tw = std::max(10.0f, w - 100);
    double total = std::max(dur, 1.0);
    float frac = (float)std::clamp(time / total, 0.0, 1.0);
    float cy = y + 8;
    Text(FormatTime(time), x, y, 40, 16, UIF(11), label, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    FillRound(tx, cy - hgt / 2, tw, hgt, hgt / 2, track);
    if (frac > 0) FillRound(tx, cy - hgt / 2, std::max(hgt, tw * frac), hgt, hgt / 2, fill);
    if (Hov(act) || g_drag.act == act) {
        float kr = hgt * 1.6f;
        Shadow(tx + tw * frac - kr, cy - kr, 2 * kr, 2 * kr, 0, 2, 1, 0.25f, true);
        FillCircle(tx + tw * frac, cy, kr, knob);
    }
    Text(FormatTime(dur), tx + tw + 10, y, 40, 16, UIF(11), label, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    AddHit(tx - 6, y - 4, tw + 12, 24, act, 0, true);
    g_seekTrack[slot] = {tx, y, tw, 16};
    g_tickBoxes.push_back({x, y - 6, w, 28});
}

// Retro volume knob: drag up/down (or scroll) to turn it.
static void VolumeKnob(float x, float y) {
    float s = 34, cx = x + s / 2, cy = y + s / 2;
    if (Visible(x, y, s, s)) {
        Gdiplus::GraphicsPath p;
        p.AddEllipse(PX(x), PX(y), PX(s), PX(s));
        Gdiplus::PathGradientBrush b(&p);
        b.SetCenterPoint(Gdiplus::PointF(PX(x + s * 0.35f), PX(y + s * 0.3f)));
        b.SetCenterColor(GC(pal::knobLight));
        Gdiplus::Color surround[1] = {GC(pal::knobDark)};
        INT n = 1;
        b.SetSurroundColors(surround, &n);
        R.g->FillPath(&b, &p);
        StrokeCircle(cx, cy, s / 2, pal::barTrack, 2);
        R.g->TranslateTransform(PX(cx), PX(cy));
        R.g->RotateTransform(-135.0f + 270.0f * g.volume);
        Gdiplus::GraphicsPath ind;
        RoundPath(ind, PX(-1.5f), PX(-13.5f), PX(3), PX(9), PX(1.5f));
        Gdiplus::SolidBrush red(GC(pal::barHighlight));
        R.g->FillPath(&red, &ind);
        R.g->ResetTransform();
    }
    AddHit(x, y, s, s, aVolume, 0, true);
    g_volumeKnob = {x, y, s, s};
}

// A record that spins on its own when music plays, without the sleeve.
static void SpinningRecord(float x, float y, float size, const string& art, bool spinning, DWORD bg) {
    auto draw = [=] {
        Vinyl(x, y, size, art, pal::gold, pal::paper, size < 80 ? 0.32f : 0.2f, false, spinning ? g_spinAngle : 0);
        Sheen(x, y, size);
    };
    draw();
    if (spinning) AddSpinJob(x, y, size, size, bg, draw);
}

static void SpinSleeve(float x, float y, float size, const string& art, bool spinning, float radius, float slide, DWORD bg) {
    auto draw = [=] { SleeveWithRecord(x, y, size, art, spinning, radius, slide); };
    draw();
    float disc = size * 0.96f, ox = x + size * (0.45f + 0.07f * slide), oy = y + size * 0.02f;
    if (spinning) AddSpinJob(ox, oy, disc, disc, bg, draw);
}

// A playlist's sleeve: a retro design, its songs' artwork, or the user's picture.
static void PlaylistSleeve(const Playlist& p, float x, float y, float size, bool showTitle = true) {
    float rad = std::max(4.0f, size * 0.04f);
    if (p.cover == Playlist::Image) { Cover(CoverURL(p.image), x, y, size, rad); return; }
    if (p.cover == Playlist::Artwork) {
        vector<string> thumbs, seen;
        for (auto& t : p.tracks)
            if (std::find(seen.begin(), seen.end(), t.thumb) == seen.end()) { seen.push_back(t.thumb); thumbs.push_back(t.thumbAt(300)); }
        if (thumbs.size() >= 4) {
            if (!Visible(x, y, size, size)) return;
            int px = IPX(x + size) - IPX(x), half = px / 2;
            string key = "mosaic|" + thumbs[0] + thumbs[1] + thumbs[2] + thumbs[3] + "|" + std::to_string(px);
            Bitmap* m = g_variants.get(key);
            if (!m) {
                Bitmap* src[4];
                bool ready = true;
                for (int i = 0; i < 4; ++i) if (!(src[i] = Art(thumbs[i]))) ready = false;
                if (ready) {
                    Bitmap grid(px, px, PixelFormat32bppPARGB);
                    Gdiplus::Graphics gg(&grid);
                    gg.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
                    for (int i = 0; i < 4; ++i)
                        gg.DrawImage(src[i], (i % 2) * half, (i / 2) * half, i % 2 ? px - half : half, i / 2 ? px - half : half);
                    m = g_variants.put(key, Shaped(&grid, px, px, IPX(rad), false));
                }
            }
            if (m) DrawBitmap(m, (float)IPX(x), (float)IPX(y));
            else { FillRound(x, y, size, size, rad, pal::linen); Icon(glyph::note, x + size / 2, y + size / 2, size * 0.2f, pal::muted); }
            return;
        }
        if (!thumbs.empty()) { Cover(thumbs[0], x, y, size, rad); return; }
        FillRound(x, y, size, size, rad, pal::linen);
        return;
    }
    RetroSleeve(x, y, size, PlaylistStyle(p), showTitle ? W(p.name) : L"");
}

// ───────────────────────────── Track list (the back of an LP) ─────────────────────────────

static float TrackList(const vector<Track>& tracks, float x, float y, float w, int kind) {
    g_view = tracks;
    g_viewKind = kind;
    if (tracks.empty()) {
        Text(L"Nothing on this record yet \x2014 right-click any song to add it.", x, y + 30, w, 24, DispItalic(15), pal::muted);
        return 84;
    }
    const float rowH = 52;
    float h = 16 + rowH * tracks.size();
    FillRound(x, y, w, h, 18, pal::card);
    StrokeRound(x, y, w, h, 18, pal::line, 1);
    float rx = x + 16, rw = w - 32;
    const Track* cur = g.current();
    bool wide = w > 720;
    for (size_t i = 0; i < tracks.size(); ++i) {
        float ry = y + 8 + rowH * i;
        if (!InClip(rx, ry, rw, rowH)) continue;   // off screen: no hits, no drawing
        if (i + 4 >= tracks.size()) g_listNearEnd = true;
        const Track& t = tracks[i];
        int n = (int)i;
        float right = rx + rw - 10, heartX = right - 44 - 14 - 16;
        AddHit(rx, ry, rw, rowH, aRow, n, false);
        AddHit(rx + 10, ry, 36, rowH, aRowPlay, n);
        AddHit(heartX - 4, ry + 12, 24, 28, aRowLike, n);
        if (!Visible(rx, ry, rw, rowH)) continue;

        bool isCurrent = kind != kQueue && cur && cur->id == t.id;
        bool hover = Hov(aRow, n) || Hov(aRowPlay, n) || Hov(aRowLike, n);
        if (isCurrent) FillRound(rx, ry, rw, rowH, 8, pal::rowPlaying);
        else if (hover) FillRound(rx, ry, rw, rowH, 8, pal::rowHover);
        FillRect(rx + 4, ry + rowH - 1, rw - 8, 1, pal::linen);
        float cx = rx + 10;
        if (hover && !isCurrent) PlayGlyph(cx + 6, ry + rowH / 2, 10, pal::ink);
        else Text(std::to_wstring(i + 1), cx, ry, 36, rowH, Disp(13.5f), isCurrent ? pal::accentText : pal::muted);
        cx += 36 + 14;
        Cover(t.thumbAt(120), cx, ry + 7, 38, 4);
        cx += 38 + 14;
        Text(W(t.duration), right - 44, ry, 44, rowH, UIF(12.5f), pal::muted, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        bool liked = IsLiked(t.id);
        if (liked || hover) Icon(liked ? glyph::heartFill : glyph::heart, heartX + 8, ry + rowH / 2, 13, liked ? pal::accent : pal::muted);
        float textR = heartX - 14;
        if (wide) {
            Text(W(t.album), textR - 200, ry, 200, rowH, UIItalic(12.5f), pal::muted);
            textR -= 200 + 14;
        }
        Text(W(t.title), cx, ry + 7, textR - cx, 20, UIF(13.5f, FW_SEMIBOLD), isCurrent ? pal::accentText : pal::ink);
        Text(W(t.artist), cx, ry + 27, textR - cx, 18, UIF(12.5f), pal::muted);
    }
    return h;
}

// ───────────────────────────── LP header ─────────────────────────────

enum HeaderKind { kCollection, kPlaylistHeader, kMixHeader };

static float LPHeader(float x, float y, float w, int style, const string& labelArt, const std::function<void(float, float, float)>& sleeve,
                      const wstring& kicker, const wstring& title, const wstring& subtitle, const wstring& meta,
                      HeaderKind kind, bool empty) {
    const SleevePal& p = kSleeves[style];
    AddHit(x, y, 310, 200, aLPSleeve, 0, false);
    bool hover = Hov(aLPSleeve) || Hov(aEditCover);
    float slide = Anim("lp", hover);
    float rx = x + 100 + 16 * slide, ry = y + 6;
    Shadow(rx, ry, 188, 188, 0, 10, 6, 0.25f, true);
    bool hasArt = Art(labelArt) != nullptr;
    Vinyl(rx, ry, 188, labelArt, p.sun, p.bg, hasArt ? 0.24f : 0.19f, true);
    Shadow(x, y, 200, 200, 8, 14, 8, 0.28f);
    sleeve(x, y, 200);
    if (kind == kPlaylistHeader && hover) {
        float pw = PillWidth(L"Change cover", glyph::edit, true);
        Pill(x + (200 - pw) / 2, y + 200 - 12 - 42, L"Change cover", glyph::edit, true, aEditCover);
    }

    float tx = x + 310 + 34, tw = std::max(120.0f, w - 344);
    Fnt tf = FitText(title, Disp(46), tw, 2, 0.6f);
    float titleH = std::min(WrappedHeight(title, tf, tw), LineHeight(tf) * 2);
    float total = 16 + 8 + titleH + (subtitle.empty() ? 0 : 8 + 22) + 8 + 18 + 8 + 8 + 42;
    float ty = y + 200 - 4 - total;
    Kicker(kicker, tx, ty, tw);
    ty += 16 + 8;
    Paragraph(title, tx, ty, tw, tf, pal::ink, 2);
    ty += titleH + 8;
    if (!subtitle.empty()) {
        Text(subtitle, tx, ty, tw, 22, DispItalic(16), pal::inkSoft);
        ty += 22 + 8;
    }
    Text(meta, tx, ty, tw, 18, UIF(13), pal::muted);
    ty += 18 + 8 + 8;
    float bx = tx;
    bx += Pill(bx, ty, L"Play", kPlayIcon, true, aPlayAll, 0, !empty) + 10;
    CircleBtn(bx, ty, 42, glyph::shuffle, false, aShuffleAll, 0, !empty);
    bx += 42 + 10;
    if (kind == kPlaylistHeader) CircleBtn(bx, ty, 42, glyph::more, false, aMoreMenu);
    if (kind == kMixHeader) Pill(bx, ty, L"Save to your records", nullptr, false, aSaveMix);
    return 200;
}

static wstring MetaLine(const vector<Track>& tracks) {
    double total = 0;
    for (auto& t : tracks) total += t.seconds();
    return std::to_wstring(tracks.size()) + (tracks.size() == 1 ? L" song" : L" songs") + L" \x00B7 " + FormatLength(total);
}

static float CollectionPage(float x, float y, float w, const wstring& kicker, const wstring& title, int style,
                            const vector<Track>& tracks, const wstring& sleeveTitle) {
    float h = LPHeader(x, y, w, style, "", [&](float sx, float sy, float s) { RetroSleeve(sx, sy, s, style, sleeveTitle); },
                       kicker, title, L"", MetaLine(tracks), kCollection, tracks.empty());
    return h + 22 + TrackList(tracks, x, y + h + 22, w, kPlain);
}

static float PlaylistPage(float x, float y, float w, const Playlist& p) {
    float h = LPHeader(x, y, w, PlaylistStyle(p), PlaylistLabelArt(p),
                       [&](float sx, float sy, float s) { PlaylistSleeve(p, sx, sy, s); }, L"Playlist", W(p.name), L"",
                       MetaLine(p.tracks), kPlaylistHeader, p.tracks.empty());
    return h + 22 + TrackList(p.tracks, x, y + h + 22, w, kPlaylist);
}

static float MixPage(float x, float y, float w, const Shelf* s) {
    if (!s) {
        Text(L"This mix has been refreshed", x, y + 60, w, 34, Disp(24), pal::ink, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        Text(L"Head back Home for today's mixes.", x, y + 98, w, 20, UIF(13), pal::muted, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        g_view.clear();
        return 140;
    }
    int style = SleeveForShelf(s->id);
    wstring title = W(s->title);
    float h = LPHeader(x, y, w, style, "", [&](float sx, float sy, float sz) { RetroSleeve(sx, sy, sz, style, title); },
                       L"Mix \x00B7 made for you", title, W(s->subtitle), MetaLine(s->tracks), kMixHeader, false);
    return h + 22 + TrackList(s->tracks, x, y + h + 22, w, kPlain);
}

// ───────────────────────────── Home ─────────────────────────────

static wstring Greeting() {
    time_t now = time(nullptr);
    tm local{};
    localtime_s(&local, &now);
    int h = local.tm_hour;
    return h >= 5 && h < 12 ? L"Good morning" : h >= 12 && h < 17 ? L"Good afternoon" : h >= 17 && h < 22 ? L"Good evening" : L"Late night spin";
}

// A small retro level meter that jumps with the playback clock — no animation frames.
static void LevelMeter(float x, float y, bool active, int seed) {
    for (int i = 0; i < 9; ++i) {
        float h = active ? 18.0f + ((seed * 7 + i * 13) % 11) * 5.0f : 10.0f;
        DWORD c = i % 3 == 0 ? pal::gold : i % 3 == 1 ? pal::accent : pal::alt;
        FillRound(x + i * 13, y + 72 - h, 8, h, 3, c);
    }
    g_tickBoxes.push_back({x, y, 9 * 13, 72});
}

static void NowSpinningCard(float x, float y, float w) {
    const float H = 232;
    FillRound(x, y, w, H, 20, pal::card);
    StrokeRound(x, y, w, H, 20, pal::line, 1);
    const Track* t = g.current();
    if (t) {
        float s = 184, sx = x + 28, sy = y + (H - s) / 2;
        AddHit(sx, sy, s * 1.41f, s, aHomeSleeve);
        bool spinning = g.playing && !g.loading;
        SpinSleeve(sx, sy, s, t->thumbAt(544), spinning, std::max(3.0f, s * 0.03f), Anim("home", Hov(aHomeSleeve)), pal::card);
        float mx = x + w - 28 - 112;
        LevelMeter(mx, y + (H - 72) / 2, spinning, (int)g.pos);
        float tx = sx + s * 1.41f + 30, tw = std::max(80.0f, mx - 30 - tx);
        wstring title = W(t->title);
        Fnt tf = Disp(30);
        float titleH = std::min(WrappedHeight(title, tf, tw), LineHeight(tf) * 2);
        float total = 16 + 6 + titleH + 6 + 20 + 6 + 8 + 42;
        float ty = y + (H - total) / 2;
        Kicker(L"NOW SPINNING", tx, ty, tw);
        ty += 16 + 6;
        Paragraph(title, tx, ty, tw, tf, pal::ink, 2);
        ty += titleH + 6;
        wstring line = W(t->artist);
        if (!t->album.empty()) line += (line.empty() ? L"" : L" \x00B7 ") + W(t->album);
        Text(line, tx, ty, tw, 20, UIF(14), pal::muted);
        ty += 20 + 6 + 8;
        float bx = tx + Pill(tx, ty, g.playing ? L"Pause" : L"Resume", g.playing ? kPauseIcon : kPlayIcon, true, aPlayPause) + 10;
        Pill(bx, ty, L"Start radio", nullptr, false, aStartRadio);
    } else {
        float sy = y + (H - 156) / 2;
        Vinyl(x + 28 + 92, sy + 4, 148, "", pal::gold, pal::accent, 0.2f, true);
        RetroSleeve(x + 28, sy, 156, kSleeveHome, L"Mosics");
        float tx = x + 28 + 250 + 30;
        Kicker(L"THE TURNTABLE IS QUIET", tx, y + H / 2 - 44, w - 340);
        Paragraph(L"Pick a mix below,\nor search for a song.", tx, y + H / 2 - 22, w - 340, Disp(26), pal::ink, 2);
    }
}

// A row that scrolls sideways (with the arrows, Shift+wheel or a touchpad).
static void ShelfArrows(float right, float y, int shelf) {
    if (g_shelfMax[shelf] <= 0) return;
    float x = right - 64;
    bool canL = g_shelfTarget[shelf] > 0, canR = g_shelfTarget[shelf] < g_shelfMax[shelf];
    CircleBtn(x, y, 28, glyph::left, false, aShelfPrev, shelf, canL);
    CircleBtn(x + 34, y, 28, glyph::right, false, aShelfNext, shelf, canR);
}

static float HomePage(float x, float y, float w) {
    float y0 = y;
    Text(Greeting(), x, y, w - 300, 52, Disp(40), pal::ink);
    Text(L"Pull a record from the crate.", x, y + 54, w - 300, 20, UIF(14), pal::muted);
    {
        float px = x + w - 280, py = y + 74 - 42;
        FillRound(px, py, 280, 42, 21, Hov(aSearchPill) ? pal::rowHover : pal::card);
        StrokeRound(px, py, 280, 42, 21, pal::line, 1);
        Icon(glyph::search, px + 24, py + 21, 13, pal::muted);
        Text(L"Search songs, artists\x2026", px + 42, py, 220, 42, UIF(14), pal::muted);
        AddHit(px, py, 280, 42, aSearchPill);
    }
    y += 74 + 28;
    NowSpinningCard(x, y, w);
    y += 232 + 28;

    // Mixes for you
    vector<const Shelf*> mixes;
    for (auto& s : g.shelves) if (s.id != "recent") mixes.push_back(&s);
    float tw = TextWidth(L"Mixes for you", Disp(22));
    Text(L"Mixes for you", x, y, tw + 4, 30, Disp(22), pal::ink);
    if (g.shelvesLoading) Text(L"digging\x2026", x + tw + 12, y + 4, 120, 26, DispItalic(13), pal::muted);
    float lw = Link(x + w, y + 4, 26, L"New mixes", aNewMixes, !g.shelvesLoading, true);
    ShelfArrows(x + w - lw - 16, y + 1, 0);
    y += 30 + 14;
    if (mixes.empty()) {
        Text(g.shelvesLoading ? L"Digging through the crates\x2026" : L"Couldn't load mixes \x2014 check your connection.",
             x, y, w, 24, DispItalic(15), pal::muted);
        g_shelfMax[0] = 0;
        y += 24;
    } else {
        const float pitch = 196 + 26, rowH = 6 + 128 + 10 + 19 + 36 + 6;
        g_shelfMax[0] = std::max(0.0f, mixes.size() * pitch - 26 - 48 - w);
        g_shelfTarget[0] = std::clamp(g_shelfTarget[0], 0.0f, g_shelfMax[0]);
        float off = Tw("shelf0", g_shelfTarget[0]);
        g_shelfBox[0] = {x - 36, y, w + 72, rowH};
        PushClip(x - 36, y, w + 72, rowH);
        for (size_t i = 0; i < mixes.size(); ++i) {
            const Shelf& s = *mixes[i];
            float tx = x + i * pitch - off, ty = y + 6;
            if (tx > x + w + 40 || tx + 240 < x - 40) continue;
            int n = (int)i, style = SleeveForShelf(s.id);
            AddHit(tx, ty, 196, rowH - 12, aMixTile, n);
            float slide = Anim("mix" + s.id, Hov(aMixTile, n));
            const SleevePal& p = kSleeves[style];
            string art = s.tracks.empty() ? "" : s.tracks[0].thumbAt(226);
            float rx = tx + 46 + 24 * slide;
            Shadow(rx, ty + 4, 120, 120, 0, 7, 4, 0.22f, true);
            Vinyl(rx, ty + 4, 120, art, p.sun, p.bg, 0.22f, true);
            Shadow(tx, ty, 128, 128, 5, 9, 5, 0.22f);
            RetroSleeve(tx, ty, 128, style, W(s.title));
            Text(W(s.title), tx, ty + 128 + 10, 170, 19, UIF(14, FW_SEMIBOLD), pal::ink);
            Paragraph(W(s.subtitle), tx, ty + 128 + 10 + 20, 170, UIF(12.5f), pal::muted, 2);
        }
        PopClip();
        y += rowH;
    }

    // Jump back in
    if (!g.recent.empty()) {
        y += 28;
        Text(L"Jump back in", x, y, w / 2, 30, Disp(22), pal::ink);
        float lw2 = Link(x + w, y + 4, 26, L"See all", aSeeAll, true, true);
        ShelfArrows(x + w - lw2 - 16, y + 1, 1);
        y += 30 + 12;
        size_t n = std::min<size_t>(14, g.recent.size());
        const float pitch = 96 + 22, rowH = 6 + 92 + 8 + 18 + 6;
        g_shelfMax[1] = std::max(0.0f, n * pitch - 22 - 48 - w);
        g_shelfTarget[1] = std::clamp(g_shelfTarget[1], 0.0f, g_shelfMax[1]);
        float off = Tw("shelf1", g_shelfTarget[1]);
        g_shelfBox[1] = {x - 36, y, w + 72, rowH};
        PushClip(x - 36, y, w + 72, rowH);
        const Track* cur = g.current();
        for (size_t i = 0; i < n; ++i) {
            const Track& t = g.recent[i];
            float tx = x + i * pitch - off, ty = y + 6;
            if (tx > x + w + 40 || tx + 120 < x - 40) continue;
            int k = (int)i;
            AddHit(tx, ty, 96, rowH - 12, aRecentTile, k);
            bool hover = Hov(aRecentTile, k);
            float turn = Tw("rec" + std::to_string(i), hover ? 24.0f : 0.0f);
            Shadow(tx + 2, ty, 92, 92, 0, 7, 4, 0.22f, true);
            Vinyl(tx + 2, ty, 92, t.thumbAt(226), pal::gold, pal::paper, 0.27f, true, turn);
            if (hover) {
                FillCircle(tx + 48, ty + 46, 17, pal::accent);
                PlayGlyph(tx + 49, ty + 46, 11, pal::onAccent);
            }
            Text(W(t.title), tx, ty + 92 + 8, 96, 18, UIF(12.5f, FW_SEMIBOLD),
                 cur && cur->id == t.id ? pal::accentText : pal::ink, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
        PopClip();
        y += rowH;
    }
    g_view.clear();
    return y - y0;
}

// ───────────────────────────── Search & Queue ─────────────────────────────

static float SearchPage(float x, float y, float w) {
    float y0 = y;
    Text(L"Search", x, y, w / 2, 52, Disp(40), pal::ink);
    {   // Songs | Videos
        const wchar_t* kinds[] = {L"Songs", L"Videos"};
        float bw[2], total = 4 + 4 + 4;
        for (int i = 0; i < 2; ++i) total += (bw[i] = TextWidth(kinds[i], UIF(13, FW_SEMIBOLD)) + 32);
        float sx = x + w - total, sy = y + 8;
        StrokeRound(sx, sy, total, 40, 20, pal::line, 1);
        float bx = sx + 4;
        for (int i = 0; i < 2; ++i) {
            bool sel = g.searchKind == i;
            if (sel) FillRound(bx, sy + 4, bw[i], 32, 16, pal::selectedBg);
            else if (Hov(aKind, i)) FillRound(bx, sy + 4, bw[i], 32, 16, pal::rowHover);
            Text(kinds[i], bx, sy + 4, bw[i], 32, UIF(13, FW_SEMIBOLD), sel ? pal::selectedText : pal::inkSoft,
                 DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            AddHit(bx, sy + 4, bw[i], 32, aKind, i);
            bx += bw[i] + 4;
        }
    }
    y += 52 + 16;
    FillRound(x, y, w, 46, 23, pal::card);
    StrokeRound(x, y, w, 46, 23, pal::line, 1);
    AddHit(x, y, w, 46, aSearchField, 0, false);
    Icon(glyph::search, x + 26, y + 23, 14, pal::muted);
    float right = x + w - 18;
    if (!g.query.empty()) {
        right -= 22;
        FillCircle(right + 11, y + 23, 9, Hov(aClearSearch) ? pal::inkSoft : pal::muted);
        Icon(glyph::close, right + 11, y + 23, 7, pal::card);
        AddHit(right - 4, y + 8, 30, 30, aClearSearch);
        right -= 10;
    }
    if (g.searching) {
        right -= 80;
        Text(L"Searching\x2026", right, y, 80, 46, UIF(12), pal::muted, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        right -= 10;
    }
    g_searchBox = {x + 44, y + 11, std::max(40.0f, right - (x + 44)), 24};
    y += 46;
    if (!g.searchError.empty()) {
        y += 16;
        Text(W(g.searchError), x, y, w, 20, UIF(13), pal::danger);
        y += 20;
    } else if (g.results.empty() && !g.searching && !Trim(g.query).empty()) {
        y += 16;
        Text(g.searchKind == 0 ? L"No songs found \x2014 try Videos for live versions, covers and uploads." : L"No results.",
             x, y, w, 24, DispItalic(15), pal::muted);
        y += 24;
    }
    y += 22;
    if (!g.results.empty() || Trim(g.query).empty()) y += TrackList(g.results, x, y, w, kPlain);
    else g_view.clear();
    if (g.loadingMore) {
        Text(L"Loading more\x2026", x, y + 14, w, 22, DispItalic(14), pal::muted, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        y += 40;
    }
    return y - y0;
}

static void Switch(float x, float y, bool on) {   // a small toggle, 36×20
    FillRound(x, y, 36, 20, 10, on ? pal::accent : pal::track);
    FillCircle(on ? x + 26 : x + 10, y + 10, 8, pal::card);
}

static float QueuePage(float x, float y, float w) {
    float y0 = y;
    vector<Track> up = g.upNext();
    Text(L"Queue", x, y, w / 2, 52, Disp(40), pal::ink);
    float cw = PillWidth(L"Clear", nullptr, false);
    Pill(x + w - cw, y + 6, L"Clear", nullptr, false, aClearQueue, 0, !up.empty());
    {
        wstring label = L"Autoplay similar songs";
        float lw = TextWidth(label, UIF(13));
        float tx = x + w - cw - 24 - 36 - 10 - lw;
        Text(label, tx, y + 6, lw + 4, 42, UIF(13), pal::ink);
        Switch(tx + lw + 10, y + 17, g.autoplay);
        AddHit(tx, y + 6, lw + 10 + 36, 42, aAutoplay);
    }
    y += 52 + 18;
    if (const Track* now = g.current()) {
        SpinningRecord(x, y, 64, now->thumbAt(226), g.playing && !g.loading, pal::paper);
        float tx = x + 64 + 16;
        Kicker(L"NOW SPINNING", tx, y + 4, w - 80);
        Text(W(now->title), tx, y + 20, w - 80, 28, Disp(20), pal::ink);
        Text(W(now->artist), tx, y + 46, w - 80, 18, UIF(13), pal::muted);
        y += 64 + 18;
    }
    Text(L"Up next", x, y, w, 24, DispItalic(17), pal::inkSoft);
    y += 24 + 22;
    y += TrackList(up, x, y, w, kQueue);
    return y - y0;
}

// ───────────────────────────── Sidebar ─────────────────────────────

static void SideItem(float x, float y, float w, bool selected, int act, int arg, const std::function<void(float, float, DWORD)>& icon,
                     const wstring& title) {
    AddHit(x, y, w, 38, act, arg);
    if (!Visible(x, y, w, 38)) return;
    if (selected) FillRound(x, y, w, 38, 10, pal::selectedBg);
    else if (Hov(act, arg)) FillRound(x, y, w, 38, 10, pal::rowHover);
    DWORD fg = selected ? pal::selectedText : pal::inkSoft;
    icon(x + 11 + 10, y + 19, fg);
    Text(title, x + 11 + 20 + 11, y, w - 53, 38, UIF(13.5f, selected ? FW_SEMIBOLD : FW_NORMAL), fg);
}

static void Sidebar(float H) {
    FillRect(0, 0, kSide, H, pal::sidebar);
    FillRect(kSide, 0, 1, H, pal::line);
    float x = 14, w = kSide - 28, y = 22;
    Vinyl(x + 8, y, 34, "", pal::gold, pal::accent, 0.3f, true);
    Text(L"mosics", x + 8 + 34 + 10, y - 2, 150, 36, Disp(25), pal::ink);
    y += 34 + 18;
    struct Item { Page p; const wchar_t* g; const wchar_t* t; } items[] = {
        {Page::Home, glyph::home, L"Home"}, {Page::Search, glyph::search, L"Search"}, {Page::Liked, glyph::heart, L"Liked Songs"},
        {Page::Recent, glyph::clock, L"Recently Played"}, {Page::Queue, glyph::list, L"Queue"}};
    for (auto& it : items) {
        const wchar_t* gl = it.g;
        SideItem(x, y, w, g.page == it.p && !g.nowPlaying, aNav, (int)it.p,
                 [gl](float cx, float cy, DWORD c) { Icon(gl, cx, cy, 14, c); }, it.t);
        y += 38 + 3;
    }
    y += 17;
    Text(L"Your records", x + 12, y, w - 12, 20, DispItalic(14), pal::muted);
    y += 20 + 6;

    float listBottom = H - 16 - 42 - 8, listH = std::max(0.0f, listBottom - y);
    g_sideList = {x, y, w, listH};
    g_sideH = g.playlists.size() * 40.0f;
    g_sideScroll = std::clamp(g_sideScroll, 0.0f, std::max(0.0f, g_sideH - listH));
    PushClip(x - 2, y, w + 4, listH);
    for (size_t i = 0; i < g.playlists.size(); ++i) {
        const Playlist& p = g.playlists[i];
        float py = y + i * 40 - g_sideScroll;
        if (py + 38 < y || py > y + listH) continue;
        string art = PlaylistLabelArt(p);
        const SleevePal& sp = kSleeves[PlaylistStyle(p)];
        SideItem(x, py, w, g.page == Page::Playlist && g.pageId == p.id && !g.nowPlaying, aSidePlaylist, (int)i,
                 [&](float cx, float cy, DWORD) {
                     bool has = Art(art) != nullptr;
                     Vinyl(cx - 12, cy - 12, 24, art, sp.sun, sp.bg, has ? 0.4f : 0.32f, false);
                 },
                 W(p.name));
    }
    PopClip();
    if (g_sideH > listH && listH > 0) {   // fade hint that the list scrolls
        float frac = listH / g_sideH, ty = y + (g_sideScroll / g_sideH) * listH;
        FillRound(kSide - 6, ty, 3, std::max(20.0f, listH * frac), 1.5f, pal::line);
    }
    float pw = w;
    bool hover = Hov(aNewPlaylist);
    float by = H - 16 - 42;
    if (hover) FillRound(x, by, pw, 42, 21, pal::rowHover);
    StrokeRound(x, by, pw, 42, 21, pal::ink, 1.5f);
    float tw = TextWidth(L"New playlist", UIF(14, FW_SEMIBOLD));
    float cx = x + (pw - (14 + 8 + tw)) / 2;
    Icon(glyph::add, cx + 7, by + 21, 13, pal::ink);
    Text(L"New playlist", cx + 22, by, tw + 4, 42, UIF(14, FW_SEMIBOLD), pal::ink);
    AddHit(x, by, pw, 42, aNewPlaylist);
}

// ───────────────────────────── Player bar ─────────────────────────────

static void PlayButton(float cx, float cy, float r) {
    bool hover = Hov(aPlayPause);
    FillCircle(cx, cy, r, hover ? pal::barHighlight : pal::accent);
    if (g.loading && g.playing) {   // a little turning arc while the song loads
        Gdiplus::Pen pen(GC(pal::onAccent), PX(2));
        pen.SetStartCap(Gdiplus::LineCapRound);
        pen.SetEndCap(Gdiplus::LineCapRound);
        float a = (float)std::fmod(g_spinAngle * 2, 360.0);
        R.g->DrawArc(&pen, PX(cx - 8), PX(cy - 8), PX(16), PX(16), a, 270.0f);
    } else if (g.playing) {
        PauseGlyph(cx, cy, r * 0.62f, pal::onAccent);
    } else {
        PlayGlyph(cx + r * 0.06f, cy, r * 0.56f, pal::onAccent);
    }
}

static void PlayerBar(float y0, float Wd) {
    FillRect(0, y0, Wd, kBar, pal::bar);
    const Track* t = g.current();
    // Left: the record and the song
    {
        float x = 20;
        if (t) AddHit(x, y0 + 8, 290, kBar - 16, aOpenNow);
        PushClip(x, y0, 80, kBar);
        bool spinning = g.playing && !g.loading;
        SpinSleeve(x, y0 + 16, 56, t ? t->thumbAt(226) : "", spinning, 3, Anim("bar", Hov(aOpenNow)), pal::bar);
        PopClip();
        float tx = x + 80 + 12, tw = 290 - 92;
        Text(t ? W(t->title) : L"Nothing playing", tx, y0 + 22, tw, 20, UIF(14, FW_BOLD), pal::barText);
        if (!g.error.empty()) Paragraph(W(g.error), tx, y0 + 43, tw, UIF(12), pal::barDanger, 2);
        else Text(t ? W(t->artist) : L"Pick a record to play", tx, y0 + 43, tw, 18, UIF(12.5f), pal::barMuted);
    }
    // Right: like, queue, volume
    float right = Wd - 20;
    {
        wstring vol = std::to_wstring((int)std::lround(g.volume * 100));
        Text(vol, right - 24, y0, 24, kBar, UIF(12), pal::barMuted);
        float kx = right - 24 - 14 - 34;
        VolumeKnob(kx, y0 + (kBar - 34) / 2);
        float lx = kx - 14 - 10;
        Icon(glyph::list, lx, y0 + kBar / 2, 15, Hov(aQueuePage) || g.page == Page::Queue ? pal::barText : pal::barMuted);
        AddHit(lx - 14, y0 + 28, 28, 32, aQueuePage);
        if (t) {
            float hx = lx - 14 - 22;
            bool liked = IsLiked(t->id);
            Icon(liked ? glyph::heartFill : glyph::heart, hx, y0 + kBar / 2, 15,
                 liked ? pal::barHighlight : Hov(aLikeCurrent) ? pal::barText : pal::barMuted);
            AddHit(hx - 14, y0 + 28, 28, 32, aLikeCurrent);
        }
    }
    // Centre: transport + scrubber
    float cl = 20 + 290 + 18, cr = Wd - 20 - 220 - 18, cx = (cl + cr) / 2, cy = y0 + 10 + 22;
    bool on = t != nullptr;
    auto dimmed = [&](DWORD c) { return on ? c : Mix(c, pal::bar, 0.6f); };
    Icon(glyph::shuffle, cx - 92, cy, 14, dimmed(g.shuffle ? pal::barHighlight : Hov(aShuffle) ? pal::barText : pal::barMuted));
    SkipGlyph(cx - 52, cy, 13, dimmed(Hov(aPrev) ? pal::barHighlight : pal::barText), false);
    PlayButton(cx, cy, 22);
    if (on && g.loading && g.playing) AddSpinJob(cx - 22, cy - 22, 44, 44, pal::bar, [cx, cy] { PlayButton(cx, cy, 22); });
    SkipGlyph(cx + 52, cy, 13, dimmed(Hov(aNext) ? pal::barHighlight : pal::barText), true);
    Icon(g.repeat == Repeat::One ? glyph::repeatOne : glyph::repeat, cx + 92, cy, 14,
         dimmed(g.repeat != Repeat::Off ? pal::barHighlight : Hov(aRepeat) ? pal::barText : pal::barMuted));
    if (on) {
        AddHit(cx - 106, cy - 16, 28, 32, aShuffle);
        AddHit(cx - 66, cy - 16, 28, 32, aPrev);
        AddHit(cx - 22, cy - 22, 44, 44, aPlayPause);
        AddHit(cx + 38, cy - 16, 28, 32, aNext);
        AddHit(cx + 78, cy - 16, 28, 32, aRepeat);
    }
    float sw = std::min(500.0f, cr - cl);
    Scrubber(cx - sw / 2, y0 + 10 + 44 + 7, sw, g.pos, g.dur, pal::barTrack, pal::barHighlight, pal::barText, pal::barMuted, 4,
             on ? aSeekBar : aNone, 0);
}

// ───────────────────────────── Now Playing ─────────────────────────────

static void NowPlaying(float Wd, float H) {
    FillRect(0, 0, Wd, H, pal::paper);
    AddHit(0, 0, Wd, H, aBlock, 0, false);
    CircleBtn(Wd - 24 - 42, 34, 42, glyph::down, false, aCloseNow);
    const Track* t = g.current();
    float size = std::max(220.0f, std::min({400.0f, (Wd - 260) * 0.42f, H - 220}));
    float blockW = size * 1.41f;
    float infoW = std::min(520.0f, std::max(260.0f, Wd - 140 - blockW - 60));
    float left = std::max(70.0f, (Wd - (blockW + 60 + infoW)) / 2);
    float midY = (34 + 42 + H) / 2;

    bool spinning = g.playing && !g.loading;
    AddHit(left, midY - size / 2, blockW, size, aLPSleeve, 1, false);
    SpinSleeve(left, midY - size / 2, size, t ? t->thumbAt(544) : "", spinning, 8, Anim("now", Hov(aLPSleeve, 1)), pal::paper);

    // Info column, centred on the sleeve
    float x = left + blockW + 60, w = infoW;
    wstring title = t ? W(t->title) : L"Nothing playing";
    Fnt tf = FitText(title, Disp(46), w, 3, 0.6f);
    float titleH = std::min(WrappedHeight(title, tf, w), LineHeight(tf) * 3);
    vector<Track> up = g.upNext();
    size_t nUp = std::min<size_t>(3, up.size());
    float cardH = nUp ? 12 + 22 + 6 + 46.0f * nUp + 12 : 0;
    float total = 16 + 8 + titleH + 8 + 24 + 24 + 16 + 24 + 88 + (nUp ? 24 + cardH : 0);
    float y = midY - total / 2;
    Kicker(L"NOW SPINNING", x, y, w);
    y += 16 + 8;
    Paragraph(title, x, y, w, tf, pal::ink, 3);
    y += titleH + 8;
    if (t) {
        wstring line = W(t->artist);
        if (!t->album.empty()) line += (line.empty() ? L"" : L" \x00B7 ") + W(t->album);
        Text(line, x, y, w, 24, UIF(17), pal::inkSoft);
    }
    y += 24 + 24;
    Scrubber(x, y, w, g.pos, g.dur, pal::track, pal::accent, pal::ink, pal::muted, 6, t ? aSeekNow : aNone, 1);
    y += 16 + 24;

    bool on = t != nullptr;
    float cy = y + 44, bx = x;
    CircleBtn(bx, cy - 23, 46, glyph::shuffle, false, aShuffle, 0, on, g.shuffle ? 0 : 0.45f);
    bx += 46 + 20;
    SkipGlyph(bx + 12, cy, 20, Hov(aPrev) ? pal::accent : on ? pal::ink : pal::muted, false);
    if (on) AddHit(bx, cy - 20, 26, 40, aPrev);
    bx += 26 + 20;
    StrokeCircle(bx + 44, cy, 44, pal::ink, 1.5f);
    FillCircle(bx + 44, cy, 38, Hov(aPlayPause, 1) ? pal::accentText : pal::accent);
    if (g.playing && g.loading) {
        Gdiplus::Pen pen(GC(pal::onAccent), PX(3));
        pen.SetStartCap(Gdiplus::LineCapRound);
        pen.SetEndCap(Gdiplus::LineCapRound);
        R.g->DrawArc(&pen, PX(bx + 44 - 13), PX(cy - 13), PX(26), PX(26), (float)std::fmod(g_spinAngle * 2, 360.0), 270.0f);
        float px0 = bx;
        AddSpinJob(bx, cy - 44, 88, 88, pal::paper, [px0, cy] {
            StrokeCircle(px0 + 44, cy, 44, pal::ink, 1.5f);
            FillCircle(px0 + 44, cy, 38, pal::accent);
            Gdiplus::Pen p2(GC(pal::onAccent), PX(3));
            p2.SetStartCap(Gdiplus::LineCapRound);
            p2.SetEndCap(Gdiplus::LineCapRound);
            R.g->DrawArc(&p2, PX(px0 + 44 - 13), PX(cy - 13), PX(26), PX(26), (float)std::fmod(g_spinAngle * 2, 360.0), 270.0f);
        });
    } else if (g.playing) PauseGlyph(bx + 44, cy, 24, pal::onAccent);
    else PlayGlyph(bx + 46, cy, 22, pal::onAccent);
    if (on) AddHit(bx, cy - 44, 88, 88, aPlayPause, 1);
    bx += 88 + 20;
    SkipGlyph(bx + 12, cy, 20, Hov(aNext) ? pal::accent : on ? pal::ink : pal::muted, true);
    if (on) AddHit(bx, cy - 20, 26, 40, aNext);
    bx += 26 + 20;
    CircleBtn(bx, cy - 23, 46, g.repeat == Repeat::One ? glyph::repeatOne : glyph::repeat, false, aRepeat, 0, on,
              g.repeat == Repeat::Off ? 0.45f : 0);
    if (t) CircleBtn(x + w - 46, cy - 23, 46, IsLiked(t->id) ? glyph::heartFill : glyph::heart, true, aLikeCurrent);
    y += 88;

    if (nUp) {
        y += 24;
        FillRound(x, y, w, cardH, 18, pal::card);
        StrokeRound(x, y, w, cardH, 18, pal::line, 1);
        float ix = x + 18, iw = w - 36, iy = y + 12;
        Text(L"Next up", ix, iy, iw, 22, DispItalic(16), pal::ink);
        iy += 22 + 6;
        for (size_t i = 0; i < nUp; ++i) {
            const Track& n = up[i];
            FillRect(ix, iy, iw, 1, pal::linen);
            AddHit(ix, iy, iw, 46, aUpNextRow, (int)i, false);
            if (Hov(aUpNextRow, (int)i)) FillRound(ix - 6, iy + 1, iw + 12, 45, 8, pal::rowHover);
            Text(std::to_wstring(i + 1), ix, iy, 18, 46, Disp(13), pal::muted);
            Text(W(n.title), ix + 30, iy + 5, iw - 30 - 56, 19, UIF(13.5f, FW_SEMIBOLD), pal::ink);
            Text(W(n.artist), ix + 30, iy + 24, iw - 30 - 56, 17, UIF(12), pal::muted);
            Text(W(n.duration), ix + iw - 50, iy, 50, 46, UIF(12), pal::muted, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            iy += 46;
        }
    }
}

// ───────────────────────────── Sheets ─────────────────────────────

static void Dim(float Wd, float H) {
    FillRect(0, 0, Wd, H, pal::ink, 0.35f);
    AddHit(0, 0, Wd, H, aBlock, 0, false);
}

// Pick a sleeve design, use the songs' artwork, or choose a picture.
static void CoverSheet(float Wd, float H) {
    Playlist* p = FindPlaylist(g.coverSheet);
    if (!p) { g.coverSheet.clear(); return; }
    Dim(Wd, H);
    const float cw = 660, ch = 28 + 36 + 20 + 220 + 20 + 42 + 28;
    float x = std::round((Wd - cw) / 2), y = std::round((H - ch) / 2);
    Shadow(x, y, cw, ch, 18, 24, 10, 0.3f);
    FillRound(x, y, cw, ch, 18, pal::paper);
    AddHit(x, y, cw, ch, aBlock, 0, false);
    Text(L"Change cover", x + 28, y + 28, cw - 56, 36, Disp(26), pal::ink);
    float ry = y + 28 + 36 + 20;
    Shadow(x + 28, ry, 190, 190, 7, 12, 6, 0.25f);
    PlaylistSleeve(*p, x + 28, ry, 190);
    float cx = x + 28 + 190 + 26;
    Text(L"Sleeve designs", cx, ry, 300, 16, UIF(12, FW_SEMIBOLD), pal::muted);
    float gy = ry + 16 + 12;
    int autoStyle = SleeveForName(p->name);
    for (int i = 0; i < kSleeveCount; ++i) {
        float sx = cx + (i % 5) * 72, sy = gy + (i / 5) * 72;
        RetroSleeve(sx, sy, 62, i, L"");
        bool sel = (p->cover == Playlist::Style && p->style == i) || (p->cover == Playlist::Auto && autoStyle == i);
        if (sel) StrokeRound(sx, sy, 62, 62, 5, pal::accent, 3);
        else if (Hov(aSheetStyle, i)) StrokeRound(sx, sy, 62, 62, 5, pal::line, 2);
        AddHit(sx, sy, 62, 62, aSheetStyle, i);
    }
    float by = gy + 134 + 12 + 4;
    float bw = Pill(cx, by, L"Use song artwork", glyph::grid, false, aSheetArtwork, 0, !p->tracks.empty());
    Pill(cx + bw + 10, by, L"Choose image\x2026", glyph::picture, false, aSheetImage);
    float fy = ry + 220 + 20;
    Link(x + 28, fy, 42, L"Reset to automatic", aSheetReset);
    float dw = PillWidth(L"Done", nullptr, true);
    Pill(x + cw - 28 - dw, fy, L"Done", nullptr, true, aSheetDone);
}

static void SetupSheet(float Wd, float H) {
    Dim(Wd, H);
    const float cw = 580;
    const wchar_t* intro =
        L"Mosics plays music through two free, open-source helpers: yt-dlp fetches the audio, and Deno runs "
        L"the bit of YouTube code that yt-dlp needs. Setup downloads them from GitHub (about 60 MB, once).";
    bool installing = g_setup.need[0] || (g_setup.phase == 0 && !IsInstalledCopy());
    const wchar_t* install = L" It also installs Mosics for your account \x2014 Start menu, desktop shortcut and "
                             L"Apps & features \x2014 no admin rights needed.";
    wstring text = wstring(intro) + (installing ? install : L"");
    float tw = cw - 56;
    float textH = WrappedHeight(text, UIF(13.5f), tw);
    float ch = 28 + 56 + 18 + textH + 20 + 3 * 34 + (g_setup.error.empty() ? 0 : 12 + 40) + 24 + 42 + 28;
    float x = std::round((Wd - cw) / 2), y = std::round((H - ch) / 2);
    Shadow(x, y, cw, ch, 18, 24, 10, 0.3f);
    FillRound(x, y, cw, ch, 18, pal::paper);
    AddHit(x, y, cw, ch, aBlock, 0, false);
    float ix = x + 28, iy = y + 28;
    Vinyl(ix, iy, 56, "", pal::gold, pal::accent, 0.3f, true);
    Text(g_setup.phase == 2 ? L"All set" : L"Set up Mosics", ix + 56 + 16, iy, tw - 72, 56, Disp(28), pal::ink);
    iy += 56 + 18;
    Paragraph(text, ix, iy, tw, UIF(13.5f), pal::inkSoft, 8);
    iy += textH + 20;
    const wchar_t* steps[3] = {L"Install Mosics", L"yt-dlp \x2014 fetches the audio", L"Deno \x2014 helps yt-dlp with YouTube"};
    for (int i = 0; i < 3; ++i) {
        bool needed = g_setup.phase == 0 ? (i == 0 ? !IsInstalledCopy() : FindTool(i == 1 ? L"yt-dlp.exe" : L"deno.exe").empty())
                                         : g_setup.need[i];
        int st = g_setup.phase == 0 ? (needed ? 0 : 2) : g_setup.state[i];
        FillRect(ix, iy, tw, 1, pal::linen);
        float cy = iy + 17;
        if (st == 2) { FillCircle(ix + 9, cy, 9, pal::ink); Icon(glyph::check, ix + 9, cy, 8, pal::card); }
        else if (st == 3) { FillCircle(ix + 9, cy, 9, pal::accent); Text(L"!", ix, cy - 9, 18, 18, UIF(12, FW_BOLD), pal::onAccent, DT_CENTER | DT_VCENTER | DT_SINGLELINE); }
        else if (st == 1) { StrokeCircle(ix + 9, cy, 9, pal::accent, 2); FillCircle(ix + 9, cy, 4, pal::accent); }
        else StrokeCircle(ix + 9, cy, 9, pal::line, 1.5f);
        Text(steps[i], ix + 30, iy, tw - 160, 34, UIF(13.5f, FW_SEMIBOLD), st == 0 && !needed ? pal::muted : pal::ink);
        wstring status = !needed && g_setup.phase == 0 ? L"Already done" : st == 2 ? L"Done" : st == 3 ? L"Failed"
                       : st == 1 ? (i == 0 ? L"Installing\x2026" : std::to_wstring((int)(g_setup.frac[i] * 100)) + L"%") : L"";
        Text(status, ix + tw - 130, iy, 130, 34, UIF(12.5f), st == 3 ? pal::danger : pal::muted, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        if (st == 1 && i > 0) {
            FillRound(ix + 30, iy + 30, tw - 30, 3, 1.5f, pal::track);
            FillRound(ix + 30, iy + 30, std::max(3.0f, (tw - 30) * (float)g_setup.frac[i]), 3, 1.5f, pal::accent);
        }
        iy += 34;
    }
    if (!g_setup.error.empty()) {
        iy += 12;
        Paragraph(W(g_setup.error), ix, iy, tw, UIF(13), pal::danger, 2);
        iy += 40;
    }
    iy += 24;
    if (g_setup.phase == 1) {
        Text(L"Setting up\x2026 you can keep this window open.", ix, iy, tw, 42, DispItalic(14), pal::muted);
    } else {
        const wchar_t* label = g_setup.phase == 2 ? L"Start listening" : g_setup.phase == 3 ? L"Try again" : L"Set up Mosics";
        float pw = PillWidth(label, nullptr, true);
        Pill(x + cw - 28 - pw, iy, label, nullptr, true, aSetupGo);
        if (g_setup.phase != 2) Link(ix, iy, 42, L"Not now", aSetupLater);
    }
}

static void ShowSetup() {
    g_setup.visible = true;
    if (g_setup.phase != 1) { g_setup.phase = 0; g_setup.error.clear(); }
    Redraw();
}

// ───────────────────────────── The whole window ─────────────────────────────

static void PaintAll() {
    g_hits.clear();
    g_spinJobs.clear();
    g_tickBoxes.clear();
    g_searchBox = {};
    g_listNearEnd = false;
    float Wd = g_W, H = g_H, mainH = H - kBar;

    if (g.nowPlaying && g.current()) {
        NowPlaying(Wd, H);
    } else {
        g.nowPlaying = false;
        g_content = {kSide + 1, 0, Wd - kSide - 1, mainH};
        FillRect(kSide + 1, 0, Wd - kSide - 1, mainH, pal::paper);
        PushClip(kSide + 1, 0, Wd - kSide - 1, mainH);
        float x = kSide + 1 + 36, w = Wd - kSide - 1 - 72, y = 44 - g_scroll, h = 0;
        switch (g.page) {
        case Page::Home: h = HomePage(x, y, w); break;
        case Page::Search: h = SearchPage(x, y, w); break;
        case Page::Liked: h = CollectionPage(x, y, w, L"Your favourites", L"Liked Songs", kSleeveLiked, g.liked, L"Liked"); break;
        case Page::Recent: h = CollectionPage(x, y, w, L"History", L"Recently Played", kSleeveRecent, g.recent, L"Recent"); break;
        case Page::Queue: h = QueuePage(x, y, w); break;
        case Page::Playlist:
            if (Playlist* p = FindPlaylist(g.pageId)) h = PlaylistPage(x, y, w, *p);
            else { g.page = Page::Home; h = HomePage(x, y, w); }
            break;
        case Page::Mix: h = MixPage(x, y, w, FindShelf(g.pageId)); break;
        }
        g_contentH = 44 + h + 28;
        g_viewH = mainH;
        // Scrollbar, only while there is more to see
        if (g_contentH > g_viewH + 1) {
            float frac = g_viewH / g_contentH, th = std::max(36.0f, g_viewH * frac);
            float ty = (g_scroll / (g_contentH - g_viewH)) * (g_viewH - th);
            bool active = Hov(aScrollbar) || g_drag.act == aScrollbar;
            FillRound(Wd - (active ? 10 : 8), ty + 2, active ? 7 : 5, th - 4, 3, pal::line, active ? 1.0f : 0.8f);
            AddHit(Wd - 14, 0, 14, g_viewH, aScrollbar, 0, false);
        }
        PopClip();
        Sidebar(mainH);
        PlayerBar(mainH, Wd);
    }
    if (!g.coverSheet.empty()) CoverSheet(Wd, H);
    if (g_setup.visible) SetupSheet(Wd, H);
    if (!g.coverSheet.empty() || g_setup.visible || g.nowPlaying || g.page != Page::Search) g_searchBox = {};
}
