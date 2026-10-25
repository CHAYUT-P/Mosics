import SwiftUI

/// Any YouTube Music image URL at another size ("=w120-h120…" → "=w544-h544…").
func artURL(_ url: String, _ px: Int) -> URL? {
    URL(string: url.replacingOccurrences(of: #"=w\d+-h\d+"#, with: "=w\(px)-h\(px)", options: .regularExpression))
}

/// Artist pages already loaded this session, so going back is instant.
@MainActor
enum ArtistCache {
    static var pages: [YTMusic.ArtistRef: YTMusic.ArtistPage] = [:]
}

/// An artist as an LP: their photo on the sleeve, their songs (most played first), and artists fans also like.
struct ArtistView: View {
    @Environment(Player.self) private var player
    let ref: YTMusic.ArtistRef
    @State private var page: YTMusic.ArtistPage?
    @State private var failed = false

    var body: some View {
        let tracks = page?.tracks ?? []
        let name = page?.ref.name ?? ref.name
        let photo = page?.ref.photo ?? ref.photo
        LPTrackList(tracks: tracks, identity: "artist-\(ref.name)-\(ref.browseId)",
                    emptyMessage: page == nil ? (failed ? "Couldn't load this artist — check your connection." : "Pulling their records…")
                                              : "No songs found for this artist.") {
            VStack(alignment: .leading, spacing: 30) {
                LPHeader(palette: .forName(name), labelArt: tracks.first?.thumb(at: 226),
                         sleeve: AnyView(ArtistSleeve(name: name, photo: photo, size: 200)),
                         kicker: "Artist", title: name, meta: meta) {
                    Button { player.shuffle = false; player.play(tracks) } label: { Label("Play", systemImage: "play.fill") }
                        .buttonStyle(PillButton())
                        .disabled(tracks.isEmpty)
                    Button { player.shuffle = true; player.play(tracks, startAt: Int.random(in: 0..<max(1, tracks.count))) } label: {
                        Image(systemName: "shuffle")
                    }
                    .buttonStyle(CircleButton())
                    .disabled(tracks.isEmpty)
                    .help("Shuffle")
                    Button("Artist radio") { if let first = tracks.first { player.startRadio(first) } }
                        .buttonStyle(PillButton(kind: .outline))
                        .disabled(tracks.isEmpty)
                }
                if let related = page?.related, !related.isEmpty {
                    VStack(alignment: .leading, spacing: 12) {
                        Text("Fans might also like").font(.display(22))
                        ScrollView(.horizontal, showsIndicators: false) {
                            HStack(alignment: .top, spacing: 22) {
                                ForEach(related, id: \.self) { RelatedArtistTile(artist: $0) }
                            }
                            .padding(.vertical, 6)
                        }
                    }
                }
                if !tracks.isEmpty { Text("Songs").font(.display(22)) }
            }
        }
        .task(id: ref) { await load() }
    }

    private var meta: String {
        guard let page else { return failed ? "" : "Loading…" }
        let total = page.tracks.reduce(0) { $0 + $1.seconds }
        let songs = "\(page.tracks.count) song\(page.tracks.count == 1 ? "" : "s") · \(formatLength(total))"
        return page.audience.isEmpty ? songs : page.audience + " · " + songs
    }

    private func load() async {
        if let hit = ArtistCache.pages[ref] { page = hit; return }
        page = nil
        failed = false
        do {
            let loaded = try await YTMusic.artist(ref)
            ArtistCache.pages[ref] = loaded
            page = loaded
        } catch {
            failed = true
        }
    }
}

/// The artist's photo as the sleeve (a retro sleeve with their name while it loads or if there's none).
struct ArtistSleeve: View {
    let name: String
    let photo: String
    var size: CGFloat

    var body: some View {
        if photo.isEmpty {
            RetroSleeve(palette: .forName(name), title: name, size: size)
        } else {
            Cover(url: artURL(photo, 544), size: size, radius: max(4, size * 0.04))
        }
    }
}

/// A related artist as a picture disc: their photo is the label.
private struct RelatedArtistTile: View {
    let artist: YTMusic.ArtistRef
    @State private var hover = false

    var body: some View {
        Button { WindowState.shared.page = .artist(artist) } label: {
            VStack(spacing: 8) {
                WithArtwork(url: artURL(artist.photo, 226)) { img in
                    VinylDisc(size: 92, label: img, labelColors: (Retro.gold, Retro.paper), labelRatio: 0.36)
                }
                .rotationEffect(.degrees(hover ? 24 : 0))
                .shadow(color: Retro.shadow.opacity(0.22), radius: 7, y: 4)
                Text(artist.name).font(.system(size: 12.5, weight: .semibold)).lineLimit(1).frame(width: 96)
            }
        }
        .buttonStyle(.plain)
        .help(artist.name)
        .onHover { h in withAnimation(.easeOut(duration: 0.3)) { hover = h } }
    }
}
