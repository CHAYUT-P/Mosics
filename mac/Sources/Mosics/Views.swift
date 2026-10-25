import AppKit
import SwiftUI

enum Page: Hashable {
    case home, search, recent, liked, queue, nowPlaying
    case playlist(UUID)
    case mix(String)
    case artist(YTMusic.ArtistRef)
}

/// Opens the page of the first credited artist ("A & B feat. C" → A).
@MainActor func openArtist(_ credit: String) {
    let name = Suggestions.mainArtist(credit)
    guard !name.isEmpty else { return }
    WindowState.shared.page = .artist(YTMusic.ArtistRef(name: name))
}

struct ContentView: View {
    @Environment(Library.self) private var library
    @Bindable private var window = WindowState.shared
    private var page: Page {
        get { window.page }
        nonmutating set { window.page = newValue }
    }
    @State private var renaming: Playlist.ID?
    @State private var newName = ""
    @State private var editingCover: Playlist.ID?

    var body: some View {
        if WindowState.shared.isOpen {
            app
        } else {
            Color.clear   // window closed: keep only the player running
        }
    }

    private var app: some View {
        ZStack {
            VStack(spacing: 0) {
                HStack(spacing: 0) {
                    Sidebar(page: $window.page, rename: { id, name in newName = name; renaming = id },
                            editCover: { editingCover = $0 })
                    Rectangle().fill(Retro.line).frame(width: 1)
                    content
                        .frame(maxWidth: .infinity, maxHeight: .infinity)
                        .background(Retro.paper)
                }
                PlayerBar(page: $window.page)
            }
            if page == .nowPlaying {
                NowPlayingPage(close: { page = .home })
                    .transition(.move(edge: .bottom).combined(with: .opacity))
                    .zIndex(1)
            }
        }
        .animation(.easeInOut(duration: 0.28), value: page == .nowPlaying)
        .background(Retro.paper)
        .ignoresSafeArea()
        .foregroundStyle(Retro.ink)
        .tint(Retro.accent)
        .preferredColorScheme(ThemeStore.shared.theme.isDark ? .dark : .light)
        .sheet(isPresented: Binding(get: { editingCover != nil }, set: { if !$0 { editingCover = nil } })) {
            if let id = editingCover { CoverPicker(playlistID: id) }
        }
        .alert("Rename Playlist", isPresented: Binding(get: { renaming != nil }, set: { if !$0 { renaming = nil } })) {
            TextField("Name", text: $newName)
            Button("Rename") { if let id = renaming { library.rename(id, to: newName) } }
            Button("Cancel", role: .cancel) {}
        }
    }

    @ViewBuilder private var content: some View {
        switch page {
        case .home, .nowPlaying: HomePage(page: $window.page)
        case .search: SearchPage()
        case .recent:
            CollectionPage(title: "Recently Played", kicker: "History", palette: .recent,
                           sleeve: AnyView(RetroSleeve(palette: .recent, title: "Recent", size: 200)), tracks: library.recent)
        case .liked:
            CollectionPage(title: "Liked Songs", kicker: "Your favourites", palette: .liked,
                           sleeve: AnyView(RetroSleeve(palette: .liked, title: "Liked", size: 200)), tracks: library.liked)
        case .queue: QueuePage()
        case .playlist(let id):
            if let p = library.playlists.first(where: { $0.id == id }) {
                CollectionPage(title: p.name, kicker: "Playlist", palette: p.palette, labelArt: p.labelArt,
                               sleeve: AnyView(PlaylistSleeve(playlist: p, size: 200)),
                               tracks: p.tracks, kind: .playlist(id),
                               onRename: { newName = p.name; renaming = id },
                               onEditCover: { editingCover = id },
                               onDelete: { library.deletePlaylist(id); page = .home })
            } else {
                ContentUnavailableView("Playlist not found", systemImage: "opticaldisc")
            }
        case .mix(let id): MixPage(id: id, page: $window.page)
        case .artist(let ref): ArtistView(ref: ref)
        }
    }
}

// MARK: - Sidebar

private struct Sidebar: View {
    @Environment(Library.self) private var library
    @Binding var page: Page
    let rename: (Playlist.ID, String) -> Void
    let editCover: (Playlist.ID) -> Void

    var body: some View {
        VStack(alignment: .leading, spacing: 3) {
            HStack(spacing: 10) {
                VinylDisc(size: 34, labelColors: (Retro.gold, Retro.accent), labelRatio: 0.3)
                Text("mosics").font(.display(25, .bold)).lineLimit(1).fixedSize()
                Spacer(minLength: 0)
                AppearanceMenu()
            }
            .padding(.horizontal, 8)
            .padding(.top, 46)
            .padding(.bottom, 18)

            item(.home, "Home", "house")
            item(.search, "Search", "magnifyingglass")
            item(.liked, "Liked Songs", "heart")
            item(.recent, "Recently Played", "clock")
            item(.queue, "Queue", "list.bullet")

            Text("Your records")
                .font(.displayItalic(14)).foregroundStyle(Retro.muted)
                .padding(.horizontal, 12).padding(.top, 20).padding(.bottom, 6)

            ScrollView {
                VStack(alignment: .leading, spacing: 2) {
                    ForEach(library.playlists) { p in
                        let pal = p.palette
                        SideItem(selected: page == .playlist(p.id)) { page = .playlist(p.id) } label: {
                            WithArtwork(url: p.labelArt) { img in
                                VinylDisc(size: 24, label: img, labelColors: (pal.sun, pal.bg), labelRatio: img == nil ? 0.32 : 0.4, sheen: false)
                            }
                            Text(p.name).lineLimit(1)
                        }
                        .contextMenu {
                            Button("Rename…") { rename(p.id, p.name) }
                            Button("Change Cover…") { editCover(p.id) }
                            Button("Delete", role: .destructive) {
                                if page == .playlist(p.id) { page = .home }
                                library.deletePlaylist(p.id)
                            }
                        }
                    }
                }
            }
            .scrollIndicators(.never)

            Button {
                let id = library.newPlaylist("")
                page = .playlist(id)
                rename(id, library.playlists.last?.name ?? "")
            } label: {
                Label("New playlist", systemImage: "plus").frame(maxWidth: .infinity)
            }
            .buttonStyle(PillButton(kind: .outline))
            .padding(.top, 8)
        }
        .padding(.horizontal, 14)
        .padding(.bottom, 16)
        .frame(width: 232)
        .frame(maxHeight: .infinity, alignment: .top)
        .background(Retro.sidebar)
    }

    private func item(_ target: Page, _ title: String, _ icon: String) -> some View {
        SideItem(selected: page == target) { page = target } label: {
            Image(systemName: icon).frame(width: 20)
            Text(title)
        }
    }
}

private struct SideItem<Label: View>: View {
    let selected: Bool
    let action: () -> Void
    @ViewBuilder var label: Label
    @State private var hover = false

    var body: some View {
        Button(action: action) {
            HStack(spacing: 11) { label }
                .font(.system(size: 13.5, weight: selected ? .semibold : .medium))
                .foregroundStyle(selected ? Retro.selectedText : Retro.inkSoft)
                .padding(.horizontal, 11)
                .frame(maxWidth: .infinity, minHeight: 38, alignment: .leading)
                .background(RoundedRectangle(cornerRadius: 10).fill(selected ? Retro.selectedBg : hover ? Retro.rowHover : .clear))
                .contentShape(RoundedRectangle(cornerRadius: 10))
        }
        .buttonStyle(.plain)
        .onHover { hover = $0 }
    }
}

// MARK: - LP header (playlists, mixes, collections)

struct LPHeader<Buttons: View>: View {
    let palette: SleevePalette
    var labelArt: URL? = nil
    let sleeve: AnyView
    let kicker: String
    let title: String
    var subtitle: String? = nil
    let meta: String
    var onEditCover: (() -> Void)? = nil
    @ViewBuilder var buttons: Buttons
    @State private var hover = false

    var body: some View {
        HStack(alignment: .bottom, spacing: 34) {
            ZStack(alignment: .topLeading) {
                WithArtwork(url: labelArt) { img in
                    VinylDisc(size: 188, label: img, labelColors: (palette.sun, palette.bg), labelRatio: img == nil ? 0.19 : 0.24)
                }
                .shadow(color: Retro.shadow.opacity(0.25), radius: 10, y: 6)
                .offset(x: hover ? 116 : 100, y: 6)
                sleeve
                    .frame(width: 200, height: 200)
                    .shadow(color: Retro.shadow.opacity(0.28), radius: 14, y: 8)
                    .overlay(alignment: .bottom) {
                        if let onEditCover, hover {
                            Button(action: onEditCover) { Label("Change cover", systemImage: "paintbrush") }
                                .buttonStyle(PillButton())
                                .padding(.bottom, 12)
                                .transition(.opacity)
                        }
                    }
            }
            .frame(width: 310, height: 200, alignment: .topLeading)
            .onHover { h in withAnimation(.spring(duration: 0.4)) { hover = h } }

            VStack(alignment: .leading, spacing: 8) {
                Text(kicker.uppercased()).font(.system(size: 11.5, weight: .bold)).tracking(1.2).foregroundStyle(Retro.accentText)
                Text(title).font(.display(46)).lineLimit(2).minimumScaleFactor(0.6)
                if let subtitle { Text(subtitle).font(.displayItalic(16)).foregroundStyle(Retro.inkSoft) }
                Text(meta).font(.system(size: 13)).foregroundStyle(Retro.muted)
                HStack(spacing: 10) { buttons }.padding(.top, 8)
            }
            .padding(.bottom, 4)
            Spacer(minLength: 0)
        }
    }
}

// MARK: - Playlist covers

extension Playlist {
    var palette: SleevePalette {
        if case .style(let i)? = cover { return .style(i) }
        return .forName(name)
    }

    /// The picture used for the record label: the chosen image, or the first song's cover.
    var labelArt: URL? {
        switch cover {
        case .image(let file)?: Library.coverURL(file)
        case .artwork?: tracks.first?.thumb(at: 226)
        default: nil
        }
    }
}

/// A playlist's sleeve: an automatic or chosen retro design, its songs' artwork, or the user's picture.
struct PlaylistSleeve: View {
    let playlist: Playlist
    var size: CGFloat
    var showTitle = true

    var body: some View {
        Group {
            switch playlist.cover {
            case .image(let file)?:
                WithArtwork(url: Library.coverURL(file)) { img in
                    if let img { Image(nsImage: img).resizable().aspectRatio(contentMode: .fill) } else { Retro.linen }
                }
            case .artwork?:
                ArtworkMosaic(tracks: playlist.tracks, size: size)
            default:
                RetroSleeve(palette: playlist.palette, title: showTitle ? playlist.name : nil, size: size)
            }
        }
        .frame(width: size, height: size)
        .clipShape(RoundedRectangle(cornerRadius: max(4, size * 0.04)))
    }
}

/// Up to four song covers in a 2×2 grid (or one cover if the playlist is small).
struct ArtworkMosaic: View {
    let tracks: [Track]
    var size: CGFloat

    var body: some View {
        var seen = Set<String>()
        let thumbs = tracks.compactMap { t -> URL? in seen.insert(t.thumb).inserted ? t.thumb(at: 300) : nil }
        return Group {
            if thumbs.count >= 4 {
                Grid(horizontalSpacing: 0, verticalSpacing: 0) {
                    GridRow { Cover(url: thumbs[0], size: size / 2, radius: 0); Cover(url: thumbs[1], size: size / 2, radius: 0) }
                    GridRow { Cover(url: thumbs[2], size: size / 2, radius: 0); Cover(url: thumbs[3], size: size / 2, radius: 0) }
                }
            } else if let first = thumbs.first {
                Cover(url: first, size: size, radius: 0)
            } else {
                Retro.linen
            }
        }
        .frame(width: size, height: size)
    }
}

/// Pick a sleeve design, use the songs' artwork, or choose a picture.
struct CoverPicker: View {
    @Environment(Library.self) private var library
    @Environment(\.dismiss) private var dismiss
    let playlistID: UUID

    var body: some View {
        if let p = library.playlists.first(where: { $0.id == playlistID }) {
            VStack(alignment: .leading, spacing: 20) {
                Text("Change cover").font(.display(26))
                HStack(alignment: .top, spacing: 26) {
                    PlaylistSleeve(playlist: p, size: 190)
                        .shadow(color: Retro.shadow.opacity(0.25), radius: 12, y: 6)
                    VStack(alignment: .leading, spacing: 12) {
                        Text("Sleeve designs").font(.system(size: 12, weight: .semibold)).foregroundStyle(Retro.muted)
                        LazyVGrid(columns: Array(repeating: GridItem(.fixed(62), spacing: 10), count: 5), alignment: .leading, spacing: 10) {
                            ForEach(0..<SleevePalette.styleCount, id: \.self) { i in
                                let selected = p.cover == .style(i) || (p.cover == nil && SleevePalette.forName(p.name) == .style(i))
                                Button { library.setCover(.style(i), for: p.id) } label: {
                                    RetroSleeve(palette: .style(i), size: 62)
                                        .overlay(RoundedRectangle(cornerRadius: 5).strokeBorder(selected ? Retro.accent : .clear, lineWidth: 3))
                                }
                                .buttonStyle(.plain)
                                .accessibilityLabel("Sleeve design \(i + 1)")
                            }
                        }
                        HStack(spacing: 10) {
                            Button { library.setCover(.artwork, for: p.id) } label: { Label("Use song artwork", systemImage: "square.grid.2x2") }
                                .buttonStyle(PillButton(kind: .outline))
                                .disabled(p.tracks.isEmpty)
                            Button { chooseImage(for: p.id) } label: { Label("Choose image…", systemImage: "photo") }
                                .buttonStyle(PillButton(kind: .outline))
                        }
                        .padding(.top, 4)
                    }
                }
                HStack {
                    Button("Reset to automatic") { library.setCover(nil, for: p.id) }
                        .buttonStyle(.plain).font(.system(size: 13, weight: .semibold)).foregroundStyle(Retro.accentText)
                    Spacer()
                    Button("Done") { dismiss() }.buttonStyle(PillButton()).keyboardShortcut(.defaultAction)
                }
            }
            .padding(28)
            .frame(width: 660)
            .background(Retro.paper)
            .foregroundStyle(Retro.ink)
        }
    }

    private func chooseImage(for id: UUID) {
        let panel = NSOpenPanel()
        panel.allowedContentTypes = [.image]
        panel.allowsMultipleSelection = false
        panel.message = "Choose a picture for this playlist's cover"
        if panel.runModal() == .OK, let url = panel.url { library.importCover(from: url, for: id) }
    }
}

/// Liked, Recent and user playlists: an LP header over a track list.
struct CollectionPage: View {
    @Environment(Player.self) private var player
    let title: String
    let kicker: String
    let palette: SleevePalette
    var labelArt: URL? = nil
    let sleeve: AnyView
    let tracks: [Track]
    var kind: TrackListKind = .plain
    var onRename: (() -> Void)? = nil
    var onEditCover: (() -> Void)? = nil
    var onDelete: (() -> Void)? = nil

    var body: some View {
        let total = tracks.reduce(0) { $0 + $1.seconds }
        let meta = "\(tracks.count) song\(tracks.count == 1 ? "" : "s") · \(formatLength(total))"
        return LPTrackList(tracks: tracks, kind: kind, identity: "\(kicker)-\(title)") {
            LPHeader(palette: palette, labelArt: labelArt, sleeve: sleeve, kicker: kicker, title: title, meta: meta,
                     onEditCover: onEditCover) {
                Button { player.shuffle = false; player.play(tracks) } label: {
                    Label("Play", systemImage: "play.fill")
                }
                .buttonStyle(PillButton())
                .disabled(tracks.isEmpty)
                Button { player.shuffle = true; player.play(tracks, startAt: Int.random(in: 0..<max(1, tracks.count))) } label: {
                    Image(systemName: "shuffle")
                }
                .buttonStyle(CircleButton())
                .disabled(tracks.isEmpty)
                .help("Shuffle")
                if onRename != nil || onDelete != nil || onEditCover != nil {
                    Menu {
                        if let onRename { Button("Rename…", action: onRename) }
                        if let onEditCover { Button("Change Cover…", action: onEditCover) }
                        if let onDelete { Divider(); Button("Delete Playlist", role: .destructive, action: onDelete) }
                    } label: {
                        Image(systemName: "ellipsis").frame(width: 42, height: 42)
                            .overlay(Circle().strokeBorder(Retro.ink, lineWidth: 1.5))
                    }
                    .menuStyle(.button).buttonStyle(.plain).menuIndicator(.hidden).fixedSize()
                }
            }
        }
    }
}

// MARK: - Track list styled like the back of an LP

enum TrackListKind: Equatable { case plain, queue, playlist(UUID) }

struct LPTrackList<Header: View>: View {
    typealias Kind = TrackListKind

    @Environment(Player.self) private var player
    @Environment(Library.self) private var library
    let tracks: [Track]
    var kind: Kind = .plain
    var identity = "list"
    var onReachEnd: (() -> Void)? = nil
    var emptyMessage = "Nothing on this record yet — right-click any song to add it."
    @ViewBuilder var header: Header

    var body: some View {
        ScrollView {
            LazyVStack(alignment: .leading, spacing: 22) {
                header
                if tracks.isEmpty {
                    Text(emptyMessage)
                        .font(.displayItalic(15)).foregroundStyle(Retro.muted)
                        .padding(.vertical, 30)
                } else {
                    LazyVStack(spacing: 0) {
                        // Key rows by position *and* song, so a row can never show a stale song
                        // from a previously viewed list.
                        ForEach(Array(tracks.enumerated()), id: \.offset) { i, t in
                            row(i, number: "\(i + 1)").id("\(i)-\(t.id)")
                        }
                    }
                    .padding(.horizontal, 16)
                    .padding(.vertical, 8)
                    .background(Retro.card, in: RoundedRectangle(cornerRadius: 18))
                    .overlay(RoundedRectangle(cornerRadius: 18).strokeBorder(Retro.line))
                }
            }
            .padding(.horizontal, 36)
            .padding(.top, 44)
            .padding(.bottom, 28)
        }
        .id(identity)   // a different playlist gets a fresh list (and scrolls to the top)
    }

    private func row(_ i: Int, number: String) -> some View {
        let t = tracks[i]
        let isCurrent = kind == .queue ? false : player.current?.id == t.id
        return TrackRow(track: t, number: number, isCurrent: isCurrent, liked: library.isLiked(t),
                        toggleLike: { library.toggleLike(t) },
                        play: { kind == .queue ? player.jump(toUpNext: i) : player.play(tracks, startAt: i) })
            .contextMenu {
                TrackMenu(picked: [t], player: player, library: library)
                if case .playlist(let id) = kind {
                    Button("Remove from This Playlist") { library.remove(at: IndexSet(integer: i), from: id) }
                }
                if kind == .queue {
                    Button("Remove from Queue") { player.removeFromUpNext(at: IndexSet(integer: i)) }
                }
            }
            .onAppear { if i >= tracks.count - 4 { onReachEnd?() } }
    }
}

struct TrackRow: View {
    let track: Track
    let number: String
    let isCurrent: Bool
    let liked: Bool
    let toggleLike: () -> Void
    let play: () -> Void
    @State private var hover = false

    var body: some View {
        HStack(spacing: 14) {
            Group {
                if hover && !isCurrent {
                    Image(systemName: "play.fill").font(.system(size: 11)).foregroundStyle(Retro.ink)
                } else {
                    Text(number).font(.display(13.5, .bold)).foregroundStyle(isCurrent ? Retro.accentText : Retro.muted)
                }
            }
            .frame(width: 36, alignment: .leading)

            Cover(url: track.thumb(at: 120), size: 38)
            VStack(alignment: .leading, spacing: 1) {
                Text(track.title).font(.system(size: 13.5, weight: .semibold)).lineLimit(1)
                    .foregroundStyle(isCurrent ? Retro.accentText : Retro.ink)
                ArtistLink(credit: track.artist, font: .system(size: 12.5), color: Retro.muted)
            }
            .frame(maxWidth: .infinity, alignment: .leading)
            Text(track.album).font(.system(size: 12.5).italic()).foregroundStyle(Retro.muted).lineLimit(1)
                .frame(width: 200, alignment: .leading)
            Button(action: toggleLike) {
                Image(systemName: liked ? "heart.fill" : "heart")
                    .foregroundStyle(liked ? Retro.accent : Retro.muted)
                    .opacity(liked || hover ? 1 : 0)
            }
            .buttonStyle(.plain)
            .help(liked ? "Remove from Liked Songs" : "Save to Liked Songs")
            Text(track.duration).font(.system(size: 12.5)).monospacedDigit().foregroundStyle(Retro.muted)
                .frame(width: 44, alignment: .trailing)
        }
        .padding(.horizontal, 10)
        .frame(height: 52)
        .background(RoundedRectangle(cornerRadius: 8).fill(isCurrent ? Retro.rowPlaying : hover ? Retro.rowHover : .clear))
        .overlay(alignment: .bottom) { Rectangle().fill(Retro.linen).frame(height: 1).padding(.horizontal, 4) }
        .contentShape(Rectangle())
        .onHover { hover = $0 }
        .onTapGesture(count: 2, perform: play)
    }
}

/// The right-click menu shared everywhere a song appears.
struct TrackMenu: View {
    let picked: [Track]
    let player: Player
    let library: Library

    var body: some View {
        if let first = picked.first {
            Button("Play") { player.play(picked) }
            Button("Play Next") { picked.reversed().forEach(player.playNext) }
            Button("Add to Queue") { picked.forEach(player.addToQueue) }
            Button("Start Radio") { player.startRadio(first) }
            Button("Go to Artist") { openArtist(first.artist) }
            Divider()
            Button(library.isLiked(first) ? "Remove from Liked Songs" : "Save to Liked Songs") {
                let like = !library.isLiked(first)
                picked.filter { library.isLiked($0) != like }.forEach(library.toggleLike)
            }
            Menu("Add to Playlist") {
                Button("New Playlist") {
                    let id = library.newPlaylist(first.title)
                    picked.forEach { library.add($0, to: id) }
                }
                if !library.playlists.isEmpty { Divider() }
                ForEach(library.playlists) { p in
                    Button(p.name) { picked.forEach { library.add($0, to: p.id) } }
                }
            }
            Divider()
            Button("Copy Song Link") {
                NSPasteboard.general.clearContents()
                NSPasteboard.general.setString("https://music.youtube.com/watch?v=" + first.id, forType: .string)
            }
        }
    }
}

// MARK: - Search

struct SearchPage: View {
    @AppStorage("lastQuery") private var query = ""
    @AppStorage("searchKind") private var kind: YTMusic.Kind = .songs
    @State private var results: [Track] = []
    @State private var next: String?
    @State private var loading = false
    @State private var loadingMore = false
    @State private var error: String?
    @FocusState private var focused: Bool

    private struct Key: Equatable { var query: String; var kind: YTMusic.Kind }

    var body: some View {
        LPTrackList(tracks: results, identity: "search", onReachEnd: loadMore) {
            VStack(alignment: .leading, spacing: 16) {
                HStack(alignment: .lastTextBaseline) {
                    Text("Search").font(.display(40))
                    Spacer()
                    HStack(spacing: 4) {
                        ForEach(YTMusic.Kind.allCases) { k in
                            Button(k.rawValue) { kind = k }
                                .buttonStyle(.plain)
                                .font(.system(size: 13, weight: .semibold))
                                .padding(.horizontal, 16).frame(height: 32)
                                .foregroundStyle(kind == k ? Retro.selectedText : Retro.inkSoft)
                                .background(Capsule().fill(kind == k ? Retro.selectedBg : .clear))
                        }
                    }
                    .padding(4)
                    .background(Capsule().strokeBorder(Retro.line))
                }
                HStack(spacing: 10) {
                    Image(systemName: "magnifyingglass").foregroundStyle(Retro.muted)
                    TextField("Songs, artists, albums…", text: $query)
                        .textFieldStyle(.plain).font(.system(size: 16)).focused($focused)
                    if loading { ProgressView().controlSize(.small) }
                    if !query.isEmpty {
                        Button { query = "" } label: { Image(systemName: "xmark.circle.fill") }
                            .buttonStyle(.plain).foregroundStyle(Retro.muted)
                    }
                }
                .padding(.horizontal, 18).frame(height: 46)
                .background(Capsule().fill(Retro.card))
                .overlay(Capsule().strokeBorder(Retro.line))
                if let error {
                    Text(error).font(.system(size: 13)).foregroundStyle(Retro.danger)
                } else if results.isEmpty && !loading && !query.isEmpty {
                    Text(kind == .songs ? "No songs found — try Videos for live versions, covers and uploads." : "No results.")
                        .font(.displayItalic(15)).foregroundStyle(Retro.muted)
                }
                if loadingMore { ProgressView().controlSize(.small) }
            }
        }
        .onAppear { focused = true }
        .task(id: Key(query: query, kind: kind)) {
            let q = query.trimmingCharacters(in: .whitespaces)
            guard !q.isEmpty else { results = []; next = nil; error = nil; return }
            try? await Task.sleep(for: .milliseconds(350))
            guard !Task.isCancelled else { return }
            loading = true
            defer { loading = false }
            do {
                let page = try await YTMusic.search(q, kind: kind)
                guard !Task.isCancelled else { return }
                results = page.tracks
                next = page.next
                error = nil
            } catch is CancellationError {
            } catch let e as URLError where e.code == .cancelled {
            } catch {
                self.error = error.localizedDescription
            }
        }
    }

    private func loadMore() {
        guard let token = next, !loadingMore, !loading, results.count < 500 else { return }
        loadingMore = true
        let key = Key(query: query, kind: kind)
        Task {
            defer { loadingMore = false }
            guard let page = try? await YTMusic.more(token), key == Key(query: query, kind: kind) else { return }
            let have = Set(results.map(\.id))
            results += page.tracks.filter { !have.contains($0.id) }
            next = page.next
        }
    }
}

// MARK: - Queue

struct QueuePage: View {
    @Environment(Player.self) private var player

    var body: some View {
        @Bindable var p = player
        LPTrackList(tracks: Array(player.upNext), kind: .queue, identity: "queue") {
            VStack(alignment: .leading, spacing: 18) {
                HStack(alignment: .lastTextBaseline) {
                    Text("Queue").font(.display(40))
                    Spacer()
                    Toggle("Autoplay similar songs", isOn: $p.autoplay).toggleStyle(.switch).controlSize(.small)
                        .font(.system(size: 13, weight: .medium)).tint(Retro.accent)
                    Button("Clear") { player.clearUpNext() }.buttonStyle(PillButton(kind: .outline)).disabled(player.upNext.isEmpty)
                }
                if let now = player.current {
                    HStack(spacing: 16) {
                        WithArtwork(url: now.thumb(at: 226)) { img in
                            SpinningRecord(size: 64, label: img, spinning: player.isPlaying && !player.isLoading)
                                .frame(width: 64, height: 64).overlay(Sheen())
                        }
                        VStack(alignment: .leading, spacing: 3) {
                            Text("NOW SPINNING").font(.system(size: 11, weight: .bold)).tracking(1.2).foregroundStyle(Retro.accentText)
                            Text(now.title).font(.display(20)).lineLimit(1)
                            ArtistLink(credit: now.artist, font: .system(size: 13), color: Retro.muted)
                        }
                    }
                }
                Text("Up next").font(.displayItalic(17)).foregroundStyle(Retro.inkSoft)
            }
        }
    }
}

// MARK: - Player bar

struct PlayerBar: View {
    @Environment(Player.self) private var player
    @Environment(Library.self) private var library
    @Binding var page: Page

    var body: some View {
        @Bindable var p = player
        HStack(spacing: 18) {
            Button { if player.current != nil { page = .nowPlaying } } label: {
                HStack(spacing: 12) {
                    SleeveWithRecord(art: player.current?.thumb(at: 226), size: 56,
                                     spinning: player.isPlaying && !player.isLoading, radius: 3)
                        .frame(width: 80, alignment: .leading)
                        .clipped()
                    VStack(alignment: .leading, spacing: 2) {
                        Text(player.current?.title ?? "Nothing playing")
                            .font(.system(size: 14, weight: .bold)).lineLimit(1)
                        if let err = player.error {
                            Text(err).font(.system(size: 12)).foregroundStyle(Retro.barDanger).lineLimit(2).help(err)
                        } else {
                            if let artist = player.current?.artist {
                                ArtistLink(credit: artist, font: .system(size: 12.5), color: Retro.barMuted)
                            } else {
                                Text("Pick a record to play").font(.system(size: 12.5)).foregroundStyle(Retro.barMuted)
                            }
                        }
                    }
                }
                .contentShape(Rectangle())
            }
            .buttonStyle(.plain)
            .help("Open now playing")
            .frame(width: 290, alignment: .leading)

            VStack(spacing: 7) {
                HStack(spacing: 20) {
                    Button(action: player.toggleShuffle) {
                        Image(systemName: "shuffle").foregroundStyle(player.shuffle ? Retro.barHighlight : Retro.barMuted)
                    }.help("Shuffle")
                    Button(action: player.previous) { Image(systemName: "backward.fill") }
                    Button(action: player.togglePlay) {
                        ZStack {
                            Circle().fill(Retro.accent).frame(width: 44, height: 44)
                            if player.isLoading && player.isPlaying {
                                ProgressView().controlSize(.small).tint(Retro.onAccent)
                            } else {
                                Image(systemName: player.isPlaying ? "pause.fill" : "play.fill").foregroundStyle(Retro.onAccent)
                            }
                        }
                    }
                    Button(action: player.next) { Image(systemName: "forward.fill") }
                    Button(action: player.cycleRepeat) {
                        Image(systemName: player.repeatMode == .one ? "repeat.1" : "repeat")
                            .foregroundStyle(player.repeatMode == .off ? Retro.barMuted : Retro.barHighlight)
                    }.help("Repeat")
                }
                .buttonStyle(.plain)
                .font(.system(size: 15))
                .foregroundStyle(Retro.barText)
                .disabled(player.current == nil)

                Scrubber(time: player.time, duration: player.duration, track: Retro.barTrack, fill: Retro.barHighlight,
                         knob: Retro.barText, label: Retro.barMuted, seek: player.seek)
                    .frame(maxWidth: 500)
                    .disabled(player.current == nil)
            }
            .frame(maxWidth: .infinity)

            HStack(spacing: 14) {
                if let t = player.current {
                    Button { library.toggleLike(t) } label: {
                        Image(systemName: library.isLiked(t) ? "heart.fill" : "heart")
                            .foregroundStyle(library.isLiked(t) ? Retro.barHighlight : Retro.barMuted)
                    }
                    .buttonStyle(.plain).help("Like")
                }
                Button { page = .queue } label: { Image(systemName: "list.bullet").foregroundStyle(Retro.barMuted) }
                    .buttonStyle(.plain).help("Queue")
                VolumeSlider(volume: $p.volume)
            }
            .frame(width: 220, alignment: .trailing)
        }
        .padding(.horizontal, 20)
        .frame(height: 88)
        .background(Retro.bar)
        .foregroundStyle(Retro.barText)
    }
}

/// A progress line you can click or drag anywhere to seek.
struct Scrubber: View {
    let time: Double
    let duration: Double
    var track: Color
    var fill: Color
    var knob: Color
    var label: Color
    var height: CGFloat = 4
    let seek: (Double) -> Void
    @State private var dragging: Double?
    @State private var hover = false

    var body: some View {
        HStack(spacing: 10) {
            Text(formatTime(dragging ?? time)).frame(width: 40, alignment: .trailing)
            GeometryReader { geo in
                let total = max(duration, 1)
                let frac = min(1, max(0, (dragging ?? time) / total))
                ZStack(alignment: .leading) {
                    Capsule().fill(track).frame(height: height)
                    Capsule().fill(fill).frame(width: geo.size.width * frac, height: height)
                    Circle().fill(knob).frame(width: height * 3.2, height: height * 3.2)
                        .shadow(color: .black.opacity(0.25), radius: 2, y: 1)
                        .offset(x: geo.size.width * frac - height * 1.6)
                        .opacity(hover || dragging != nil ? 1 : 0)
                }
                .frame(maxHeight: .infinity)
                .contentShape(Rectangle())
                .gesture(DragGesture(minimumDistance: 0)
                    .onChanged { v in dragging = min(1, max(0, v.location.x / geo.size.width)) * total }
                    .onEnded { _ in if let d = dragging { seek(d) }; dragging = nil })
            }
            .frame(height: 16)
            .onHover { hover = $0 }
            Text(formatTime(duration)).frame(width: 40, alignment: .leading)
        }
        .font(.system(size: 11)).monospacedDigit().foregroundStyle(label)
    }
}

/// Volume: a speaker (click to mute) and a slider you can click, drag or scroll.
struct VolumeSlider: View {
    @Binding var volume: Float
    @State private var hover = false
    @State private var dragging = false
    @State private var beforeMute: Float = 0.8

    private var icon: String {
        volume <= 0.001 ? "speaker.slash.fill" : volume < 0.34 ? "speaker.wave.1.fill" : volume < 0.67 ? "speaker.wave.2.fill" : "speaker.wave.3.fill"
    }

    var body: some View {
        HStack(spacing: 8) {
            Button {
                if volume > 0.001 { beforeMute = volume; volume = 0 } else { volume = max(beforeMute, 0.1) }
            } label: {
                Image(systemName: icon).font(.system(size: 13)).frame(width: 20, alignment: .leading)
                    .foregroundStyle(hover ? Retro.barText : Retro.barMuted)
            }
            .buttonStyle(.plain)
            .help(volume > 0.001 ? "Mute" : "Unmute")
            GeometryReader { geo in
                let frac = CGFloat(min(1, max(0, volume)))
                ZStack(alignment: .leading) {
                    Capsule().fill(Retro.barTrack).frame(height: 4)
                    Capsule().fill(hover || dragging ? Retro.barHighlight : Retro.barText).frame(width: geo.size.width * frac, height: 4)
                    Circle().fill(Retro.barText).frame(width: 12, height: 12)
                        .shadow(color: .black.opacity(0.3), radius: 2, y: 1)
                        .offset(x: geo.size.width * frac - 6)
                        .opacity(hover || dragging ? 1 : 0)
                }
                .frame(maxHeight: .infinity)
                .contentShape(Rectangle())
                .gesture(DragGesture(minimumDistance: 0)
                    .onChanged { v in dragging = true; volume = Float(min(1, max(0, v.location.x / geo.size.width))) }
                    .onEnded { _ in dragging = false })
            }
            .frame(width: 96, height: 16)
        }
        .onHover { hover = $0 }
        .accessibilityElement()
        .accessibilityLabel("Volume")
        .accessibilityValue("\(Int(volume * 100)) percent")
        .accessibilityAdjustableAction { dir in
            volume = min(1, max(0, volume + (dir == .increment ? 0.05 : -0.05)))
        }
    }
}

/// An artist name you can click to open their page; underlined on hover.
struct ArtistLink: View {
    let credit: String
    var font: Font
    var color: Color
    @State private var hover = false

    var body: some View {
        Text(credit).font(font).foregroundStyle(color).lineLimit(1).underline(hover)
            .onHover { hover = $0 }
            .onTapGesture { openArtist(credit) }
            .help("Go to \(Suggestions.mainArtist(credit))")
    }
}

/// Sun / moon button: Match System, Light or Dark.
struct AppearanceMenu: View {
    var body: some View {
        @Bindable var store = ThemeStore.shared
        Menu {
            Picker("Appearance", selection: $store.appearance) {
                ForEach(Appearance.allCases) { Text($0.label).tag($0) }
            }
            .pickerStyle(.inline)
        } label: {
            Image(systemName: store.theme.isDark ? "moon.fill" : "sun.max.fill")
                .font(.system(size: 13)).foregroundStyle(Retro.muted).frame(width: 24, height: 28)
        }
        .menuStyle(.button).buttonStyle(.plain).menuIndicator(.hidden).fixedSize()
        .help("Appearance")
    }
}

// MARK: - Now Playing: the turntable

struct NowPlayingPage: View {
    @Environment(Player.self) private var player
    @Environment(Library.self) private var library
    let close: () -> Void

    var body: some View {
        GeometryReader { geo in
            let sleeve = min(400, (geo.size.width - 260) * 0.42 / 1.41 * 1.41, geo.size.height - 220)
            VStack(spacing: 0) {
                HStack {
                    Spacer()
                    Button(action: close) { Image(systemName: "chevron.down") }
                        .buttonStyle(CircleButton(size: 42))
                        .keyboardShortcut(.escape, modifiers: [])
                        .help("Close")
                }
                .padding(.horizontal, 24).padding(.top, 34)

                HStack(alignment: .center, spacing: 60) {
                    SleeveWithRecord(art: player.current?.thumb(at: 544), size: max(220, sleeve),
                                     spinning: player.isPlaying && !player.isLoading, radius: 8)
                    info.frame(maxWidth: 520)
                }
                .padding(.horizontal, 70)
                .frame(maxHeight: .infinity)
            }
        }
        .background(Retro.paper)
    }

    private var info: some View {
        VStack(alignment: .leading, spacing: 24) {
            VStack(alignment: .leading, spacing: 8) {
                Text("NOW SPINNING").font(.system(size: 11.5, weight: .bold)).tracking(1.2).foregroundStyle(Retro.accentText)
                Text(player.current?.title ?? "Nothing playing").font(.display(46)).lineLimit(3).minimumScaleFactor(0.6)
                if let t = player.current {
                    HStack(spacing: 0) {
                        ArtistLink(credit: t.artist, font: .system(size: 17), color: Retro.inkSoft).fixedSize()
                        if !t.album.isEmpty {
                            Text(" · " + t.album).font(.system(size: 17)).foregroundStyle(Retro.inkSoft).lineLimit(1)
                        }
                    }
                }
            }
            Scrubber(time: player.time, duration: player.duration, track: Retro.track, fill: Retro.accent,
                     knob: Retro.ink, label: Retro.muted, height: 6, seek: player.seek)

            HStack(spacing: 20) {
                Button(action: player.toggleShuffle) { Image(systemName: "shuffle") }
                    .buttonStyle(CircleButton(size: 46)).opacity(player.shuffle ? 1 : 0.55).help("Shuffle")
                Button(action: player.previous) { Image(systemName: "backward.fill").font(.system(size: 22)) }.buttonStyle(.plain)
                Button(action: player.togglePlay) {
                    Image(systemName: player.isPlaying ? "pause.fill" : "play.fill")
                        .font(.system(size: 26)).foregroundStyle(Retro.onAccent)
                        .frame(width: 76, height: 76)
                        .background(Circle().fill(Retro.accent))
                        .padding(6)
                        .overlay(Circle().strokeBorder(Retro.ink, lineWidth: 1.5))
                }
                .buttonStyle(.plain)
                Button(action: player.next) { Image(systemName: "forward.fill").font(.system(size: 22)) }.buttonStyle(.plain)
                Button(action: player.cycleRepeat) { Image(systemName: player.repeatMode == .one ? "repeat.1" : "repeat") }
                    .buttonStyle(CircleButton(size: 46)).opacity(player.repeatMode == .off ? 0.55 : 1).help("Repeat")
                Spacer()
                if let t = player.current {
                    Button { library.toggleLike(t) } label: { Image(systemName: library.isLiked(t) ? "heart.fill" : "heart") }
                        .buttonStyle(CircleButton(size: 46, filled: true)).help("Like")
                }
            }
            .disabled(player.current == nil)

            if !player.upNext.isEmpty {
                VStack(alignment: .leading, spacing: 0) {
                    Text("Next up").font(.displayItalic(16)).padding(.bottom, 6)
                    ForEach(Array(player.upNext.prefix(3).enumerated()), id: \.offset) { i, t in
                        HStack(spacing: 12) {
                            Text("\(i + 1)").font(.display(13, .bold)).foregroundStyle(Retro.muted).frame(width: 18, alignment: .leading)
                            VStack(alignment: .leading, spacing: 1) {
                                Text(t.title).font(.system(size: 13.5, weight: .semibold)).lineLimit(1)
                                ArtistLink(credit: t.artist, font: .system(size: 12), color: Retro.muted)
                            }
                            Spacer()
                            Text(t.duration).font(.system(size: 12)).monospacedDigit().foregroundStyle(Retro.muted)
                        }
                        .frame(height: 46)
                        .overlay(alignment: .top) { Rectangle().fill(Retro.linen).frame(height: 1) }
                        .contentShape(Rectangle())
                        .onTapGesture(count: 2) { player.jump(toUpNext: i) }
                    }
                }
                .padding(.horizontal, 18).padding(.vertical, 12)
                .background(Retro.card, in: RoundedRectangle(cornerRadius: 18))
                .overlay(RoundedRectangle(cornerRadius: 18).strokeBorder(Retro.line))
            }
        }
    }
}
