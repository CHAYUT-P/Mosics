import AppKit
import SwiftUI

struct HomePage: View {
    @Environment(Suggestions.self) private var suggestions
    @Environment(Library.self) private var library
    @Environment(Player.self) private var player
    @Binding var page: Page

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 28) {
                HStack(alignment: .bottom) {
                    VStack(alignment: .leading, spacing: 4) {
                        Text(greeting).font(.display(40))
                        Text("Pull a record from the crate.").font(.system(size: 14)).foregroundStyle(Retro.muted)
                    }
                    Spacer()
                    Button { page = .search } label: {
                        HStack(spacing: 10) {
                            Image(systemName: "magnifyingglass")
                            Text("Search songs, artists…")
                            Spacer()
                        }
                        .font(.system(size: 14)).foregroundStyle(Retro.muted)
                        .padding(.horizontal, 16).frame(width: 280, height: 42)
                        .background(Capsule().fill(Retro.card)).overlay(Capsule().strokeBorder(Retro.line))
                    }
                    .buttonStyle(.plain)
                }

                NowSpinningCard(openTurntable: { page = .nowPlaying })

                let mixes = suggestions.shelves.filter { $0.id != "recent" }
                VStack(alignment: .leading, spacing: 14) {
                    HStack(alignment: .lastTextBaseline) {
                        Text("Mixes for you").font(.display(22))
                        if suggestions.loading { ProgressView().controlSize(.small).padding(.leading, 6) }
                        Spacer()
                        Button("New mixes") { Task { await suggestions.refresh(library: library, force: true) } }
                            .buttonStyle(.plain).font(.system(size: 13, weight: .semibold)).foregroundStyle(Retro.accentText)
                            .disabled(suggestions.loading)
                    }
                    if mixes.isEmpty {
                        Text(suggestions.loading ? "Digging through the crates…" : "Couldn't load mixes — check your connection.")
                            .font(.displayItalic(15)).foregroundStyle(Retro.muted)
                    } else {
                        ScrollView(.horizontal, showsIndicators: false) {
                            HStack(alignment: .top, spacing: 26) {
                                ForEach(mixes) { shelf in
                                    MixSleeveTile(shelf: shelf) { page = .mix(shelf.id) }
                                }
                            }
                            .padding(.vertical, 6)
                        }
                    }
                }

                if !library.recent.isEmpty {
                    VStack(alignment: .leading, spacing: 12) {
                        HStack(alignment: .lastTextBaseline) {
                            Text("Jump back in").font(.display(22))
                            Spacer()
                            Button("See all") { page = .recent }
                                .buttonStyle(.plain).font(.system(size: 13, weight: .semibold)).foregroundStyle(Retro.accentText)
                        }
                        ScrollView(.horizontal, showsIndicators: false) {
                            HStack(spacing: 22) {
                                ForEach(Array(library.recent.prefix(14).enumerated()), id: \.offset) { i, t in
                                    RecordTile(track: t) { player.play(Array(library.recent), startAt: i) }
                                        .contextMenu { TrackMenu(picked: [t], player: player, library: library) }
                                }
                            }
                            .padding(.vertical, 6)
                        }
                    }
                }
            }
            .padding(.horizontal, 36)
            .padding(.top, 44)
            .padding(.bottom, 30)
        }
        .task { await suggestions.refresh(library: library) }
    }

    private var greeting: String {
        switch Calendar.current.component(.hour, from: Date()) {
        case 5..<12: "Good morning"
        case 12..<17: "Good afternoon"
        case 17..<22: "Good evening"
        default: "Late night spin"
        }
    }
}

/// The current record: sleeve with the disc pulled half out, and a little level meter.
private struct NowSpinningCard: View {
    @Environment(Player.self) private var player
    let openTurntable: () -> Void

    var body: some View {
        HStack(spacing: 30) {
            if let t = player.current {
                Button(action: openTurntable) {
                    SleeveWithRecord(art: t.thumb(at: 544), size: 184, spinning: player.isPlaying && !player.isLoading)
                }
                .buttonStyle(.plain)
                .help("Open now playing")

                VStack(alignment: .leading, spacing: 6) {
                    Text("NOW SPINNING").font(.system(size: 11.5, weight: .bold)).tracking(1.2).foregroundStyle(Retro.accentText)
                    Text(t.title).font(.display(30)).lineLimit(2)
                    HStack(spacing: 0) {
                        ArtistLink(credit: t.artist, font: .system(size: 14), color: Retro.muted).fixedSize()
                        if !t.album.isEmpty { Text(" · " + t.album).font(.system(size: 14)).foregroundStyle(Retro.muted).lineLimit(1) }
                    }
                    HStack(spacing: 10) {
                        Button(action: player.togglePlay) {
                            Label(player.isPlaying ? "Pause" : "Resume", systemImage: player.isPlaying ? "pause.fill" : "play.fill")
                        }
                        .buttonStyle(PillButton())
                        Button("Start radio") { player.startRadio(t) }.buttonStyle(PillButton(kind: .outline))
                    }
                    .padding(.top, 8)
                }
                Spacer(minLength: 0)
                LevelMeter(active: player.isPlaying && !player.isLoading, seed: Int(player.time))
            } else {
                ZStack(alignment: .topLeading) {
                    VinylDisc(size: 148, labelColors: (Retro.gold, Retro.accent)).offset(x: 92, y: 4)
                    RetroSleeve(palette: .home, title: "Mosics", size: 156)
                }
                .frame(width: 250, height: 156, alignment: .topLeading)
                VStack(alignment: .leading, spacing: 6) {
                    Text("THE TURNTABLE IS QUIET").font(.system(size: 11.5, weight: .bold)).tracking(1.2).foregroundStyle(Retro.accentText)
                    Text("Pick a mix below,\nor search for a song.").font(.display(26))
                }
                Spacer(minLength: 0)
            }
        }
        .padding(.horizontal, 28)
        .frame(height: 232)
        .background(Retro.card, in: RoundedRectangle(cornerRadius: 20))
        .overlay(RoundedRectangle(cornerRadius: 20).strokeBorder(Retro.line))
    }
}

/// A small retro level meter that jumps with the playback clock — no timers, no animation frames.
private struct LevelMeter: View {
    let active: Bool
    let seed: Int

    var body: some View {
        HStack(alignment: .bottom, spacing: 5) {
            ForEach(0..<9, id: \.self) { i in
                let h = active ? 18 + Double((seed * 7 + i * 13) % 11) * 5 : 10
                RoundedRectangle(cornerRadius: 3)
                    .fill(i % 3 == 0 ? Retro.gold : i % 3 == 1 ? Retro.accent : Retro.alt)
                    .frame(width: 8, height: h)
            }
        }
        .frame(height: 72, alignment: .bottom)
        .accessibilityHidden(true)
    }
}

/// A mix as a retro sunset sleeve; the record slides out on hover.
private struct MixSleeveTile: View {
    let shelf: Shelf
    let open: () -> Void
    @State private var hover = false

    var body: some View {
        let pal = SleevePalette.forShelf(shelf.id)
        Button(action: open) {
            VStack(alignment: .leading, spacing: 10) {
                ZStack(alignment: .topLeading) {
                    WithArtwork(url: shelf.tracks.first?.thumb(at: 226)) { img in
                        VinylDisc(size: 120, label: img, labelColors: (pal.sun, pal.bg), labelRatio: 0.22)
                    }
                    .shadow(color: Retro.shadow.opacity(0.22), radius: 7, y: 4)
                    .offset(x: hover ? 70 : 46, y: 4)
                    RetroSleeve(palette: pal, title: shelf.title, size: 128)
                        .shadow(color: Retro.shadow.opacity(0.22), radius: 9, y: 5)
                }
                .frame(width: 196, height: 128, alignment: .topLeading)
                VStack(alignment: .leading, spacing: 2) {
                    Text(shelf.title).font(.system(size: 14, weight: .semibold)).lineLimit(1)
                    Text(shelf.subtitle).font(.system(size: 12.5)).foregroundStyle(Retro.muted).lineLimit(2)
                }
                .frame(width: 170, alignment: .leading)
            }
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .onHover { h in withAnimation(.spring(duration: 0.35)) { hover = h } }
    }
}

/// A recently played song as a record, its cover as the label.
private struct RecordTile: View {
    @Environment(Player.self) private var player
    let track: Track
    let play: () -> Void
    @State private var hover = false

    var body: some View {
        Button(action: play) {
            VStack(spacing: 8) {
                WithArtwork(url: track.thumb(at: 226)) { img in
                    VinylDisc(size: 92, label: img, labelRatio: 0.27)
                }
                .rotationEffect(.degrees(hover ? 24 : 0))
                .shadow(color: Retro.shadow.opacity(0.22), radius: 7, y: 4)
                .overlay {
                    if hover {
                        Image(systemName: "play.fill").font(.system(size: 14)).foregroundStyle(Retro.onAccent)
                            .frame(width: 34, height: 34).background(Circle().fill(Retro.accent))
                    }
                }
                Text(track.title).font(.system(size: 12.5, weight: .semibold)).lineLimit(1).frame(width: 96)
                    .foregroundStyle(player.current?.id == track.id ? Retro.accentText : Retro.ink)
            }
        }
        .buttonStyle(.plain)
        .help("\(track.title) — \(track.artist)")
        .onHover { h in withAnimation(.easeOut(duration: 0.3)) { hover = h } }
    }
}

/// A mix opened as an LP, with a button to keep it.
struct MixPage: View {
    @Environment(Suggestions.self) private var suggestions
    @Environment(Library.self) private var library
    @Environment(Player.self) private var player
    let id: String
    @Binding var page: Page

    var body: some View {
        if let shelf = suggestions.shelves.first(where: { $0.id == id }) {
            let total = shelf.tracks.reduce(0) { $0 + $1.seconds }
            LPTrackList(tracks: shelf.tracks, identity: "mix-\(shelf.id)") {
                LPHeader(palette: .forShelf(shelf.id), sleeve: AnyView(RetroSleeve(palette: .forShelf(shelf.id), title: shelf.title, size: 200)),
                         kicker: "Mix · made for you", title: shelf.title,
                         subtitle: shelf.subtitle, meta: "\(shelf.tracks.count) songs · \(formatLength(total))") {
                    Button { player.shuffle = false; player.play(shelf.tracks) } label: { Label("Play", systemImage: "play.fill") }
                        .buttonStyle(PillButton())
                    Button { player.shuffle = true; player.play(shelf.tracks, startAt: Int.random(in: 0..<max(1, shelf.tracks.count))) } label: {
                        Image(systemName: "shuffle")
                    }
                    .buttonStyle(CircleButton()).help("Shuffle")
                    Button("Save to your records") {
                        let pid = library.newPlaylist(shelf.title)
                        shelf.tracks.forEach { library.add($0, to: pid) }
                        page = .playlist(pid)
                    }
                    .buttonStyle(PillButton(kind: .outline))
                }
            }
        } else {
            ContentUnavailableView("This mix has been refreshed", systemImage: "opticaldisc",
                                   description: Text("Head back Home for today's mixes."))
        }
    }
}
