// Hand-curated genre mixes and the artist-matching rules behind them.
// Mirrors mac/Sources/Mosics/Suggestions.swift — keep the two in sync.
#pragma once
#include <windows.h>

#include <string>
#include <vector>

#include "platform.h"
#include "ytmusic.h"

struct Shelf {
    string id, title, subtitle;
    vector<Track> tracks;
};

struct Mix {
    const char* id;
    const char* title;
    const char* subtitle;
    // Each entry: search name, plus extra spellings the catalog might use (romanised / native script).
    vector<vector<string>> artists;
};

static const vector<Mix>& Mixes() {
    static const vector<Mix> mixes = {
        {"thai-indie", "Thai Indie", "Bangkok bedroom pop, warm guitars and late-night synths",
         {{"Phum Viphurit"}, {"Polycat", u8"โพลีแคท"}, {"Television Off"}, {"Safeplanet"}, {"Scrubb", u8"สครับบ์"},
          {"YENA", u8"เยนา"}, {"Gym and Swim"}, {"Plastic Plastic"}, {"Jelly Rocket"}, {"The Toys", u8"เดอะทอยส์"},
          {"Moving and Cut"}, {"Lipta"}, {"Max Jenmana", u8"แม็กซ์ เจนมานะ"}, {"Tilly Birds"}, {"Zweed n' Roll"},
          {"Folk9"}, {"Part Time Musicians"}, {"H 3 F"}, {"Yellow Fang"}, {"Desktop Error"},
          {"Solitude Is Bliss"}, {"Musketeers", u8"มัสคีเทียร์"}, {"Hugo"}, {"Whal & Dolph", "Whal&Dolph"}}},
        {"city-pop", "City Pop", u8"Tokyo after dark, 1978–1989",
         {{"Mariya Takeuchi", u8"竹内まりや"}, {"Miki Matsubara", u8"松原みき"}, {"Tatsuro Yamashita", u8"山下達郎"},
          {"Anri", u8"杏里"}, {"Taeko Onuki", "Taeko Ohnuki", u8"大貫妙子"}, {"Junko Ohashi", u8"大橋純子"},
          {"Hiroshi Sato", u8"佐藤博"}, {"Toshiki Kadomatsu", u8"角松敏生"}, {"Momoko Kikuchi", u8"菊池桃子"},
          {"Junko Yagami", u8"八神純子"}, {"Tomoko Aran", u8"亜蘭知子"}, {"Masayoshi Takanaka", u8"高中正義"},
          {"Omega Tribe", u8"オメガトライブ"}, {"Yurie Kokubu", u8"国分友里恵"}, {"Meiko Nakahara", u8"中原めいこ"},
          {"Hitomi Tohyama", u8"当山ひとみ"}, {"Takako Mamiya", u8"間宮貴子"}, {"EPO"}, {"Akiko Yano", u8"矢野顕子"}}},
        {"japan", "Japan Indie & Anime", "Your Name skies, Tokyo bedrooms, soft guitars",
         {{"RADWIMPS"}, {"Yorushika", u8"ヨルシカ"}, {"Fujii Kaze", u8"藤井 風", u8"藤井風"}, {"Aimyon", u8"あいみょん"},
          {"Vaundy"}, {"Kenshi Yonezu", u8"米津玄師"}, {"Official HIGE DANdism", u8"Official髭男dism"}, {"King Gnu"},
          {"YOASOBI"}, {"Aimer"}, {"Spitz", u8"スピッツ"}, {"back number"}, {"Gen Hoshino", u8"星野源"}, {"Uru"},
          {"Yuuri", u8"優里"}, {"Lamp"}, {"KIRINJI", u8"キリンジ"}, {"never young beach"}, {"Ichiko Aoba", u8"青葉市子"},
          {"Suchmos"}, {"Yogee New Waves"}, {"Nulbarich"}, {"Kirinji"}, {"Mrs. GREEN APPLE"}, {"Ado"},
          {"Joe Hisaishi", u8"久石譲"}}},
        {"britpop", "If You Like Oasis", "Britpop and timeless English guitar songs",
         {{"Oasis"}, {"Blur"}, {"The Verve"}, {"Pulp"}, {"Suede"}, {"The Stone Roses"}, {"The La's"},
          {"Travis"}, {"Stereophonics"}, {"Ocean Colour Scene"}, {"Supergrass"}, {"Manic Street Preachers"},
          {"The Smiths"}, {"The Charlatans"}, {"Kula Shaker"}, {"Cast"}, {"Embrace"}, {"Shed Seven"},
          {"The Beatles"}, {"The Kinks"}, {"Keane"}, {"The Kooks"}}},
    };
    return mixes;
}

// Lowercase, without accents, letters and digits only — so "Taeko Ōnuki" matches "taeko onuki".
static wstring NormName(const string& s) {
    wstring w = W(s);
    if (w.empty()) return w;
    CharLowerBuffW(w.data(), (DWORD)w.size());
    int n = FoldStringW(MAP_COMPOSITE, w.c_str(), (int)w.size(), nullptr, 0);
    wstring dec(n > 0 ? n : 0, 0);
    if (n > 0) FoldStringW(MAP_COMPOSITE, w.c_str(), (int)w.size(), dec.data(), n);
    wstring out;
    for (wchar_t c : n > 0 ? dec : w) if (IsCharAlphaNumericW(c)) out += c;
    return out;
}

static vector<string> SplitAll(vector<string> parts, const char* sep) {
    vector<string> out;
    size_t n = strlen(sep);
    for (auto& p : parts) {
        size_t start = 0, pos;
        while ((pos = p.find(sep, start)) != string::npos) { out.push_back(p.substr(start, pos - start)); start = pos + n; }
        out.push_back(p.substr(start));
    }
    return out;
}

// True if any credited artist ("A & B", "A, B", "A feat. B") is exactly `name`, ignoring case/accents/punctuation.
static bool ArtistMatches(const string& artist, const string& name) {
    wstring n = NormName(name);
    if (n.empty()) return false;
    vector<string> parts{artist};
    for (const char* sep : {" & ", ", ", " feat. ", " ft. ", " x ", " and ", u8"、"}) parts = SplitAll(parts, sep);
    for (auto& p : parts) if (NormName(p) == n) return true;
    return false;
}

static string LowerAscii(string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return s;
}

// Demos, live takes, remixes, covers… (but not remasters).
static bool IsVariant(const string& title) {
    string t = LowerAscii(title);
    bool hit = false;
    for (const char* w : {"demo", "live", "remix", " mix)", "instrumental", "karaoke", "acoustic", "ver.", "version)",
                          "cover", "sped up", "slowed"})
        if (t.find(w) != string::npos) hit = true;
    return (hit && t.find("restored") == string::npos && t.find("remaster") == string::npos) ||
           t.find("demo") != string::npos;
}

static string MainArtist(const Track& t) {
    for (const char* sep : {" & ", ", ", " x ", " feat. "}) {
        size_t p = t.artist.find(sep);
        if (p != string::npos) return t.artist.substr(0, p);
    }
    return t.artist;
}
