import Foundation

struct Track: Codable, Identifiable, Hashable {
    let id: String          // YouTube videoId
    var title: String
    var artist: String
    var album: String
    var duration: String    // "3:45"
    var thumb: String       // artwork URL

    /// Same artwork at another size (YouTube Music thumbnails are resizable via "=wN-hN").
    func thumb(at px: Int) -> URL? {
        URL(string: thumb.replacingOccurrences(of: #"=w\d+-h\d+"#, with: "=w\(px)-h\(px)", options: .regularExpression))
    }
    var bigThumb: URL? { thumb(at: 544) }
    var smallThumb: URL? { URL(string: thumb) }

    var seconds: Double {
        duration.split(separator: ":").reduce(0) { $0 * 60 + (Double($1) ?? 0) }
    }
}

struct Playlist: Codable, Identifiable, Hashable {
    var id = UUID()
    var name: String
    var tracks: [Track] = []
    var cover: PlaylistCover? = nil   // nil = automatic sleeve
}

/// How a playlist's sleeve looks.
enum PlaylistCover: Codable, Hashable {
    case style(Int)       // one of the theme's sleeve designs
    case artwork          // the playlist's own song covers
    case image(String)    // a picture the user chose, saved in Application Support/Mosics/covers
}

func formatTime(_ t: Double) -> String {
    guard t.isFinite, t >= 0 else { return "0:00" }
    let s = Int(t)
    return s >= 3600 ? String(format: "%d:%02d:%02d", s / 3600, s / 60 % 60, s % 60)
                     : String(format: "%d:%02d", s / 60, s % 60)
}
