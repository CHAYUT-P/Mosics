import Foundation

/// Talks to YouTube Music's public InnerTube API for search and "radio" (autoplay) suggestions.
/// No account, no API key, no video — just JSON metadata.
enum YTMusic {
    private static let endpoint = "https://music.youtube.com/youtubei/v1/"
    private static let context: [String: Any] = [
        "client": ["clientName": "WEB_REMIX", "clientVersion": "1.20250101.01.00", "hl": "en", "gl": "US"]
    ]
    enum Kind: String, CaseIterable, Identifiable {
        case songs = "Songs", videos = "Videos"
        var id: String { rawValue }
        var param: String {
            switch self {
            case .songs: "EgWKAQIIAWoKEAkQBRAKEAMQBA%3D%3D"
            case .videos: "EgWKAQIQAWoKEAkQChAFEAMQBA%3D%3D"   // live takes, covers, uploads not listed as songs
            }
        }
    }

    /// One page of results plus a token for the next page (nil when there are no more).
    struct Page {
        var tracks: [Track]
        var next: String?
    }

    static func search(_ query: String) async throws -> [Track] {
        try await search(query, kind: .songs).tracks
    }

    static func search(_ query: String, kind: Kind) async throws -> Page {
        let json = try await post("search", ["query": query, "params": kind.param])
        let sections = jv(json, "contents", "tabbedSearchResultsRenderer", "tabs", 0, "tabRenderer",
                          "content", "sectionListRenderer", "contents") as? [Any] ?? []
        var page = Page(tracks: [], next: nil)
        for section in sections {
            guard let shelf = jv(section, "musicShelfRenderer") else { continue }
            let p = parseShelf(shelf)
            page.tracks += p.tracks
            page.next = page.next ?? p.next
        }
        return page
    }

    static func more(_ token: String) async throws -> Page {
        let json = try await post("search", [:], query: "&ctoken=\(token)&continuation=\(token)&type=next")
        guard let shelf = jv(json, "continuationContents", "musicShelfContinuation") else { return Page(tracks: [], next: nil) }
        return parseShelf(shelf)
    }

    private static func parseShelf(_ shelf: Any) -> Page {
        var out: [Track] = []
        for item in jv(shelf, "contents") as? [Any] ?? [] {
            guard let r = jv(item, "musicResponsiveListItemRenderer"),
                  let id = jv(r, "playlistItemData", "videoId") as? String else { continue }
            let col = { (i: Int) in runsText(jv(r, "flexColumns", i, "musicResponsiveListItemFlexColumnRenderer", "text", "runs")) }
            let thumbs = jv(r, "thumbnail", "musicThumbnailRenderer", "thumbnail", "thumbnails") as? [Any]
            out.append(makeTrack(id: id, title: col(0), byline: col(1), thumb: jv(thumbs?.last, "url") as? String ?? ""))
        }
        let next = jv(shelf, "continuations", 0, "nextContinuationData", "continuation") as? String
        return Page(tracks: out, next: out.isEmpty ? nil : next)
    }

    // MARK: Artists

    /// An artist to open: the name is enough, the channel id makes it exact.
    struct ArtistRef: Hashable, Codable {
        var name: String
        var browseId: String = ""
        var photo: String = ""
    }

    struct ArtistPage {
        var ref: ArtistRef
        var audience: String      // "33.2M monthly audience"
        var tracks: [Track]       // most played first (up to 100)
        var related: [ArtistRef]  // "Fans might also like"
    }

    static func artist(_ ref: ArtistRef) async throws -> ArtistPage {
        var ref = ref
        var audience = ""
        if ref.browseId.isEmpty {   // find the channel: the first "Artists" result with this exact name
            let json = try await post("search", ["query": ref.name, "params": "EgWKAQIgAWoKEAkQBRAKEAMQBA%3D%3D"])
            let sections = jv(json, "contents", "tabbedSearchResultsRenderer", "tabs", 0, "tabRenderer",
                              "content", "sectionListRenderer", "contents") as? [Any] ?? []
            var found: [(ArtistRef, String)] = []
            for section in sections {
                for item in jv(section, "musicShelfRenderer", "contents") as? [Any] ?? [] {
                    guard let r = jv(item, "musicResponsiveListItemRenderer"),
                          let id = jv(r, "navigationEndpoint", "browseEndpoint", "browseId") as? String else { continue }
                    let thumbs = jv(r, "thumbnail", "musicThumbnailRenderer", "thumbnail", "thumbnails") as? [Any]
                    let sub = runsText(jv(r, "flexColumns", 1, "musicResponsiveListItemFlexColumnRenderer", "text", "runs"))
                    found.append((ArtistRef(name: runsText(jv(r, "flexColumns", 0, "musicResponsiveListItemFlexColumnRenderer", "text", "runs")),
                                            browseId: id, photo: jv(thumbs?.last, "url") as? String ?? ""),
                                  sub.components(separatedBy: " • ").last ?? ""))
                }
            }
            guard let pick = found.first(where: { Suggestions.matches($0.0.name, ref.name) }) ?? found.first else {
                return ArtistPage(ref: ref, audience: "", tracks: [], related: [])
            }
            ref = pick.0
            audience = pick.1
        }
        let page = try await post("browse", ["browseId": ref.browseId])
        if let title = jv(page, "header", "musicImmersiveHeaderRenderer", "title", "runs") ?? jv(page, "header", "musicVisualHeaderRenderer", "title", "runs") {
            let t = runsText(title)
            if !t.isEmpty { ref.name = t }
        }
        let sections = jv(page, "contents", "singleColumnBrowseResultsRenderer", "tabs", 0, "tabRenderer", "content",
                          "sectionListRenderer", "contents") as? [Any] ?? []
        var tracks: [Track] = [], related: [ArtistRef] = [], allSongs: String?
        for section in sections {
            if let shelf = jv(section, "musicShelfRenderer") {   // "Top songs"
                tracks = parseSongRows(jv(shelf, "contents") as? [Any] ?? [])
                allSongs = jv(shelf, "bottomEndpoint", "browseEndpoint", "browseId") as? String
                    ?? jv(shelf, "title", "runs", 0, "navigationEndpoint", "browseEndpoint", "browseId") as? String
            } else if let carousel = jv(section, "musicCarouselShelfRenderer"),
                      runsText(jv(carousel, "header", "musicCarouselShelfBasicHeaderRenderer", "title", "runs")).hasPrefix("Fans") {
                for item in jv(carousel, "contents") as? [Any] ?? [] {
                    guard let r = jv(item, "musicTwoRowItemRenderer"),
                          let id = jv(r, "navigationEndpoint", "browseEndpoint", "browseId") as? String else { continue }
                    let thumbs = jv(r, "thumbnailRenderer", "musicThumbnailRenderer", "thumbnail", "thumbnails") as? [Any]
                    related.append(ArtistRef(name: runsText(jv(r, "title", "runs")), browseId: id,
                                             photo: jv(thumbs?.last, "url") as? String ?? ""))
                }
            }
        }
        // "See all" songs: a playlist of the artist's songs, most played first.
        if let all = allSongs, let list = try? await post("browse", ["browseId": all]) {
            let shelf = jv(list, "contents", "twoColumnBrowseResultsRenderer", "secondaryContents", "sectionListRenderer",
                           "contents", 0, "musicPlaylistShelfRenderer", "contents") as? [Any] ?? []
            let more = parseSongRows(shelf)
            if more.count > tracks.count { tracks = more }
        }
        return ArtistPage(ref: ref, audience: audience, tracks: tracks, related: related)
    }

    /// Song rows on artist pages and playlists: title, artist, (plays), album, with the length in a fixed column.
    private static func parseSongRows(_ items: [Any]) -> [Track] {
        items.compactMap { item in
            guard let r = jv(item, "musicResponsiveListItemRenderer"),
                  let id = jv(r, "playlistItemData", "videoId") as? String else { return nil }
            let cols = (0..<4).map { runsText(jv(r, "flexColumns", $0, "musicResponsiveListItemFlexColumnRenderer", "text", "runs")) }
                .filter { !$0.hasSuffix(" plays") && !$0.hasSuffix(" views") }
            let thumbs = jv(r, "thumbnail", "musicThumbnailRenderer", "thumbnail", "thumbnails") as? [Any]
            let length = runsText(jv(r, "fixedColumns", 0, "musicResponsiveListItemFixedColumnRenderer", "text", "runs"))
            return Track(id: id, title: cols.first ?? "", artist: cols.count > 1 ? cols[1] : "", album: cols.count > 2 ? cols[2] : "",
                         duration: length, thumb: jv(thumbs?.last, "url") as? String ?? "")
        }
    }

    /// Songs similar to `id` — used to keep the music going when the queue runs out.
    static func radio(for id: String) async throws -> [Track] {
        let json = try await post("next", ["videoId": id, "playlistId": "RDAMVM" + id, "isAudioOnly": true])
        let items = jv(json, "contents", "singleColumnMusicWatchNextResultsRenderer", "tabbedRenderer",
                       "watchNextTabbedResultsRenderer", "tabs", 0, "tabRenderer", "content",
                       "musicQueueRenderer", "content", "playlistPanelRenderer", "contents") as? [Any] ?? []
        return items.compactMap { item in
            guard let r = jv(item, "playlistPanelVideoRenderer")
                    ?? jv(item, "playlistPanelVideoWrapperRenderer", "primaryRenderer", "playlistPanelVideoRenderer"),
                  let id = jv(r, "videoId") as? String else { return nil }
            var byline = runsText(jv(r, "longBylineText", "runs"))
            if let len = jv(r, "lengthText", "runs", 0, "text") as? String { byline += " • " + len }
            let thumbs = jv(r, "thumbnail", "thumbnails") as? [Any]
            return makeTrack(id: id, title: runsText(jv(r, "title", "runs")), byline: byline,
                             thumb: jv(thumbs?.last, "url") as? String ?? "")
        }
    }

    // "Artist • Album • 3:45" → parts. Radio bylines also carry a year, which we drop.
    private static func makeTrack(id: String, title: String, byline: String, thumb: String) -> Track {
        var parts = byline.components(separatedBy: " • ").filter { !$0.hasSuffix(" views") && !$0.hasSuffix(" plays") }
        var duration = ""
        if let last = parts.last, last.range(of: #"^\d+(:\d{2})+$"#, options: .regularExpression) != nil {
            duration = parts.removeLast()
        }
        if parts.count > 2, let last = parts.last, last.range(of: #"^\d{4}$"#, options: .regularExpression) != nil {
            parts.removeLast()
        }
        return Track(id: id, title: title, artist: parts.first ?? "", album: parts.count > 1 ? parts[1] : "",
                     duration: duration, thumb: thumb)
    }

    private static func runsText(_ runs: Any?) -> String {
        (runs as? [Any] ?? []).compactMap { jv($0, "text") as? String }.joined()
    }

    private static func post(_ path: String, _ body: [String: Any], query: String = "") async throws -> Any {
        var req = URLRequest(url: URL(string: endpoint + path + "?prettyPrint=false" + query)!)
        req.httpMethod = "POST"
        req.setValue("application/json", forHTTPHeaderField: "Content-Type")
        req.setValue("https://music.youtube.com", forHTTPHeaderField: "Origin")
        req.setValue("Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0 Safari/537.36",
                     forHTTPHeaderField: "User-Agent")
        var payload = body
        payload["context"] = context
        req.httpBody = try JSONSerialization.data(withJSONObject: payload)
        let (data, _) = try await URLSession.shared.data(for: req)
        return try JSONSerialization.jsonObject(with: data)
    }
}

/// Walks loosely-typed JSON: `jv(json, "a", 0, "b")`.
func jv(_ root: Any?, _ path: Any...) -> Any? {
    var cur = root
    for key in path {
        switch key {
        case let k as String: cur = (cur as? [String: Any])?[k]
        case let i as Int:
            guard let a = cur as? [Any], a.indices.contains(i) else { return nil }
            cur = a[i]
        default: return nil
        }
    }
    return cur
}
