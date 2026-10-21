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
