import AppKit
import SwiftUI

@main
struct MosicsApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) private var appDelegate
    @State private var library: Library
    @State private var player: Player
    @State private var suggestions = Suggestions()

    init() {
        let lib = Library()
        _library = State(initialValue: lib)
        _player = State(initialValue: Player(library: lib))
        // Artwork lives on disk (100 MB) and as decoded images in ArtworkStore, so the URL cache needs little RAM.
        URLCache.shared = URLCache(memoryCapacity: 2 << 20, diskCapacity: 100 << 20)
    }

    var body: some Scene {
        Window("Mosics", id: "main") {
            ContentView()
                .environment(library)
                .environment(player)
                .environment(suggestions)
                .frame(minWidth: 960, minHeight: 640)

                .onAppear { SpaceKey.install { player.togglePlay() } }
        }
        .windowStyle(.hiddenTitleBar)
        .defaultSize(width: 1180, height: 780)
        .commands {
            CommandGroup(after: .sidebar) {
                Picker("Appearance", selection: Binding(get: { ThemeStore.shared.appearance },
                                                        set: { ThemeStore.shared.appearance = $0 })) {
                    ForEach(Appearance.allCases) { Text($0.label).tag($0) }
                }
            }
            CommandMenu("Playback") {
                Button(player.isPlaying ? "Pause" : "Play") { player.togglePlay() }
                    .keyboardShortcut(.return, modifiers: .command)
                Button("Next") { player.next() }.keyboardShortcut(.rightArrow, modifiers: .command)
                Button("Previous") { player.previous() }.keyboardShortcut(.leftArrow, modifiers: .command)
                Divider()
                Button("Volume Up") { player.volume = min(1, player.volume + 0.1) }
                    .keyboardShortcut(.upArrow, modifiers: .command)
                Button("Volume Down") { player.volume = max(0, player.volume - 0.1) }
                    .keyboardShortcut(.downArrow, modifiers: .command)
                Divider()
                Toggle("Shuffle", isOn: Binding(get: { player.shuffle }, set: { _ in player.toggleShuffle() }))
                    .keyboardShortcut("s", modifiers: [.command, .option])
                Button("Cycle Repeat") { player.cycleRepeat() }.keyboardShortcut("r", modifiers: .command)
                Toggle("Autoplay Similar Songs", isOn: Binding(get: { player.autoplay }, set: { player.autoplay = $0 }))
                Divider()
                Button("Like Current Song") { if let t = player.current { library.toggleLike(t) } }
                    .keyboardShortcut("l", modifiers: .command)
            }
        }
        MenuBarExtra {
            MenuBarControls().environment(player).environment(library)
        } label: {
            Image(systemName: player.isPlaying ? "record.circle.fill" : "record.circle")
        }
        .menuBarExtraStyle(.menu)   // a plain system menu: no extra window, almost no memory
    }
}

/// Keeps the app (and the music) alive when the window is closed; the Dock icon brings it back.
final class AppDelegate: NSObject, NSApplicationDelegate {
    func applicationDidFinishLaunching(_ notification: Notification) {
        MainActor.assumeIsolated { ThemeStore.shared.start() }
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { false }
}

/// The menu-bar record: control playback while the window is closed.
struct MenuBarControls: View {
    @Environment(Player.self) private var player
    @Environment(Library.self) private var library
    @Environment(\.openWindow) private var openWindow

    var body: some View {
        if let t = player.current {
            Text(t.title)
            Text(t.artist)
        } else {
            Text("Nothing playing")
        }
        Divider()
        Button(player.isPlaying ? "Pause" : "Play") { player.togglePlay() }.disabled(player.current == nil)
        Button("Next") { player.next() }.disabled(player.current == nil)
        Button("Previous") { player.previous() }.disabled(player.current == nil)
        if let t = player.current {
            Button(library.isLiked(t) ? "Remove from Liked Songs" : "Save to Liked Songs") { library.toggleLike(t) }
        }
        Divider()
        Button("Open Mosics") {
            openWindow(id: "main")
            NSApp.activate(ignoringOtherApps: true)
        }
        Button("Quit Mosics") { NSApp.terminate(nil) }
    }
}

/// Space toggles play/pause, except while typing in a text field.
enum SpaceKey {
    private static var monitor: Any?

    @MainActor static func install(_ action: @escaping @MainActor () -> Void) {
        guard monitor == nil else { return }
        monitor = NSEvent.addLocalMonitorForEvents(matching: .keyDown) { event in
            guard event.keyCode == 49, event.modifierFlags.intersection(.deviceIndependentFlagsMask).isEmpty,
                  !(NSApp.keyWindow?.firstResponder is NSText) else { return event }
            MainActor.assumeIsolated { action() }
            return nil
        }
    }
}
