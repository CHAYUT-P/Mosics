import AVFoundation
import AppKit
import Darwin
import MediaPlayer
import Observation

@Observable @MainActor
final class Player {
    enum Repeat { case off, all, one }

    private(set) var queue: [Track] = []
    private(set) var index: Int?
    private(set) var isPlaying = false
    private(set) var isLoading = false
    private(set) var time: Double = 0
    private(set) var duration: Double = 0
    var error: String?
    var shuffle = false
    var repeatMode: Repeat = .off
    var autoplay = true
    var volume: Float = 0.8 { didSet { avPlayer.volume = volume; UserDefaults.standard.set(volume, forKey: "volume") } }

    var current: Track? { index.flatMap { queue.indices.contains($0) ? queue[$0] : nil } }
    var upNext: ArraySlice<Track> { index.map { queue.dropFirst($0 + 1) } ?? queue[...] }

    @ObservationIgnored let library: Library
    @ObservationIgnored private let avPlayer = AVPlayer()
    @ObservationIgnored private var timeObserver: Any?
    @ObservationIgnored private var loadID = 0
    @ObservationIgnored private var artwork: MPMediaItemArtwork?
    /// False while the window is closed, minimised or fully covered: then the 1 s clock stops
    /// pushing updates into SwiftUI (nothing would be drawn anyway).
    @ObservationIgnored private var uiVisible = true

    /// Where playback really is, even while UI updates are paused.
    var position: Double {
        let t = avPlayer.currentTime().seconds
        return t.isFinite ? t : time
    }

    init(library: Library) {
        self.library = library
        if UserDefaults.standard.object(forKey: "volume") != nil {
            volume = UserDefaults.standard.float(forKey: "volume")
        }
        avPlayer.volume = volume
        avPlayer.automaticallyWaitsToMinimizeStalling = true
        timeObserver = avPlayer.addPeriodicTimeObserver(forInterval: CMTime(seconds: 1, preferredTimescale: 600),
                                                        queue: .main) { [weak self] t in
            MainActor.assumeIsolated { self?.tick(t.seconds) }
        }
        NotificationCenter.default.addObserver(forName: AVPlayerItem.didPlayToEndTimeNotification, object: nil,
                                               queue: .main) { [weak self] note in
            MainActor.assumeIsolated {
                guard let self, (note.object as? AVPlayerItem) === self.avPlayer.currentItem else { return }
                self.trackEnded()
            }
        }
        NotificationCenter.default.addObserver(forName: AVPlayerItem.failedToPlayToEndTimeNotification, object: nil,
                                               queue: .main) { [weak self] note in
            MainActor.assumeIsolated {
                let err = note.userInfo?[AVPlayerItemFailedToPlayToEndTimeErrorKey] as? Error
                self?.fail(err?.localizedDescription ?? "Playback stopped.")
            }
        }
        setupRemoteCommands()
        for name in [NSWindow.didChangeOcclusionStateNotification, NSWindow.willCloseNotification,
                     NSWindow.didMiniaturizeNotification, NSWindow.didDeminiaturizeNotification] {
            NotificationCenter.default.addObserver(forName: name, object: nil, queue: .main) { [weak self] _ in
                // After the event settles (a closing window is still "visible" while it closes).
                DispatchQueue.main.async { MainActor.assumeIsolated { self?.refreshVisibility() } }
            }
        }
    }

    private func refreshVisibility() {
        let visible = NSApp.windows.contains {
            $0.isVisible && !$0.isMiniaturized && $0.styleMask.contains(.titled) && $0.occlusionState.contains(.visible)
        }
        guard visible != uiVisible else { return }
        uiVisible = visible
        let open = NSApp.windows.contains { $0.isVisible && $0.styleMask.contains(.titled) }
        if visible {
            time = position
            if !WindowState.shared.isOpen { WindowState.shared.isOpen = true }
        } else if !open {
            // Window closed (macOS keeps it around, hidden): tear the UI down so only the player remains.
            WindowState.shared.isOpen = false
            ArtworkStore.shared.purge()
            URLCache.shared.removeAllCachedResponses()
            // Once SwiftUI has let go of the views, hand the freed pages back to the system.
            DispatchQueue.main.asyncAfter(deadline: .now() + 1.5) { _ = malloc_zone_pressure_relief(nil, 0) }
        }
    }

    // MARK: Queue control

    func play(_ tracks: [Track], startAt i: Int = 0) {
        guard tracks.indices.contains(i) else { return }
        var list = tracks
        var start = i
        if shuffle {
            let first = list.remove(at: i)
            list.shuffle()
            list.insert(first, at: 0)
            start = 0
        }
        queue = list
        load(start)
    }

    func playNext(_ t: Track) {
        guard let i = index else { return play([t]) }
        queue.insert(t, at: i + 1)
    }

    func addToQueue(_ t: Track) {
        if index == nil { return play([t]) }
        queue.append(t)
    }

    func startRadio(_ t: Track) {
        play([t])
        Task { await appendRadio(seed: t) }
    }

    func jump(toUpNext offset: Int) {
        guard let i = index else { return }
        load(i + 1 + offset)
    }

    func removeFromUpNext(at offsets: IndexSet) {
        guard let i = index else { return }
        queue.remove(atOffsets: IndexSet(offsets.map { $0 + i + 1 }))
    }

    func moveUpNext(from: IndexSet, to: Int) {
        guard let i = index else { return }
        queue.move(fromOffsets: IndexSet(from.map { $0 + i + 1 }), toOffset: to + i + 1)
    }

    func clearUpNext() {
        guard let i = index else { return }
        queue.removeSubrange((i + 1)...)
    }

    // MARK: Transport

    func togglePlay() {
        guard current != nil else { return }
        if isPlaying { avPlayer.pause(); isPlaying = false } else { avPlayer.play(); isPlaying = true }
        updateNowPlaying()
    }

    func next() {
        guard let i = index else { return }
        if i + 1 < queue.count { load(i + 1) }
        else if repeatMode == .all, !queue.isEmpty { load(0) }
        else if autoplay, let t = current { Task { await appendRadio(seed: t); if index! + 1 < queue.count { load(index! + 1) } } }
    }

    func previous() {
        guard let i = index else { return }
        if position > 3 || i == 0 { seek(to: 0) } else { load(i - 1) }
    }

    func seek(to seconds: Double) {
        time = seconds
        avPlayer.seek(to: CMTime(seconds: seconds, preferredTimescale: 600), toleranceBefore: .zero, toleranceAfter: .zero)
        updateNowPlaying()
    }

    func toggleShuffle() {
        shuffle.toggle()
        if shuffle, let i = index, i + 1 < queue.count { queue[(i + 1)...].shuffle() }
    }

    func cycleRepeat() {
        repeatMode = switch repeatMode { case .off: .all; case .all: .one; case .one: .off }
    }

    // MARK: Internals

    private func load(_ i: Int) {
        guard queue.indices.contains(i) else { return }
        index = i
        let track = queue[i]
        loadID += 1
        let id = loadID
        avPlayer.replaceCurrentItem(with: nil)
        time = 0
        duration = track.seconds
        isLoading = true
        isPlaying = true
        error = nil
        library.addRecent(track)
        updateNowPlaying()
        loadArtwork(track)
        Task {
            do {
                let url = try await StreamResolver.shared.url(for: track.id)
                guard id == loadID else { return }
                let item = AVPlayerItem(asset: AVURLAsset(url: url))
                item.preferredForwardBufferDuration = 30
                avPlayer.replaceCurrentItem(with: item)
                if isPlaying { avPlayer.play() }
                prefetchNext()
                if autoplay, i + 1 >= queue.count, repeatMode == .off { await appendRadio(seed: track) }
            } catch {
                guard id == loadID else { return }
                fail(error.localizedDescription)
            }
        }
    }

    private func fail(_ message: String) {
        isLoading = false
        isPlaying = false
        error = message
    }

    private func tick(_ t: Double) {
        guard let item = avPlayer.currentItem else { return }
        if uiVisible, t.isFinite, abs(t - time) >= 0.25 { time = t }
        let d = item.duration.seconds
        if d.isFinite, d > 0, d != duration { duration = d }
        let loading = avPlayer.timeControlStatus == .waitingToPlayAtSpecifiedRate
        if loading != isLoading { isLoading = loading; updateNowPlaying() }
        if item.status == .failed { fail(item.error?.localizedDescription ?? "Couldn't play this song.") }
    }

    private func trackEnded() {
        if repeatMode == .one { seek(to: 0); avPlayer.play(); return }
        guard let i = index else { return }
        if i + 1 < queue.count || repeatMode == .all || autoplay { next() } else {
            isPlaying = false
            seek(to: 0)
        }
    }

    private func prefetchNext() {
        guard let i = index, i + 1 < queue.count else { return }
        StreamResolver.shared.prefetch(queue[i + 1].id)
    }

    private func appendRadio(seed: Track) async {
        guard let more = try? await YTMusic.radio(for: seed.id) else { return }
        let have = Set(queue.map(\.id))
        queue.append(contentsOf: more.filter { !have.contains($0.id) }.prefix(25))
        prefetchNext()
    }

    // MARK: Media keys & Control Center

    private func setupRemoteCommands() {
        let c = MPRemoteCommandCenter.shared()
        c.playCommand.addTarget { [weak self] _ in MainActor.assumeIsolated { if self?.isPlaying == false { self?.togglePlay() } }; return .success }
        c.pauseCommand.addTarget { [weak self] _ in MainActor.assumeIsolated { if self?.isPlaying == true { self?.togglePlay() } }; return .success }
        c.togglePlayPauseCommand.addTarget { [weak self] _ in MainActor.assumeIsolated { self?.togglePlay() }; return .success }
        c.nextTrackCommand.addTarget { [weak self] _ in MainActor.assumeIsolated { self?.next() }; return .success }
        c.previousTrackCommand.addTarget { [weak self] _ in MainActor.assumeIsolated { self?.previous() }; return .success }
        c.changePlaybackPositionCommand.addTarget { [weak self] e in
            guard let e = e as? MPChangePlaybackPositionCommandEvent else { return .commandFailed }
            MainActor.assumeIsolated { self?.seek(to: e.positionTime) }
            return .success
        }
    }

    private func loadArtwork(_ t: Track) {
        artwork = nil
        guard let url = t.bigThumb else { return }
        Task {
            guard let (data, _) = try? await URLSession.shared.data(from: url), let img = NSImage(data: data),
                  current?.id == t.id else { return }
            artwork = MPMediaItemArtwork(boundsSize: img.size) { _ in img }
            updateNowPlaying()
        }
    }

    private func updateNowPlaying() {
        let center = MPNowPlayingInfoCenter.default()
        guard let t = current else { center.nowPlayingInfo = nil; center.playbackState = .stopped; return }
        var info: [String: Any] = [
            MPMediaItemPropertyTitle: t.title,
            MPMediaItemPropertyArtist: t.artist,
            MPMediaItemPropertyAlbumTitle: t.album,
            MPMediaItemPropertyPlaybackDuration: duration,
            MPNowPlayingInfoPropertyElapsedPlaybackTime: position,
            MPNowPlayingInfoPropertyPlaybackRate: isPlaying && !isLoading ? 1.0 : 0.0,
        ]
        if let artwork { info[MPMediaItemPropertyArtwork] = artwork }
        center.nowPlayingInfo = info
        center.playbackState = isPlaying ? .playing : .paused
    }
}

/// Whether the main window is open. While closed, ContentView renders nothing, freeing all UI memory.
@Observable @MainActor
final class WindowState {
    static let shared = WindowState()
    var isOpen = true
    /// Lives here (not in the view) so reopening the window returns to the same page.
    var page: Page = .home
}
