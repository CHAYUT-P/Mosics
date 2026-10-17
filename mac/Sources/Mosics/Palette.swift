import Observation
import SwiftUI

/// One colour scheme for the vinyl design. Every role is explicit so dark themes work too
/// (e.g. `bar` stays the darkest surface even when `paper` is dark).
struct VinylTheme: Identifiable, Hashable, Sendable {
    let id: String
    let name: String
    var isDark = false
    // Surfaces & text
    var paper, sidebar, line, card, linen, rowPlaying, rowHover: Color
    var ink, inkSoft, muted: Color
    var accent, accentText, onAccent, gold, alt, danger: Color
    var selectedBg, selectedText, shadow, track: Color
    // Player bar
    var bar, barText, barMuted, barTrack, barHighlight, barDanger, knobLight, knobDark: Color
    // Records & turntable
    var vinyl, groove, grooveGap, spindle: Color
    var plinthTop, plinthBottom, platter, platterRim, deckDetail, armBase, arm, armDark: Color
    /// Nine sleeve palettes; mixes and playlists pick from these.
    var sleeves: [SleevePalette]
}

/// The current theme. Views read it through `Retro`, so changing it re-renders everything.
@Observable
final class ThemeStore {
    nonisolated(unsafe) static let shared = ThemeStore()

    var theme: VinylTheme = .swissRed

    private init() {}
}

/// Shorthand for the current theme's colours.
enum Retro {
    private static var t: VinylTheme { ThemeStore.shared.theme }
    static var paper: Color { t.paper }
    static var sidebar: Color { t.sidebar }
    static var line: Color { t.line }
    static var card: Color { t.card }
    static var linen: Color { t.linen }
    static var rowPlaying: Color { t.rowPlaying }
    static var rowHover: Color { t.rowHover }
    static var ink: Color { t.ink }
    static var inkSoft: Color { t.inkSoft }
    static var muted: Color { t.muted }
    static var accent: Color { t.accent }
    static var accentText: Color { t.accentText }
    static var onAccent: Color { t.onAccent }
    static var gold: Color { t.gold }
    static var alt: Color { t.alt }
    static var danger: Color { t.danger }
    static var selectedBg: Color { t.selectedBg }
    static var selectedText: Color { t.selectedText }
    static var shadow: Color { t.shadow }
    static var track: Color { t.track }
    static var bar: Color { t.bar }
    static var barText: Color { t.barText }
    static var barMuted: Color { t.barMuted }
    static var barTrack: Color { t.barTrack }
    static var barHighlight: Color { t.barHighlight }
    static var barDanger: Color { t.barDanger }
    static var knobLight: Color { t.knobLight }
    static var knobDark: Color { t.knobDark }
    static var vinyl: Color { t.vinyl }
    static var groove: Color { t.groove }
    static var grooveGap: Color { t.grooveGap }
    static var spindle: Color { t.spindle }
    static var plinthTop: Color { t.plinthTop }
    static var plinthBottom: Color { t.plinthBottom }
    static var platter: Color { t.platter }
    static var platterRim: Color { t.platterRim }
    static var deckDetail: Color { t.deckDetail }
    static var armBase: Color { t.armBase }
    static var arm: Color { t.arm }
    static var armDark: Color { t.armDark }
}

/// Colours for a retro sunset sleeve and its record label.
struct SleevePalette: Hashable, Sendable {
    var bg: Color, sun: Color, stripe1: Color, stripe2: Color, ink: Color

    init(_ bg: UInt32, _ sun: UInt32, _ s1: UInt32, _ s2: UInt32, _ ink: UInt32) {
        self.bg = Color(hex: bg); self.sun = Color(hex: sun)
        stripe1 = Color(hex: s1); stripe2 = Color(hex: s2); self.ink = Color(hex: ink)
    }

    private static var pool: [SleevePalette] { ThemeStore.shared.theme.sleeves }

    /// A stable palette for any name (Swift's hashValue changes per launch, so hash the bytes).
    static func forName(_ name: String) -> SleevePalette {
        pool[name.utf8.reduce(0) { ($0 &* 31 &+ Int($1)) & 0xFFFF } % pool.count]
    }

    static func forShelf(_ id: String) -> SleevePalette {
        switch id {
        case "city-pop": pool[0]
        case "thai-indie": pool[1]
        case "japan": pool[2]
        case "britpop": pool[3]
        case "made-for-you": pool[4]
        case "because": pool[5]
        default: forName(id)
        }
    }

    static func style(_ i: Int) -> SleevePalette { pool[((i % pool.count) + pool.count) % pool.count] }
    static var styleCount: Int { pool.count }
    static var liked: SleevePalette { pool[7] }
    static var recent: SleevePalette { pool[6] }
    static var home: SleevePalette { pool[0] }
}

// MARK: - The themes

private func c(_ hex: UInt32) -> Color { Color(hex: hex) }

extension VinylTheme {

    /// Cream paper, espresso ink, burnt orange — a record shop on a sunny afternoon.
    static let sunset = VinylTheme(
        id: "sunset", name: "Sunset Records",
        paper: c(0xF3EBDD), sidebar: c(0xEADFCB), line: c(0xDCCDB3), card: c(0xFBF6EC), linen: c(0xEADFCB),
        rowPlaying: c(0xF3E3D3), rowHover: c(0xF0E5D2),
        ink: c(0x2A211C), inkSoft: c(0x4E433B), muted: c(0x6E625A),
        accent: c(0xC4501E), accentText: c(0xB8481B), onAccent: .white, gold: c(0xE3A93B), alt: c(0x2F6F6A), danger: c(0xA8432E),
        selectedBg: c(0x2A211C), selectedText: c(0xF3EBDD), shadow: c(0x2A211C), track: c(0xE2D4BC),
        bar: c(0x2A211C), barText: c(0xF3EBDD), barMuted: c(0xC9BBA6), barTrack: c(0x4A3E36), barHighlight: c(0xE3A93B),
        barDanger: c(0xE58A6B), knobLight: c(0x5A4C43), knobDark: c(0x2F2722),
        vinyl: c(0x1B1716), groove: c(0x2C2522), grooveGap: c(0x3A322E), spindle: c(0xF3EBDD),
        plinthTop: c(0xE9DCC6), plinthBottom: c(0xDCCBAE), platter: c(0xC9B796), platterRim: c(0xBDA987),
        deckDetail: c(0xB9A584), armBase: c(0xCDBB9A), arm: c(0x3A302A), armDark: c(0x2A211C),
        sleeves: [
            SleevePalette(0x2F4C6E, 0xE58A6B, 0xE3A93B, 0xC4501E, 0xF3EBDD), SleevePalette(0xE8D8B8, 0xC4501E, 0x2F6F6A, 0xE3A93B, 0x2A211C),
            SleevePalette(0x7FB3D5, 0xF3EBDD, 0xF2C1B6, 0xE58A6B, 0x1F2E44), SleevePalette(0xA8432E, 0xE3A93B, 0xF3EBDD, 0x2A211C, 0xF3EBDD),
            SleevePalette(0x2F6F6A, 0xE3A93B, 0xE58A6B, 0xF3EBDD, 0xF3EBDD), SleevePalette(0xE3A93B, 0xA8432E, 0x2A211C, 0xF3EBDD, 0x2A211C),
            SleevePalette(0x6B7A3A, 0xF3EBDD, 0xE3A93B, 0xC4501E, 0xF3EBDD), SleevePalette(0x5A3E55, 0xE58A6B, 0xE3A93B, 0xF3EBDD, 0xF3EBDD),
            SleevePalette(0x2A211C, 0xE3A93B, 0xC4501E, 0x2F6F6A, 0xF3EBDD),
        ])

    /// Swiss Red (chosen 2026-10-06): warm white, black ink, one strong red — a Swiss poster on vinyl.
    static let swissRed = VinylTheme(
        id: "swiss", name: "Swiss Red",
        paper: c(0xF5F4F0), sidebar: c(0xECEBE6), line: c(0xD6D4CC), card: c(0xFFFFFF), linen: c(0xE6E4DD),
        rowPlaying: c(0xFBE5E3), rowHover: c(0xEFEEE9),
        ink: c(0x121212), inkSoft: c(0x2E2E2E), muted: c(0x666660),
        accent: c(0xD7261E), accentText: c(0xC0211A), onAccent: .white, gold: c(0x121212), alt: c(0x9A9A92), danger: c(0xC0211A),
        selectedBg: c(0x121212), selectedText: c(0xFFFFFF), shadow: c(0x121212), track: c(0xDEDCD5),
        bar: c(0x121212), barText: c(0xFFFFFF), barMuted: c(0xA8A8A2), barTrack: c(0x3A3A3A), barHighlight: c(0xFF3B30),
        barDanger: c(0xFF8A80), knobLight: c(0x4A4A4A), knobDark: c(0x1A1A1A),
        vinyl: c(0x111111), groove: c(0x242424), grooveGap: c(0x333333), spindle: c(0xF5F4F0),
        plinthTop: c(0xF2F1EC), plinthBottom: c(0xE2E0D9), platter: c(0xCFCDC6), platterRim: c(0xBDBBB4),
        deckDetail: c(0xA8A6A0), armBase: c(0xDCDAD3), arm: c(0x2E2E2E), armDark: c(0x121212),
        sleeves: [
            SleevePalette(0xD7261E, 0xFFFFFF, 0x121212, 0xF5F4F0, 0xFFFFFF), SleevePalette(0xFFFFFF, 0xD7261E, 0x121212, 0xD6D4CC, 0x121212),
            SleevePalette(0x121212, 0xD7261E, 0xFFFFFF, 0x6E6E6E, 0xFFFFFF), SleevePalette(0xF5F4F0, 0x121212, 0xD7261E, 0x9A9A92, 0x121212),
            SleevePalette(0x6E6E6E, 0xFFFFFF, 0xD7261E, 0x121212, 0xFFFFFF), SleevePalette(0xD7261E, 0x121212, 0xFFFFFF, 0x121212, 0xFFFFFF),
            SleevePalette(0x121212, 0xFFFFFF, 0xD7261E, 0x6E6E6E, 0xFFFFFF), SleevePalette(0xD6D4CC, 0xD7261E, 0x121212, 0xFFFFFF, 0x121212),
            SleevePalette(0x9A9A92, 0x121212, 0xFFFFFF, 0xD7261E, 0x121212),
        ])
}
