// YouTube Music InnerTube request bodies + response parsing. Portable; HTTP is done by the caller.
// Mirrors mac/Sources/Mosics/YTMusic.swift.
#pragma once
#include <cctype>
#include <cstring>
#include <string>
#include <vector>
#include "json.h"

struct Track {
    std::string id, title, artist, album, duration, thumb;

    Json toJson() const {
        return Json::object().set("id", id).set("title", title).set("artist", artist)
            .set("album", album).set("duration", duration).set("thumb", thumb);
    }
    static Track fromJson(const Json& j) {
        return {j["id"].str(), j["title"].str(), j["artist"].str(), j["album"].str(), j["duration"].str(), j["thumb"].str()};
    }
    // Same artwork at another size (YouTube Music thumbnails are resizable via "=wN-hN").
    std::string thumbAt(int px) const {
        size_t w = thumb.find("=w");
        if (w == std::string::npos) return thumb;
        size_t h = thumb.find("-h", w);
        if (h == std::string::npos) return thumb;
        size_t end = h + 2;
        while (end < thumb.size() && isdigit((unsigned char)thumb[end])) ++end;
        std::string n = std::to_string(px);
        return thumb.substr(0, w) + "=w" + n + "-h" + n + thumb.substr(end);
    }
    double seconds() const {
        double total = 0, part = 0;
        for (char c : duration) {
            if (c == ':') { total = total * 60 + part; part = 0; }
            else if (c >= '0' && c <= '9') part = part * 10 + (c - '0');
        }
        return total * 60 + part;
    }
};

namespace ytmusic {

enum class Kind { Songs, Videos };

// One page of results plus a token for the next page (empty when there are no more).
struct Page {
    std::vector<Track> tracks;
    std::string next;
};

inline Json context() {
    return Json::object().set("client", Json::object()
        .set("clientName", "WEB_REMIX").set("clientVersion", "1.20250101.01.00").set("hl", "en").set("gl", "US"));
}

inline std::string searchBody(const std::string& query, Kind kind = Kind::Songs) {
    // "Videos" finds live takes, covers and uploads that aren't listed as songs.
    const char* param = kind == Kind::Songs ? "EgWKAQIIAWoKEAkQBRAKEAMQBA%3D%3D" : "EgWKAQIQAWoKEAkQChAFEAMQBA%3D%3D";
    return Json::object().set("context", context()).set("query", query).set("params", param).dump();
}

inline std::string moreBody() { return Json::object().set("context", context()).dump(); }
inline std::string moreQuery(const std::string& token) {
    return "&ctoken=" + token + "&continuation=" + token + "&type=next";
}

inline std::string radioBody(const std::string& id) {
    return Json::object().set("context", context()).set("videoId", id)
        .set("playlistId", "RDAMVM" + id).set("isAudioOnly", true).dump();
}

inline std::string runsText(const Json& runs) {
    std::string s;
    for (auto& r : runs.items()) s += r["text"].str();
    return s;
}

inline bool allDigits(const std::string& s) {
    for (char c : s) if (!isdigit((unsigned char)c)) return false;
    return !s.empty();
}
inline bool isTime(const std::string& s) {   // "3:45", "1:02:03"
    size_t colon = s.find(':');
    if (colon == std::string::npos || !allDigits(s.substr(0, colon))) return false;
    for (size_t p = colon; p < s.size(); p += 3)
        if (s[p] != ':' || p + 3 > s.size() || !allDigits(s.substr(p + 1, 2))) return false;
    return true;
}
inline bool endsWith(const std::string& s, const char* suffix) {
    size_t n = strlen(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

// "Artist • Album • 3:45" (radio adds " • 1975" before the time) -> fields.
inline Track makeTrack(std::string id, std::string title, const std::string& byline, std::string thumb) {
    static const std::string sep = " \xE2\x80\xA2 ";   // " • "
    std::vector<std::string> parts;
    size_t start = 0, pos;
    for (;;) {
        pos = byline.find(sep, start);
        std::string part = byline.substr(start, pos == std::string::npos ? std::string::npos : pos - start);
        if (!endsWith(part, " views") && !endsWith(part, " plays")) parts.push_back(part);
        if (pos == std::string::npos) break;
        start = pos + sep.size();
    }
    Track t{std::move(id), std::move(title), "", "", "", std::move(thumb)};
    if (!parts.empty() && isTime(parts.back())) { t.duration = parts.back(); parts.pop_back(); }
    if (parts.size() > 2 && parts.back().size() == 4 && allDigits(parts.back())) parts.pop_back();
    if (!parts.empty()) t.artist = parts[0];
    if (parts.size() > 1) t.album = parts[1];
    return t;
}

inline Page parseShelf(const Json& shelf) {
    Page page;
    for (auto& item : shelf["contents"].items()) {
        const Json& r = item["musicResponsiveListItemRenderer"];
        const std::string& id = r["playlistItemData"]["videoId"].str();
        if (id.empty()) continue;
        auto col = [&](int i) { return runsText(r["flexColumns"][i]["musicResponsiveListItemFlexColumnRenderer"]["text"]["runs"]); };
        const Json& thumbs = r["thumbnail"]["musicThumbnailRenderer"]["thumbnail"]["thumbnails"];
        page.tracks.push_back(makeTrack(id, col(0), col(1), thumbs[int(thumbs.size()) - 1]["url"].str()));
    }
    if (!page.tracks.empty()) page.next = shelf["continuations"][0]["nextContinuationData"]["continuation"].str();
    return page;
}

inline Page parseSearch(const std::string& body) {
    Page page;
    Json j = Json::parse(body);
    const Json& sections = j["contents"]["tabbedSearchResultsRenderer"]["tabs"][0]["tabRenderer"]["content"]
                            ["sectionListRenderer"]["contents"];
    for (auto& section : sections.items()) {
        const Json& shelf = section["musicShelfRenderer"];
        if (shelf.isNull()) continue;
        Page p = parseShelf(shelf);
        page.tracks.insert(page.tracks.end(), p.tracks.begin(), p.tracks.end());
        if (page.next.empty()) page.next = p.next;
    }
    return page;
}

inline Page parseMore(const std::string& body) {
    Json j = Json::parse(body);
    const Json& shelf = j["continuationContents"]["musicShelfContinuation"];
    return shelf.isNull() ? Page{} : parseShelf(shelf);
}

// ───── Artists ─────

struct ArtistRef { std::string name, browseId, photo; };

struct ArtistInfo {
    ArtistRef ref;
    std::string audience;            // "33.2M monthly audience"
    std::vector<Track> tracks;       // most played first (up to 100)
    std::vector<ArtistRef> related;  // "Fans might also like"
    std::string allSongs;            // browse id of the "See all" songs playlist
};

inline std::string artistSearchBody(const std::string& name) {
    return Json::object().set("context", context()).set("query", name).set("params", "EgWKAQIgAWoKEAkQBRAKEAMQBA%3D%3D").dump();
}
inline std::string browseBody(const std::string& id) { return Json::object().set("context", context()).set("browseId", id).dump(); }

// "Artists" search results, with their audience line ("Artist • 33.2M monthly audience" → the last part).
inline std::vector<std::pair<ArtistRef, std::string>> parseArtistSearch(const std::string& body) {
    std::vector<std::pair<ArtistRef, std::string>> out;
    Json j = Json::parse(body);
    const Json& sections = j["contents"]["tabbedSearchResultsRenderer"]["tabs"][0]["tabRenderer"]["content"]
                            ["sectionListRenderer"]["contents"];
    for (auto& section : sections.items())
        for (auto& item : section["musicShelfRenderer"]["contents"].items()) {
            const Json& r = item["musicResponsiveListItemRenderer"];
            const std::string& id = r["navigationEndpoint"]["browseEndpoint"]["browseId"].str();
            if (id.empty()) continue;
            const Json& thumbs = r["thumbnail"]["musicThumbnailRenderer"]["thumbnail"]["thumbnails"];
            std::string sub = runsText(r["flexColumns"][1]["musicResponsiveListItemFlexColumnRenderer"]["text"]["runs"]);
            size_t dot = sub.rfind(" \xE2\x80\xA2 ");
            out.push_back({{runsText(r["flexColumns"][0]["musicResponsiveListItemFlexColumnRenderer"]["text"]["runs"]), id,
                            thumbs[int(thumbs.size()) - 1]["url"].str()},
                           dot == std::string::npos ? sub : sub.substr(dot + 5)});
        }
    return out;
}

// Song rows on artist pages and playlists: title, artist, (plays), album; the length is a fixed column.
inline std::vector<Track> parseSongRows(const Json& items) {
    std::vector<Track> out;
    for (auto& item : items.items()) {
        const Json& r = item["musicResponsiveListItemRenderer"];
        const std::string& id = r["playlistItemData"]["videoId"].str();
        if (id.empty()) continue;
        std::vector<std::string> cols;
        for (int i = 0; i < 4; ++i) {
            std::string c = runsText(r["flexColumns"][i]["musicResponsiveListItemFlexColumnRenderer"]["text"]["runs"]);
            if (!endsWith(c, " plays") && !endsWith(c, " views")) cols.push_back(c);
        }
        const Json& thumbs = r["thumbnail"]["musicThumbnailRenderer"]["thumbnail"]["thumbnails"];
        Track t{id, cols.size() > 0 ? cols[0] : "", cols.size() > 1 ? cols[1] : "", cols.size() > 2 ? cols[2] : "",
                runsText(r["fixedColumns"][0]["musicResponsiveListItemFixedColumnRenderer"]["text"]["runs"]),
                thumbs[int(thumbs.size()) - 1]["url"].str()};
        out.push_back(std::move(t));
    }
    return out;
}

// The artist's page: name, top songs, the "See all" playlist and "Fans might also like".
inline void parseArtistBrowse(const std::string& body, ArtistInfo& info) {
    Json j = Json::parse(body);
    std::string title = runsText(j["header"]["musicImmersiveHeaderRenderer"]["title"]["runs"]);
    if (title.empty()) title = runsText(j["header"]["musicVisualHeaderRenderer"]["title"]["runs"]);
    if (!title.empty()) info.ref.name = title;
    const Json& sections = j["contents"]["singleColumnBrowseResultsRenderer"]["tabs"][0]["tabRenderer"]["content"]
                            ["sectionListRenderer"]["contents"];
    for (auto& section : sections.items()) {
        const Json& shelf = section["musicShelfRenderer"];
        if (!shelf.isNull()) {
            info.tracks = parseSongRows(shelf["contents"]);
            info.allSongs = shelf["bottomEndpoint"]["browseEndpoint"]["browseId"].str();
            if (info.allSongs.empty()) info.allSongs = shelf["title"]["runs"][0]["navigationEndpoint"]["browseEndpoint"]["browseId"].str();
            continue;
        }
        const Json& carousel = section["musicCarouselShelfRenderer"];
        if (runsText(carousel["header"]["musicCarouselShelfBasicHeaderRenderer"]["title"]["runs"]).rfind("Fans", 0) != 0) continue;
        for (auto& item : carousel["contents"].items()) {
            const Json& r = item["musicTwoRowItemRenderer"];
            const std::string& id = r["navigationEndpoint"]["browseEndpoint"]["browseId"].str();
            if (id.empty()) continue;
            const Json& thumbs = r["thumbnailRenderer"]["musicThumbnailRenderer"]["thumbnail"]["thumbnails"];
            info.related.push_back({runsText(r["title"]["runs"]), id, thumbs[int(thumbs.size()) - 1]["url"].str()});
        }
    }
}

inline std::vector<Track> parsePlaylistSongs(const std::string& body) {
    Json j = Json::parse(body);
    return parseSongRows(j["contents"]["twoColumnBrowseResultsRenderer"]["secondaryContents"]["sectionListRenderer"]
                          ["contents"][0]["musicPlaylistShelfRenderer"]["contents"]);
}

// Any YouTube Music image URL at another size ("=w120-h120-p-l90-rj" → "=w544-h544-p-l90-rj").
inline std::string resizedArt(const std::string& url, int px) {
    size_t w = url.find("=w");
    if (w == std::string::npos) return url;
    size_t h = url.find("-h", w);
    if (h == std::string::npos) return url;
    size_t end = h + 2;
    while (end < url.size() && isdigit((unsigned char)url[end])) ++end;
    std::string n = std::to_string(px);
    return url.substr(0, w) + "=w" + n + "-h" + n + url.substr(end);
}

inline std::vector<Track> parseRadio(const std::string& body) {
    std::vector<Track> out;
    Json j = Json::parse(body);
    const Json& items = j["contents"]["singleColumnMusicWatchNextResultsRenderer"]["tabbedRenderer"]
                         ["watchNextTabbedResultsRenderer"]["tabs"][0]["tabRenderer"]["content"]
                         ["musicQueueRenderer"]["content"]["playlistPanelRenderer"]["contents"];
    for (auto& item : items.items()) {
        const Json* r = &item["playlistPanelVideoRenderer"];
        if (r->isNull()) r = &item["playlistPanelVideoWrapperRenderer"]["primaryRenderer"]["playlistPanelVideoRenderer"];
        const std::string& id = (*r)["videoId"].str();
        if (id.empty()) continue;
        std::string byline = runsText((*r)["longBylineText"]["runs"]);
        const std::string& len = (*r)["lengthText"]["runs"][0]["text"].str();
        if (!len.empty()) byline += " \xE2\x80\xA2 " + len;
        const Json& thumbs = (*r)["thumbnail"]["thumbnails"];
        out.push_back(makeTrack(id, runsText((*r)["title"]["runs"]), byline, thumbs[int(thumbs.size()) - 1]["url"].str()));
    }
    return out;
}

}  // namespace ytmusic
