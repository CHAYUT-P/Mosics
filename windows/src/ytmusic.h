// YouTube Music InnerTube request bodies + response parsing. Portable; HTTP is done by the caller.
#pragma once
#include <cctype>
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
    // Same artwork at a larger size ("=w120-h120" -> "=w226-h226").
    std::string thumbAt(int px) const {
        size_t w = thumb.find("=w");
        if (w == std::string::npos) return thumb;
        size_t h = thumb.find("-h", w), end = h == std::string::npos ? h : h + 2;
        while (end < thumb.size() && isdigit((unsigned char)thumb[end])) ++end;
        if (end == std::string::npos) return thumb;
        std::string n = std::to_string(px);
        return thumb.substr(0, w) + "=w" + n + "-h" + n + thumb.substr(end);
    }
};

namespace ytmusic {

inline const char* kHost = "music.youtube.com";

inline Json context() {
    return Json::object().set("client", Json::object()
        .set("clientName", "WEB_REMIX").set("clientVersion", "1.20250101.01.00").set("hl", "en").set("gl", "US"));
}

inline std::string searchBody(const std::string& query) {
    return Json::object().set("context", context()).set("query", query)
        .set("params", "EgWKAQIIAWoKEAkQBRAKEAMQBA%3D%3D").dump();   // "Songs" filter
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

// "Artist • Album • 3:45" (radio adds " • 1975" before the time) -> fields.
inline Track makeTrack(std::string id, std::string title, const std::string& byline, std::string thumb) {
    static const std::string sep = " \xE2\x80\xA2 ";   // " • "
    std::vector<std::string> parts;
    size_t start = 0, pos;
    while ((pos = byline.find(sep, start)) != std::string::npos) {
        parts.push_back(byline.substr(start, pos - start));
        start = pos + sep.size();
    }
    parts.push_back(byline.substr(start));
    Track t{std::move(id), std::move(title), "", "", "", std::move(thumb)};
    if (!parts.empty() && isTime(parts.back())) { t.duration = parts.back(); parts.pop_back(); }
    if (parts.size() > 2 && parts.back().size() == 4 && allDigits(parts.back())) parts.pop_back();
    if (!parts.empty()) t.artist = parts[0];
    if (parts.size() > 1) t.album = parts[1];
    return t;
}

inline std::vector<Track> parseSearch(const std::string& body) {
    std::vector<Track> out;
    Json j = Json::parse(body);
    const Json& sections = j["contents"]["tabbedSearchResultsRenderer"]["tabs"][0]["tabRenderer"]["content"]
                            ["sectionListRenderer"]["contents"];
    for (auto& section : sections.items()) {
        for (auto& item : section["musicShelfRenderer"]["contents"].items()) {
            const Json& r = item["musicResponsiveListItemRenderer"];
            const std::string& id = r["playlistItemData"]["videoId"].str();
            if (id.empty()) continue;
            auto col = [&](int i) { return runsText(r["flexColumns"][i]["musicResponsiveListItemFlexColumnRenderer"]["text"]["runs"]); };
            const Json& thumbs = r["thumbnail"]["musicThumbnailRenderer"]["thumbnail"]["thumbnails"];
            out.push_back(makeTrack(id, col(0), col(1), thumbs[int(thumbs.size()) - 1]["url"].str()));
        }
    }
    return out;
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
