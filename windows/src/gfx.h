// Drawing for the "Modern Vintage" vinyl design, Swiss Red palette. Mirrors mac/Sources/Mosics/Theme.swift.
// Text goes through GDI (ClearType, and font fallback for Thai/Japanese); shapes and images through GDI+.
// All coordinates passed in are DIPs (1/96 inch); g_k converts them to pixels.
#pragma once
#include <windows.h>
#include <shlwapi.h>

#include <cmath>
#include <list>
#include <unordered_map>

#include "platform.h"
namespace Gdiplus { using std::max; using std::min; }
#include <gdiplus.h>

using Gdiplus::Bitmap;

// ───────────────────────────── Palette (Swiss Red) ─────────────────────────────

// The current colours. SetPalette() switches between Swiss Red (light) and Swiss Red Dark — the same
// values as Palette.swift in the Mac app.
namespace pal {
static DWORD paper, sidebar, line, card, linen, rowPlaying, rowHover, ink, inkSoft, muted, accent, accentText, onAccent,
    gold, alt, danger, selectedBg, selectedText, shadow, track, bar, barText, barMuted, barTrack, barHighlight, barDanger,
    knobLight, knobDark, vinyl, groove, grooveGap, spindle;
static bool dark = false;
}

static void SetPalette(bool dark) {
    using namespace pal;
    pal::dark = dark;
    if (!dark) {
        paper = 0xF5F4F0; sidebar = 0xECEBE6; line = 0xD6D4CC; card = 0xFFFFFF; linen = 0xE6E4DD;
        rowPlaying = 0xFBE5E3; rowHover = 0xEFEEE9; ink = 0x121212; inkSoft = 0x2E2E2E; muted = 0x666660;
        accent = 0xD7261E; accentText = 0xC0211A; onAccent = 0xFFFFFF; gold = 0x121212; alt = 0x9A9A92;
        danger = 0xC0211A; selectedBg = 0x121212; selectedText = 0xFFFFFF; shadow = 0x121212; track = 0xDEDCD5;
        bar = 0x121212; vinyl = 0x111111; groove = 0x242424; grooveGap = 0x333333; spindle = 0xF5F4F0;
    } else {
        paper = 0x161616; sidebar = 0x1D1D1C; line = 0x2F2F2D; card = 0x1F1F1E; linen = 0x2A2A28;
        rowPlaying = 0x3A1B18; rowHover = 0x272726; ink = 0xF2F1EC; inkSoft = 0xD6D4CC; muted = 0x9A9A92;
        accent = 0xE0302A; accentText = 0xFF5A4F; onAccent = 0xFFFFFF; gold = 0xF2F1EC; alt = 0x6E6E6A;
        danger = 0xFF6B61; selectedBg = 0xF2F1EC; selectedText = 0x121212; shadow = 0x000000; track = 0x333331;
        bar = 0x0C0C0C; vinyl = 0x0A0A0A; groove = 0x1F1F1F; grooveGap = 0x2E2E2E; spindle = 0x161616;
    }
    barText = 0xFFFFFF; barMuted = 0xA8A8A2; barTrack = 0x3A3A3A; barHighlight = 0xFF3B30; barDanger = 0xFF8A80;
    knobLight = 0x4A4A4A; knobDark = 0x1A1A1A;
}

// Colours for a retro sunset sleeve and its record label.
struct SleevePal { DWORD bg, sun, s1, s2, ink; };
static const SleevePal kSleeves[9] = {
    {0xD7261E, 0xFFFFFF, 0x121212, 0xF5F4F0, 0xFFFFFF}, {0xFFFFFF, 0xD7261E, 0x121212, 0xD6D4CC, 0x121212},
    {0x121212, 0xD7261E, 0xFFFFFF, 0x6E6E6E, 0xFFFFFF}, {0xF5F4F0, 0x121212, 0xD7261E, 0x9A9A92, 0x121212},
    {0x6E6E6E, 0xFFFFFF, 0xD7261E, 0x121212, 0xFFFFFF}, {0xD7261E, 0x121212, 0xFFFFFF, 0x121212, 0xFFFFFF},
    {0x121212, 0xFFFFFF, 0xD7261E, 0x6E6E6E, 0xFFFFFF}, {0xD6D4CC, 0xD7261E, 0x121212, 0xFFFFFF, 0x121212},
    {0x9A9A92, 0x121212, 0xFFFFFF, 0xD7261E, 0x121212},
};
constexpr int kSleeveCount = 9, kSleeveLiked = 7, kSleeveRecent = 6, kSleeveHome = 0;

// A stable sleeve for any name (same hash as the Mac app, so a playlist looks the same on both).
static int SleeveForName(const string& name) {
    int h = 0;
    for (unsigned char b : name) h = (h * 31 + b) & 0xFFFF;
    return h % kSleeveCount;
}
static int SleeveForShelf(const string& id) {
    if (id == "city-pop") return 0;
    if (id == "thai-indie") return 1;
    if (id == "japan") return 2;
    if (id == "britpop") return 3;
    if (id == "made-for-you") return 4;
    if (id == "because") return 5;
    return SleeveForName(id);
}

static COLORREF CR(DWORD h) { return RGB((h >> 16) & 255, (h >> 8) & 255, h & 255); }
static Gdiplus::Color GC(DWORD h, float a = 1) {
    return Gdiplus::Color((BYTE)std::clamp(a * 255.0f, 0.0f, 255.0f), (h >> 16) & 255, (h >> 8) & 255, h & 255);
}

// ───────────────────────────── Render state ─────────────────────────────

static float g_k = 1;   // pixels per DIP
static float PX(float d) { return d * g_k; }
static int IPX(float d) { return (int)std::lround(d * g_k); }

struct ClipRect { float x, y, w, h; };
struct Gfx {
    HDC dc = nullptr;
    Gdiplus::Graphics* g = nullptr;
    RECT paint{};               // dirty area, pixels
    vector<ClipRect> clips;     // DIPs
} R;

// Skip anything outside the dirty area or the current clip — most frames repaint one small record.
static bool Visible(float x, float y, float w, float h) {
    float l = x * g_k, t = y * g_k, r = (x + w) * g_k, b = (y + h) * g_k;
    if (r < R.paint.left || l > R.paint.right || b < R.paint.top || t > R.paint.bottom) return false;
    if (!R.clips.empty()) {
        const ClipRect& c = R.clips.back();
        if (x + w < c.x || x > c.x + c.w || y + h < c.y || y > c.y + c.h) return false;
    }
    return true;
}

// Applies the innermost clip (or just the dirty area) to both GDI and GDI+. Set explicitly each time:
// mixing SaveDC/RestoreDC with GDI+ Save/Restore on one DC leaves the wrong clip behind.
static void ApplyClip() {
    RECT r = R.paint;
    if (!R.clips.empty()) {
        const ClipRect& c = R.clips.back();
        r.left = std::max(r.left, (LONG)IPX(c.x));
        r.top = std::max(r.top, (LONG)IPX(c.y));
        r.right = std::min(r.right, (LONG)IPX(c.x + c.w));
        r.bottom = std::min(r.bottom, (LONG)IPX(c.y + c.h));
        if (r.right < r.left) r.right = r.left;
        if (r.bottom < r.top) r.bottom = r.top;
    }
    HRGN rgn = CreateRectRgn(r.left, r.top, r.right, r.bottom);
    SelectClipRgn(R.dc, rgn);
    DeleteObject(rgn);
    R.g->SetClip(Gdiplus::Rect(r.left, r.top, r.right - r.left, r.bottom - r.top));
}

static void PushClip(float x, float y, float w, float h) {
    if (!R.clips.empty()) {   // intersect with the enclosing clip
        const ClipRect& c = R.clips.back();
        float l = std::max(x, c.x), t = std::max(y, c.y), r = std::min(x + w, c.x + c.w), b = std::min(y + h, c.y + c.h);
        x = l; y = t; w = std::max(0.0f, r - l); h = std::max(0.0f, b - t);
    }
    R.clips.push_back({x, y, w, h});
    ApplyClip();
}

static void PopClip() {
    R.clips.pop_back();
    ApplyClip();
}

// ───────────────────────────── Bitmap caches ─────────────────────────────

// Small LRU of pre-rendered bitmaps (shadows, grooves, scaled artwork), bounded by decoded bytes.
struct BitmapCache {
    size_t limit;
    size_t bytes = 0;
    std::list<string> order;   // most recent first
    struct Entry { Bitmap* bmp; size_t size; std::list<string>::iterator it; };
    std::unordered_map<string, Entry> map;

    explicit BitmapCache(size_t limitBytes) : limit(limitBytes) {}
    Bitmap* get(const string& key) {
        auto f = map.find(key);
        if (f == map.end()) return nullptr;
        order.splice(order.begin(), order, f->second.it);
        return f->second.bmp;
    }
    Bitmap* put(const string& key, Bitmap* b) {
        if (!b) return nullptr;
        size_t size = (size_t)b->GetWidth() * b->GetHeight() * 4;
        order.push_front(key);
        map[key] = {b, size, order.begin()};
        bytes += size;
        while (bytes > limit && order.size() > 1) {
            auto last = map.find(order.back());
            bytes -= last->second.size;
            delete last->second.bmp;
            map.erase(last);
            order.pop_back();
        }
        return b;
    }
    void clear() {
        for (auto& [k, e] : map) delete e.bmp;
        map.clear();
        order.clear();
        bytes = 0;
    }
};

static BitmapCache g_shapes(16u << 20);    // shadows, grooves, sheen, sleeves
static BitmapCache g_variants(24u << 20);  // artwork scaled + rounded for a spot on screen

static void DrawBitmap(Bitmap* b, float xPx, float yPx) {
    if (b) R.g->DrawImage(b, (INT)std::lround(xPx), (INT)std::lround(yPx), (INT)b->GetWidth(), (INT)b->GetHeight());
}

// ───────────────────────────── Shapes ─────────────────────────────

static void RoundPath(Gdiplus::GraphicsPath& p, float x, float y, float w, float h, float r) {   // pixels
    r = std::min(r, std::min(w, h) / 2);
    if (r <= 0.5f) { p.AddRectangle(Gdiplus::RectF(x, y, w, h)); return; }
    float d = 2 * r;
    p.AddArc(x, y, d, d, 180, 90);
    p.AddArc(x + w - d, y, d, d, 270, 90);
    p.AddArc(x + w - d, y + h - d, d, d, 0, 90);
    p.AddArc(x, y + h - d, d, d, 90, 90);
    p.CloseFigure();
}

static void FillRect(float x, float y, float w, float h, DWORD c, float a = 1) {
    if (!Visible(x, y, w, h)) return;
    if (a >= 1) {   // plain GDI: crisp edges, fastest
        RECT r{IPX(x), IPX(y), IPX(x + w), IPX(y + h)};
        SetDCBrushColor(R.dc, CR(c));
        ::FillRect(R.dc, &r, (HBRUSH)GetStockObject(DC_BRUSH));
        return;
    }
    Gdiplus::SolidBrush b(GC(c, a));
    R.g->FillRectangle(&b, (INT)IPX(x), (INT)IPX(y), (INT)(IPX(x + w) - IPX(x)), (INT)(IPX(y + h) - IPX(y)));
}

static void FillRound(float x, float y, float w, float h, float r, DWORD c, float a = 1) {
    if (!Visible(x, y, w, h)) return;
    Gdiplus::GraphicsPath p;
    RoundPath(p, PX(x), PX(y), PX(w), PX(h), PX(r));
    Gdiplus::SolidBrush b(GC(c, a));
    R.g->FillPath(&b, &p);
}

static void StrokeRound(float x, float y, float w, float h, float r, DWORD c, float width, float a = 1) {
    if (!Visible(x - width, y - width, w + 2 * width, h + 2 * width)) return;
    float inset = width / 2;   // stroke inside the bounds, like SwiftUI's strokeBorder
    Gdiplus::GraphicsPath p;
    RoundPath(p, PX(x + inset), PX(y + inset), PX(w - width), PX(h - width), PX(r - inset));
    Gdiplus::Pen pen(GC(c, a), PX(width));
    R.g->DrawPath(&pen, &p);
}

static void FillCircle(float cx, float cy, float r, DWORD c, float a = 1) {
    if (!Visible(cx - r, cy - r, 2 * r, 2 * r)) return;
    Gdiplus::SolidBrush b(GC(c, a));
    R.g->FillEllipse(&b, PX(cx - r), PX(cy - r), PX(2 * r), PX(2 * r));
}

static void StrokeCircle(float cx, float cy, float r, DWORD c, float width, float a = 1) {
    if (!Visible(cx - r, cy - r, 2 * r, 2 * r)) return;
    Gdiplus::Pen pen(GC(c, a), PX(width));
    float rr = r - width / 2;
    R.g->DrawEllipse(&pen, PX(cx - rr), PX(cy - rr), PX(2 * rr), PX(2 * rr));
}

// Transport glyphs drawn as shapes, so they look the same on Windows 10 and 11.
static void PlayGlyph(float cx, float cy, float s, DWORD c) {
    Gdiplus::PointF pts[3] = {{PX(cx - s * 0.36f), PX(cy - s * 0.5f)}, {PX(cx + s * 0.5f), PX(cy)},
                              {PX(cx - s * 0.36f), PX(cy + s * 0.5f)}};
    Gdiplus::SolidBrush b(GC(c));
    Gdiplus::Pen pen(GC(c), PX(s * 0.12f));
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    R.g->FillPolygon(&b, pts, 3);
    R.g->DrawPolygon(&pen, pts, 3);
}
static void PauseGlyph(float cx, float cy, float s, DWORD c) {
    float w = s * 0.3f, h = s, gap = s * 0.2f;
    FillRound(cx - gap / 2 - w, cy - h / 2, w, h, w * 0.25f, c);
    FillRound(cx + gap / 2, cy - h / 2, w, h, w * 0.25f, c);
}
static void SkipGlyph(float cx, float cy, float s, DWORD c, bool forward) {
    float d = forward ? 1 : -1, tw = s * 0.5f;
    Gdiplus::SolidBrush b(GC(c));
    for (int i = 0; i < 2; ++i) {
        float x0 = cx - d * tw + d * i * tw;
        Gdiplus::PointF pts[3] = {{PX(x0), PX(cy - s * 0.42f)}, {PX(x0 + d * tw), PX(cy)}, {PX(x0), PX(cy + s * 0.42f)}};
        R.g->FillPolygon(&b, pts, 3);
    }
}

// ───────────────────────────── Soft shadows ─────────────────────────────

// A blurred silhouette, rendered once per size and cached. SwiftUI-style: blur radius, y offset, opacity.
static Bitmap* ShadowBitmap(int w, int h, int rad, int blur, bool circle, float opacity) {
    char key[96];
    snprintf(key, sizeof key, "sh%d.%d.%d.%d.%d.%d.%06lx", w, h, rad, blur, circle, (int)(opacity * 100), (unsigned long)pal::shadow);
    if (Bitmap* hit = g_shapes.get(key)) return hit;
    int pad = blur * 2, W2 = w + 2 * pad, H2 = h + 2 * pad;
    vector<float> a((size_t)W2 * H2, 0.0f);
    {   // rasterise the shape
        Bitmap mask(W2, H2, PixelFormat32bppARGB);
        Gdiplus::Graphics mg(&mask);
        mg.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        Gdiplus::SolidBrush white(Gdiplus::Color(255, 255, 255, 255));
        if (circle) mg.FillEllipse(&white, pad, pad, w, h);
        else {
            Gdiplus::GraphicsPath p;
            RoundPath(p, (float)pad, (float)pad, (float)w, (float)h, (float)rad);
            mg.FillPath(&white, &p);
        }
        Gdiplus::Rect all(0, 0, W2, H2);
        Gdiplus::BitmapData d;
        if (mask.LockBits(&all, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &d) == Gdiplus::Ok) {
            for (int y = 0; y < H2; ++y) {
                auto* row = (const uint32_t*)((const BYTE*)d.Scan0 + (size_t)y * d.Stride);
                for (int x = 0; x < W2; ++x) a[(size_t)y * W2 + x] = (row[x] >> 24) / 255.0f;
            }
            mask.UnlockBits(&d);
        }
    }
    // Three box blurs ≈ a Gaussian.
    int r = std::max(1, blur / 2);
    vector<float> tmp(a.size());
    for (int pass = 0; pass < 3; ++pass) {
        for (int y = 0; y < H2; ++y) {
            float sum = 0;
            const float* src = &a[(size_t)y * W2];
            float* dst = &tmp[(size_t)y * W2];
            for (int x = -r; x <= r; ++x) sum += src[std::clamp(x, 0, W2 - 1)];
            for (int x = 0; x < W2; ++x) {
                dst[x] = sum / (2 * r + 1);
                sum += src[std::min(x + r + 1, W2 - 1)] - src[std::max(x - r, 0)];
            }
        }
        for (int x = 0; x < W2; ++x) {
            float sum = 0;
            for (int y = -r; y <= r; ++y) sum += tmp[(size_t)std::clamp(y, 0, H2 - 1) * W2 + x];
            for (int y = 0; y < H2; ++y) {
                a[(size_t)y * W2 + x] = sum / (2 * r + 1);
                sum += tmp[(size_t)std::min(y + r + 1, H2 - 1) * W2 + x] - tmp[(size_t)std::max(y - r, 0) * W2 + x];
            }
        }
    }
    auto* out = new Bitmap(W2, H2, PixelFormat32bppPARGB);
    Gdiplus::Rect all(0, 0, W2, H2);
    Gdiplus::BitmapData d;
    if (out->LockBits(&all, Gdiplus::ImageLockModeWrite, PixelFormat32bppPARGB, &d) == Gdiplus::Ok) {
        const DWORD sc = pal::shadow;   // premultiplied below
        for (int y = 0; y < H2; ++y) {
            auto* row = (uint32_t*)((BYTE*)d.Scan0 + (size_t)y * d.Stride);
            for (int x = 0; x < W2; ++x) {
                uint32_t al = (uint32_t)std::lround(std::clamp(a[(size_t)y * W2 + x] * opacity, 0.0f, 1.0f) * 255);
                uint32_t r = ((sc >> 16) & 255) * al / 255, gr = ((sc >> 8) & 255) * al / 255, b = (sc & 255) * al / 255;
                row[x] = (al << 24) | (r << 16) | (gr << 8) | b;
            }
        }
        out->UnlockBits(&d);
    }
    return g_shapes.put(key, out);
}

static void Shadow(float x, float y, float w, float h, float rad, float blur, float dy, float opacity, bool circle = false) {
    float pad = blur * 2;
    if (!Visible(x - pad, y - pad + dy, w + 2 * pad, h + 2 * pad)) return;
    int bpx = std::max(1, IPX(blur));
    Bitmap* b = ShadowBitmap(IPX(x + w) - IPX(x), IPX(y + h) - IPX(y), IPX(rad), bpx, circle, opacity);
    DrawBitmap(b, (float)(IPX(x) - 2 * bpx), (float)(IPX(y + dy) - 2 * bpx));
}

// ───────────────────────────── Fonts & text ─────────────────────────────

enum Face { kUI, kDisplay, kIcon };
struct Fnt { Face face; float size; int weight; bool italic; };
static Fnt UIF(float size, int weight = FW_NORMAL) { return {kUI, size, weight, false}; }
static Fnt UIItalic(float size) { return {kUI, size, FW_NORMAL, true}; }
static Fnt Disp(float size, int weight = FW_BOLD) { return {kDisplay, size, weight, false}; }
static Fnt DispItalic(float size) { return {kDisplay, size, FW_NORMAL, true}; }
static Fnt IconF(float size) { return {kIcon, size, FW_NORMAL, false}; }

static wstring g_iconFace = L"Segoe Fluent Icons";
static std::unordered_map<string, HFONT> g_fonts;

// Georgia stands in for the Mac's Superclarendon. It has no Thai or Japanese glyphs, so text in
// those scripts uses Segoe UI (which falls back to Leelawadee / Yu Gothic) at the same size.
static bool NeedsFallback(const wstring& s) {
    for (wchar_t c : s)
        if (c > 0x24F && !(c >= 0x2000 && c <= 0x206F) && c != 0x00B7) return true;
    return false;
}

static HFONT GetFont(Fnt f, bool fallback = false) {
    const wchar_t* face = L"Segoe UI";
    int weight = f.weight;
    if (f.face == kIcon) face = g_iconFace.c_str();
    else if (f.face == kDisplay && !fallback) face = L"Georgia";
    else if (weight == FW_SEMIBOLD) face = L"Segoe UI Semibold";
    int px = std::max(1, (int)std::lround(f.size * g_k * (f.face == kDisplay && !fallback ? 0.96f : 1.0f)));
    char key[96];
    snprintf(key, sizeof key, "%d.%d.%d.%d.%d", (int)f.face, px, weight, (int)f.italic, (int)fallback);
    auto it = g_fonts.find(key);
    if (it != g_fonts.end()) return it->second;
    HFONT h = CreateFontW(-px, 0, 0, 0, weight, f.italic, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                          CLEARTYPE_QUALITY, DEFAULT_PITCH, face);
    g_fonts[key] = h;
    return h;
}

static std::unordered_map<wstring, float> g_measured;   // wrapped heights, keyed by text + font + width
static std::unordered_map<wstring, float> g_fitted;     // FitText results: text + font + box -> size

static void ClearFonts() {
    for (auto& [k, f] : g_fonts) DeleteObject(f);
    g_fonts.clear();
    g_measured.clear();
    g_fitted.clear();
}

static HFONT Pick(const wstring& s, Fnt f) { return GetFont(f, f.face == kDisplay && NeedsFallback(s)); }

constexpr UINT kLine = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX;

// Draws `s` in a DIP box. `tracking` adds letter spacing (DIPs), like SwiftUI's .tracking().
static void Text(const wstring& s, float x, float y, float w, float h, Fnt f, DWORD color, UINT fmt = kLine,
                 float tracking = 0) {
    if (s.empty() || !Visible(x, y, w, h)) return;
    R.g->Flush(Gdiplus::FlushIntentionSync);
    SelectObject(R.dc, Pick(s, f));
    SetTextColor(R.dc, CR(color));
    SetBkMode(R.dc, TRANSPARENT);
    int extra = tracking ? IPX(tracking) : 0;
    if (extra) SetTextCharacterExtra(R.dc, extra);
    RECT r{IPX(x), IPX(y), IPX(x + w), IPX(y + h)};
    DrawTextW(R.dc, s.c_str(), (int)s.size(), &r, fmt | DT_NOPREFIX);
    if (extra) SetTextCharacterExtra(R.dc, 0);
}

static float TextWidth(const wstring& s, Fnt f, float tracking = 0) {
    if (s.empty()) return 0;
    HDC dc = R.dc ? R.dc : GetDC(nullptr);
    HGDIOBJ old = SelectObject(dc, Pick(s, f));
    SIZE sz{};
    GetTextExtentPoint32W(dc, s.c_str(), (int)s.size(), &sz);
    SelectObject(dc, old);
    if (!R.dc) ReleaseDC(nullptr, dc);
    return sz.cx / g_k + tracking * s.size();
}

static float LineHeight(Fnt f) {
    HDC dc = R.dc ? R.dc : GetDC(nullptr);
    HGDIOBJ old = SelectObject(dc, GetFont(f));
    TEXTMETRICW tm{};
    GetTextMetricsW(dc, &tm);
    SelectObject(dc, old);
    if (!R.dc) ReleaseDC(nullptr, dc);
    return tm.tmHeight / g_k;
}

// Height of word-wrapped text in a box `w` wide (DIPs).
static float WrappedHeight(const wstring& s, Fnt f, float w) {
    wchar_t tag[64];
    swprintf(tag, 64, L"|%d.%d.%d.%d.%d", (int)f.face, (int)(f.size * 10), f.weight, (int)f.italic, IPX(w));
    wstring key = s + tag;
    auto hit = g_measured.find(key);
    if (hit != g_measured.end()) return hit->second;
    if (g_measured.size() > 4000) g_measured.clear();
    HDC dc = R.dc ? R.dc : GetDC(nullptr);
    HGDIOBJ old = SelectObject(dc, Pick(s, f));
    RECT r{0, 0, IPX(w), 0};
    DrawTextW(dc, s.c_str(), (int)s.size(), &r, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(dc, old);
    if (!R.dc) ReleaseDC(nullptr, dc);
    return g_measured[key] = (r.bottom - r.top) / g_k;
}

// Like SwiftUI's lineLimit + minimumScaleFactor: shrink the font until the text fits.
static Fnt FitTextUncached(const wstring& s, Fnt f, float w, int maxLines, float minScale, float maxHeight);

static Fnt FitText(const wstring& s, Fnt f, float w, int maxLines, float minScale, float maxHeight = 1e9f) {
    wchar_t tag[80];
    swprintf(tag, 80, L"|%d.%d.%d.%d.%d.%d.%d", (int)f.face, (int)(f.size * 10), f.weight, IPX(w), maxLines,
             (int)(minScale * 100), (int)std::min(maxHeight, 1e6f));
    wstring key = s + tag;
    if (auto hit = g_fitted.find(key); hit != g_fitted.end()) { Fnt t = f; t.size = hit->second; return t; }
    if (g_fitted.size() > 2000) g_fitted.clear();
    Fnt t = FitTextUncached(s, f, w, maxLines, minScale, maxHeight);
    g_fitted[key] = t.size;
    return t;
}

static Fnt FitTextUncached(const wstring& s, Fnt f, float w, int maxLines, float minScale, float maxHeight) {
    Fnt t = f;
    for (int i = 0; i < 14; ++i) {
        float lh = LineHeight(t);
        float hgt = WrappedHeight(s, t, w);
        bool wordsFit = true;
        size_t start = 0;
        while (start < s.size() && wordsFit) {   // a single long word must fit on one line
            size_t sp = s.find(L' ', start);
            wstring word = s.substr(start, sp == wstring::npos ? wstring::npos : sp - start);
            if (TextWidth(word, t) > w) wordsFit = false;
            if (sp == wstring::npos) break;
            start = sp + 1;
        }
        if (wordsFit && hgt <= lh * maxLines + 0.5f && hgt <= maxHeight) return t;
        if (t.size <= f.size * minScale) break;
        t.size = std::max(f.size * minScale, t.size * 0.93f);
    }
    return t;
}

// Multi-line text, clipped to `maxLines`; returns the height used.
static float Paragraph(const wstring& s, float x, float y, float w, Fnt f, DWORD color, int maxLines) {
    if (s.empty()) return 0;
    float lh = LineHeight(f);
    float h = std::min(WrappedHeight(s, f, w), lh * maxLines);
    Text(s, x, y, w, h + 1, f, color, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_EDITCONTROL | DT_END_ELLIPSIS);
    return h;
}

static void Icon(const wchar_t* glyph, float cx, float cy, float size, DWORD color) {
    Text(glyph, cx - size, cy - size, 2 * size, 2 * size, IconF(size), color, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

namespace glyph {
const wchar_t home[] = L"\xE80F", search[] = L"\xE721", heart[] = L"\xE006", heartFill[] = L"\xE00B",
              clock[] = L"\xE823", list[] = L"\xE8FD", shuffle[] = L"\xE8B1", repeat[] = L"\xE8EE",
              repeatOne[] = L"\xE8ED", add[] = L"\xE710", more[] = L"\xE712", down[] = L"\xE70D", close[] = L"\xE711",
              edit[] = L"\xE70F", picture[] = L"\xE8B9", grid[] = L"\xE8A9", note[] = L"\xE8D6", left[] = L"\xE76B",
              right[] = L"\xE76C", check[] = L"\xE73E";
}

// ───────────────────────────── Artwork ─────────────────────────────

// Decoded cover art by URL. Downloads run on worker threads (max 6 at once) with a disk cache;
// the UI thread is told through `g_artReady` (a window message), then redraws.
struct ArtSource { Bitmap* bmp = nullptr; bool loading = false, failed = false; ULONGLONG used = 0; };
static std::unordered_map<string, ArtSource> g_art;
static wstring g_artDir;
static HANDLE g_artSlots;   // semaphore
static std::function<void(string url, Bitmap* bmp)> g_artReady;

// Decodes image bytes into a bitmap that owns its pixels (GDI+ stream bitmaps keep the stream).
static Bitmap* DecodeImage(const string& bytes, int maxSide = 0) {
    IStream* s = SHCreateMemStream((const BYTE*)bytes.data(), (UINT)bytes.size());
    if (!s) return nullptr;
    Bitmap* out = nullptr;
    {
        Bitmap src(s);
        if (src.GetLastStatus() == Gdiplus::Ok && src.GetWidth() > 0) {
            int w = src.GetWidth(), h = src.GetHeight();
            if (maxSide && std::max(w, h) > maxSide) {
                float k = (float)maxSide / std::max(w, h);
                w = std::max(1, (int)(w * k));
                h = std::max(1, (int)(h * k));
            }
            out = new Bitmap(w, h, PixelFormat32bppPARGB);
            Gdiplus::Graphics g(out);
            g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
            g.DrawImage(&src, 0, 0, w, h);
        }
    }
    s->Release();
    return out;
}

static Bitmap* LoadImageFile(const wstring& path, int maxSide) {
    string bytes;
    return ReadFileAll(path, bytes) ? DecodeImage(bytes, maxSide) : nullptr;
}

// Returns the art if it's ready; otherwise starts loading it and returns nullptr.
static Bitmap* Art(const string& url) {
    if (url.empty()) return nullptr;
    ArtSource& a = g_art[url];
    a.used = GetTickCount64();
    if (a.bmp || a.loading || a.failed) return a.bmp;
    a.loading = true;
    RunAsync([url] {
        WaitForSingleObject(g_artSlots, INFINITE);
        string bytes;
        bool local = url.rfind("file:", 0) == 0;
        wchar_t name[32];
        swprintf(name, 32, L"%016llx", (unsigned long long)Fnv64(url));
        wstring cached = g_artDir + L"\\" + name;
        if (local) ReadFileAll(W(url.substr(5)), bytes);
        else if (ReadFileAll(cached, bytes) && !bytes.empty()) Touch(cached);
        else if (HttpGet(url, bytes) && !bytes.empty()) WriteFileAll(cached, bytes);
        Bitmap* bmp = bytes.empty() ? nullptr : DecodeImage(bytes, 600);
        ReleaseSemaphore(g_artSlots, 1, nullptr);
        g_artReady(url, bmp);
    });
    return nullptr;
}

static void ArtLoaded(const string& url, Bitmap* bmp) {
    ArtSource& a = g_art[url];
    a.loading = false;
    if (!bmp) { a.failed = true; return; }
    delete a.bmp;
    a.bmp = bmp;
    // Keep at most ~120 decoded covers; drop the least recently drawn.
    if (g_art.size() > 120) {
        vector<std::pair<ULONGLONG, string>> by;
        for (auto& [u, s] : g_art) if (s.bmp) by.push_back({s.used, u});
        std::sort(by.begin(), by.end());
        for (size_t i = 0; i + 100 < by.size(); ++i) {
            auto& s = g_art[by[i].second];
            delete s.bmp;
            s.bmp = nullptr;
        }
    }
}

static void PurgeArt() {
    for (auto& [u, s] : g_art) delete s.bmp;
    g_art.clear();
    g_variants.clear();
    g_shapes.clear();
}

// `src` scaled to fill w×h (centre-cropped) with rounded corners or a circle, antialiased.
static Bitmap* Shaped(Bitmap* src, int w, int h, int rad, bool circle) {
    Bitmap scaled(w, h, PixelFormat32bppPARGB);
    {
        Gdiplus::Graphics g(&scaled);
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        float sw = (float)src->GetWidth(), sh = (float)src->GetHeight();
        float k = std::max(w / sw, h / sh), cw = w / k, ch = h / k;
        Gdiplus::ImageAttributes ia;
        ia.SetWrapMode(Gdiplus::WrapModeTileFlipXY);   // no dark fringe at the edges
        g.DrawImage(src, Gdiplus::Rect(0, 0, w, h), (INT)((sw - cw) / 2), (INT)((sh - ch) / 2), (INT)cw, (INT)ch,
                    Gdiplus::UnitPixel, &ia);
    }
    auto* out = new Bitmap(w, h, PixelFormat32bppPARGB);
    Gdiplus::Graphics g(out);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    Gdiplus::TextureBrush brush(&scaled);
    if (circle) g.FillEllipse(&brush, 0, 0, w, h);
    else {
        Gdiplus::GraphicsPath p;
        RoundPath(p, 0, 0, (float)w, (float)h, (float)rad);
        g.FillPath(&brush, &p);
    }
    return out;
}

static Bitmap* ArtVariant(const string& url, int w, int h, int rad, bool circle) {
    Bitmap* src = Art(url);
    if (!src) return nullptr;
    char tail[64];
    snprintf(tail, sizeof tail, "|%d.%d.%d.%d", w, h, rad, circle);
    string key = url + tail;
    if (Bitmap* hit = g_variants.get(key)) return hit;
    return g_variants.put(key, Shaped(src, w, h, rad, circle));
}

// Square cover art with a placeholder while it loads.
static void Cover(const string& url, float x, float y, float size, float radius = 4) {
    if (!Visible(x, y, size, size)) return;
    int px = IPX(x + size) - IPX(x);
    if (Bitmap* b = ArtVariant(url, px, px, IPX(radius), false)) { DrawBitmap(b, (float)IPX(x), (float)IPX(y)); return; }
    FillRound(x, y, size, size, radius, pal::linen);
    Icon(glyph::note, x + size / 2, y + size / 2, std::max(9.0f, size * 0.22f), pal::muted);
}

// ───────────────────────────── Records ─────────────────────────────

static double g_spinAngle = 0;   // degrees, advanced by the spin timer while music plays

// Grooves, drawn once per size: ~100 rings per record per frame would be the costliest thing on screen.
static Bitmap* Grooves(int px, float labelRatio) {
    char key[48];
    snprintf(key, sizeof key, "gr%d.%d.%d", px, (int)(labelRatio * 100), (int)pal::dark);
    if (Bitmap* hit = g_shapes.get(key)) return hit;
    auto* b = new Bitmap(px, px, PixelFormat32bppPARGB);
    Gdiplus::Graphics g(b);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    float w = (float)px, Rr = w / 2, scale = g_k;
    Gdiplus::SolidBrush vinyl(GC(pal::vinyl));
    g.FillEllipse(&vinyl, 0.0f, 0.0f, w, w);
    float step = std::max(1.6f * scale, w / 140), r = Rr * labelRatio + 3 * scale;
    Gdiplus::Pen groove(GC(pal::groove), step * 0.45f), gap(GC(pal::grooveGap), step * 0.9f);
    for (int i = 0; r < Rr - 2 * scale; ++i, r += step) {
        bool isGap = i % 17 == 9;   // the lighter space between tracks
        g.DrawEllipse(isGap ? &gap : &groove, Rr - r, Rr - r, 2 * r, 2 * r);
    }
    return g_shapes.put(key, b);
}

// Light catching the grooves: an angular gradient that stays still while the record turns.
static Bitmap* SheenBitmap(int px) {
    char key[32];
    snprintf(key, sizeof key, "sheen%d", px);
    if (Bitmap* hit = g_shapes.get(key)) return hit;
    auto* b = new Bitmap(px, px, PixelFormat32bppPARGB);
    Gdiplus::Rect all(0, 0, px, px);
    Gdiplus::BitmapData d;
    static const float stops[][2] = {{0, 0}, {0.11f, 0.13f}, {0.25f, 0}, {0.53f, 0}, {0.64f, 0.09f}, {0.78f, 0}, {1, 0}};
    if (b->LockBits(&all, Gdiplus::ImageLockModeWrite, PixelFormat32bppPARGB, &d) == Gdiplus::Ok) {
        float c = px / 2.0f;
        for (int y = 0; y < px; ++y) {
            auto* row = (uint32_t*)((BYTE*)d.Scan0 + (size_t)y * d.Stride);
            for (int x = 0; x < px; ++x) {
                float dx = x + 0.5f - c, dy = y + 0.5f - c, dist = std::sqrt(dx * dx + dy * dy);
                float cover = std::clamp(c - dist + 0.5f, 0.0f, 1.0f);
                float deg = std::atan2(dy, dx) * 57.29578f;   // clockwise from 3 o'clock (y points down)
                float loc = std::fmod(deg - 20 + 720, 360.0f) / 360.0f, a = 0;
                for (int i = 0; i < 6; ++i)
                    if (loc >= stops[i][0] && loc <= stops[i + 1][0]) {
                        float t = (loc - stops[i][0]) / (stops[i + 1][0] - stops[i][0]);
                        a = stops[i][1] + (stops[i + 1][1] - stops[i][1]) * t;
                        break;
                    }
                uint32_t al = (uint32_t)std::lround(a * cover * 255);
                row[x] = (al << 24) | (al << 16) | (al << 8) | al;   // white, premultiplied
            }
        }
        b->UnlockBits(&d);
    }
    return g_shapes.put(key, b);
}

static void Sheen(float x, float y, float size) {
    if (!Visible(x, y, size, size)) return;
    DrawBitmap(SheenBitmap(IPX(x + size) - IPX(x)), (float)IPX(x), (float)IPX(y));
}

// A vinyl record: grooves, a label (cover art or two colours) turned by `angle`, the spindle hole.
static void Vinyl(float x, float y, float size, const string& art, DWORD c0, DWORD c1, float labelRatio, bool sheen,
                  double angle = 0) {
    if (!Visible(x, y, size, size)) return;
    int px = IPX(x + size) - IPX(x);
    float X = (float)IPX(x), Y = (float)IPX(y), cx = X + px / 2.0f, cy = Y + px / 2.0f;
    DrawBitmap(Grooves(px, labelRatio), X, Y);
    int lpx = std::max(2, (int)std::lround(px * labelRatio * 2));
    Bitmap* label = art.empty() ? nullptr : ArtVariant(art, lpx, lpx, 0, true);
    R.g->TranslateTransform(cx, cy);
    if (angle) R.g->RotateTransform((Gdiplus::REAL)angle);
    if (label) {
        R.g->DrawImage(label, -lpx / 2, -lpx / 2, lpx, lpx);
    } else {
        Gdiplus::SolidBrush top(GC(c0)), bottom(GC(c1));
        float lr = lpx / 2.0f;
        R.g->FillPie(&top, -lr, -lr, 2 * lr, 2 * lr, 180.0f, 180.0f);
        R.g->FillPie(&bottom, -lr, -lr, 2 * lr, 2 * lr, 0.0f, 180.0f);
    }
    R.g->ResetTransform();
    float hole = std::max(1.5f, size * 0.0125f);
    FillCircle(x + size / 2, y + size / 2, hole, pal::spindle);
    if (sheen) Sheen(x, y, size);
}

// The signature motif: the album sleeve in front, its record sliding half out to the right with the
// cover as the label. `slide` (0…1) is the hover animation; `spinning` turns the record.
static void SleeveWithRecord(float x, float y, float size, const string& art, bool spinning, float radius, float slide) {
    float disc = size * 0.96f, ox = x + size * (0.45f + 0.07f * slide), oy = y + size * 0.02f;
    if (!Visible(x - size * 0.2f, y - size * 0.1f, size * 1.7f, size * 1.3f)) return;
    Shadow(ox, oy, disc, disc, 0, size * 0.05f, size * 0.03f, 0.28f, true);
    Vinyl(ox, oy, disc, art, pal::gold, pal::paper, disc < 80 ? 0.32f : 0.2f, false, spinning ? g_spinAngle : 0);
    Sheen(ox, oy, disc);
    Shadow(x, y, size, size, radius, size * 0.06f, size * 0.04f, 0.3f);
    Cover(art, x, y, size, radius);
}

// ───────────────────────────── Sleeves ─────────────────────────────

// A 70s sunset sleeve: a sun sinking into three stripes, title in the corner.
static Bitmap* SleeveArt(int style, int px, int rad) {
    char key[48];
    snprintf(key, sizeof key, "sl%d.%d.%d", style, px, rad);
    if (Bitmap* hit = g_shapes.get(key)) return hit;
    const SleevePal& p = kSleeves[((style % kSleeveCount) + kSleeveCount) % kSleeveCount];
    Bitmap square(px, px, PixelFormat32bppPARGB);
    {
        Gdiplus::Graphics g(&square);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        float w = (float)px, h = (float)px;
        Gdiplus::SolidBrush bg(GC(p.bg)), sun(GC(p.sun)), s1(GC(p.s1)), s2(GC(p.s2));
        g.FillRectangle(&bg, 0.0f, 0.0f, w, h);
        float r = w * 0.24f;
        g.FillEllipse(&sun, w / 2 - r, h * 0.74f - r, 2 * r, 2 * r);
        g.SetSmoothingMode(Gdiplus::SmoothingModeNone);
        struct Band { float a, b; Gdiplus::Brush* br; } bands[] = {
            {0.62f, 0.70f, &s1}, {0.70f, 0.74f, &bg}, {0.74f, 0.82f, &s2}, {0.82f, 0.86f, &bg}, {0.86f, 0.94f, &sun}, {0.94f, 1.0f, &bg}};
        for (auto& b : bands) g.FillRectangle(b.br, 0.0f, std::round(h * b.a), w, std::round(h * b.b) - std::round(h * b.a));
    }
    // Rounded corners via the shaping helper.
    return g_shapes.put(key, Shaped(&square, px, px, rad, false));
}

static void RetroSleeve(float x, float y, float size, int style, const wstring& title, float radius = -1) {
    if (!Visible(x, y, size, size)) return;
    if (radius < 0) radius = std::max(4.0f, size * 0.04f);
    int px = IPX(x + size) - IPX(x);
    DrawBitmap(SleeveArt(style, px, IPX(radius)), (float)IPX(x), (float)IPX(y));
    if (title.empty()) return;
    const SleevePal& p = kSleeves[((style % kSleeveCount) + kSleeveCount) % kSleeveCount];
    float pad = size * 0.08f, w = size - 2 * pad;
    Fnt f = FitText(title, Disp(std::max(11.0f, size * 0.12f), FW_BOLD), w, 3, 0.55f, size * 0.55f);
    Paragraph(title, x + pad, y + pad, w, f, p.ink, 3);
}
