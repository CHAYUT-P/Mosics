import AppKit
import Foundation
import Observation

/// Liked songs, playlists and history, saved as one small JSON file.
@Observable @MainActor
final class Library {
    var liked: [Track] = [] { didSet { save() } }
    var playlists: [Playlist] = [] { didSet { save() } }
    var recent: [Track] = [] { didSet { save() } }

    private struct Stored: Codable {
        var liked: [Track]
        var playlists: [Playlist]
        var recent: [Track]
    }

    private let file: URL = {
        let dir = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("Mosics", isDirectory: true)
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        return dir.appendingPathComponent("library.json")
    }()
    private var loading = true

    init() {
        if let data = try? Data(contentsOf: file), let s = try? JSONDecoder().decode(Stored.self, from: data) {
            liked = s.liked
            playlists = s.playlists
            recent = s.recent
        }
        loading = false
    }

    private func save() {
        guard !loading else { return }
        let s = Stored(liked: liked, playlists: playlists, recent: recent)
        if let data = try? JSONEncoder().encode(s) { try? data.write(to: file, options: .atomic) }
    }

    func isLiked(_ t: Track) -> Bool { liked.contains { $0.id == t.id } }

    func toggleLike(_ t: Track) {
        if let i = liked.firstIndex(where: { $0.id == t.id }) { liked.remove(at: i) } else { liked.insert(t, at: 0) }
    }

    func addRecent(_ t: Track) {
        recent.removeAll { $0.id == t.id }
        recent.insert(t, at: 0)
        if recent.count > 100 { recent.removeLast(recent.count - 100) }
    }

    @discardableResult
    func newPlaylist(_ name: String) -> Playlist.ID {
        let p = Playlist(name: name.isEmpty ? "Playlist \(playlists.count + 1)" : name)
        playlists.append(p)
        return p.id
    }

    func add(_ t: Track, to id: Playlist.ID) {
        guard let i = playlists.firstIndex(where: { $0.id == id }),
              !playlists[i].tracks.contains(where: { $0.id == t.id }) else { return }
        playlists[i].tracks.append(t)
    }

    func remove(at offsets: IndexSet, from id: Playlist.ID) {
        guard let i = playlists.firstIndex(where: { $0.id == id }) else { return }
        playlists[i].tracks.remove(atOffsets: offsets)
    }

    func move(from: IndexSet, to: Int, in id: Playlist.ID) {
        guard let i = playlists.firstIndex(where: { $0.id == id }) else { return }
        playlists[i].tracks.move(fromOffsets: from, toOffset: to)
    }

    func rename(_ id: Playlist.ID, to name: String) {
        guard let i = playlists.firstIndex(where: { $0.id == id }), !name.isEmpty else { return }
        playlists[i].name = name
    }

    func deletePlaylist(_ id: Playlist.ID) {
        if case .image(let file)? = playlists.first(where: { $0.id == id })?.cover {
            try? FileManager.default.removeItem(at: Self.coverURL(file))
        }
        playlists.removeAll { $0.id == id }
    }

    // MARK: Covers

    nonisolated static let coversDir: URL = {
        let dir = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("Mosics/covers", isDirectory: true)
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        return dir
    }()

    nonisolated static func coverURL(_ file: String) -> URL { coversDir.appendingPathComponent(file) }

    func setCover(_ cover: PlaylistCover?, for id: Playlist.ID) {
        guard let i = playlists.firstIndex(where: { $0.id == id }) else { return }
        if case .image(let old)? = playlists[i].cover, cover != playlists[i].cover {
            try? FileManager.default.removeItem(at: Self.coverURL(old))
        }
        playlists[i].cover = cover
    }

    /// Copies a picture in as the playlist cover: square-cropped to 600 px, saved as a small JPEG.
    @discardableResult
    func importCover(from url: URL, for id: Playlist.ID) -> Bool {
        guard let src = NSImage(contentsOf: url), let cg = src.cgImage(forProposedRect: nil, context: nil, hints: nil) else { return false }
        let side = min(cg.width, cg.height)
        let crop = CGRect(x: (cg.width - side) / 2, y: (cg.height - side) / 2, width: side, height: side)
        guard let square = cg.cropping(to: crop) else { return false }
        let out = 600
        guard let ctx = CGContext(data: nil, width: out, height: out, bitsPerComponent: 8, bytesPerRow: 0,
                                  space: CGColorSpaceCreateDeviceRGB(), bitmapInfo: CGImageAlphaInfo.noneSkipLast.rawValue) else { return false }
        ctx.interpolationQuality = .high
        ctx.draw(square, in: CGRect(x: 0, y: 0, width: out, height: out))
        guard let scaled = ctx.makeImage(),
              let jpeg = NSBitmapImageRep(cgImage: scaled).representation(using: .jpeg, properties: [.compressionFactor: 0.85])
        else { return false }
        let file = "\(id.uuidString)-\(Int(Date().timeIntervalSince1970)).jpg"
        guard (try? jpeg.write(to: Self.coverURL(file), options: .atomic)) != nil else { return false }
        setCover(.image(file), for: id)
        return true
    }
}
