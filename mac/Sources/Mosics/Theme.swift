import AppKit
import QuartzCore
import SwiftUI

/// Design key: "Modern Vintage" — vinyl everywhere, retro sleeves, Superclarendon titles.
/// Colours come from the selected `VinylTheme` (see Palette.swift).

extension Color {
    init(hex: UInt32, alpha: Double = 1) {
        self.init(.sRGB, red: Double((hex >> 16) & 0xFF) / 255, green: Double((hex >> 8) & 0xFF) / 255,
                  blue: Double(hex & 0xFF) / 255, opacity: alpha)
    }
}

extension Font {
    /// Superclarendon ships with macOS: 70s record-sleeve lettering at zero cost to app size.
    static func display(_ size: CGFloat, _ weight: Font.Weight = .bold) -> Font {
        let name = switch weight {
        case .black, .heavy: "Superclarendon-Black"
        case .bold, .semibold: "Superclarendon-Bold"
        case .light, .thin, .ultraLight: "Superclarendon-Light"
        default: "Superclarendon-Regular"
        }
        return .custom(name, size: size)
    }
    static func displayItalic(_ size: CGFloat) -> Font { .custom("Superclarendon-Italic", size: size) }
}

// MARK: - Sleeves & records

/// A 70s sunset sleeve: a sun sinking into three stripes, title in the corner.
struct RetroSleeve: View {
    let palette: SleevePalette
    var title: String? = nil
    var size: CGFloat
    var badge: String? = nil

    var body: some View {
        Canvas { ctx, sz in
            let w = sz.width, h = sz.height
            ctx.fill(Path(CGRect(origin: .zero, size: sz)), with: .color(palette.bg))
            let r = w * 0.24
            ctx.fill(Path(ellipseIn: CGRect(x: w / 2 - r, y: h * 0.74 - r, width: 2 * r, height: 2 * r)), with: .color(palette.sun))
            // Stripes over the sun, with background-coloured gaps between them.
            let bands: [(CGFloat, CGFloat, Color)] = [
                (0.62, 0.70, palette.stripe1), (0.70, 0.74, palette.bg), (0.74, 0.82, palette.stripe2),
                (0.82, 0.86, palette.bg), (0.86, 0.94, palette.sun), (0.94, 1.0, palette.bg),
            ]
            for (a, b, c) in bands {
                ctx.fill(Path(CGRect(x: 0, y: h * a, width: w, height: h * (b - a) + 0.5)), with: .color(c))
            }
        }
        .overlay(alignment: .topLeading) {
            if let title {
                Text(title)
                    .font(.display(max(11, size * 0.12), .black))
                    .foregroundStyle(palette.ink)
                    .lineLimit(3)
                    .minimumScaleFactor(0.55)
                    .frame(maxHeight: size * 0.55, alignment: .topLeading)
                    .padding(size * 0.08)
            }
        }
        .overlay(alignment: .topTrailing) {
            if let badge {
                Text(badge).font(.system(size: max(8, size * 0.05), weight: .bold)).tracking(1.2)
                    .foregroundStyle(palette.ink.opacity(0.85)).padding(size * 0.08)
            }
        }
        .frame(width: size, height: size)
        .clipShape(RoundedRectangle(cornerRadius: max(4, size * 0.04)))
    }
}

/// A vinyl record: grooves, a few track gaps, and a label (cover art or two-tone colour).
struct VinylDisc: View {
    var size: CGFloat
    var label: NSImage? = nil
    var labelColors: (Color, Color) = (Retro.gold, Retro.paper)
    var labelRatio: CGFloat = 0.2      // label radius ÷ record radius (a real LP is ~0.17)
    var sheen = true
    @Environment(\.displayScale) private var scale

    var body: some View {
        ZStack {
            // Grooves come from a cached bitmap: drawn once per size, shared by every record.
            if let grooves = GrooveCache.image(size: size, labelRatio: labelRatio, scale: scale) {
                Image(decorative: grooves, scale: scale)
            }
            labelView
                .frame(width: size * labelRatio * 2, height: size * labelRatio * 2)
                .clipShape(Circle())
            Circle().fill(Retro.spindle).frame(width: max(3, size * 0.025), height: max(3, size * 0.025))
            if sheen { Sheen() }
        }
        .frame(width: size, height: size)
    }

    @ViewBuilder private var labelView: some View {
        if let label {
            Image(nsImage: label).resizable().aspectRatio(contentMode: .fill)
        } else {
            VStack(spacing: 0) {
                labelColors.0
                labelColors.1
            }
        }
    }
}

/// Renders vinyl grooves once per (size, label size, scale, theme) with Core Graphics and keeps
/// the bitmap. Drawing ~100 rings per record per frame through SwiftUI was the app's biggest memory cost.
@MainActor
enum GrooveCache {
    private static var images: [String: CGImage] = [:]

    static func image(size: CGFloat, labelRatio: CGFloat, scale: CGFloat) -> CGImage? {
        let px = max(1, Int((size * scale).rounded()))
        let key = "\(px)-\(Int(labelRatio * 100))-\(ThemeStore.shared.theme.id)"
        if let hit = images[key] { return hit }
        if images.count > 48 { images.removeAll() }   // sizes are few; this never grows in practice
        guard let ctx = CGContext(data: nil, width: px, height: px, bitsPerComponent: 8, bytesPerRow: 0,
                                  space: CGColorSpaceCreateDeviceRGB(),
                                  bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else { return nil }
        let w = CGFloat(px), R = w / 2, c = CGPoint(x: R, y: R)
        ctx.setFillColor(NSColor(Retro.vinyl).cgColor)
        ctx.fillEllipse(in: CGRect(x: 0, y: 0, width: w, height: w))
        let groove = NSColor(Retro.groove).cgColor, gap = NSColor(Retro.grooveGap).cgColor
        let step = max(1.6 * scale, w / 140)
        var r = R * labelRatio + 3 * scale
        var i = 0
        while r < R - 2 * scale {
            let isGap = i % 17 == 9   // the lighter space between tracks
            ctx.setStrokeColor(isGap ? gap : groove)
            ctx.setLineWidth(isGap ? step * 0.9 : step * 0.45)
            ctx.strokeEllipse(in: CGRect(x: c.x - r, y: c.y - r, width: 2 * r, height: 2 * r))
            r += step
            i += 1
        }
        let img = ctx.makeImage()
        images[key] = img
        return img
    }
}

/// Light catching the grooves. Separate so it stays still while a record turns.
struct Sheen: View {
    var body: some View {
        Circle().fill(AngularGradient(stops: [
            .init(color: .white.opacity(0), location: 0.0),
            .init(color: .white.opacity(0.13), location: 0.11),
            .init(color: .white.opacity(0), location: 0.25),
            .init(color: .white.opacity(0), location: 0.53),
            .init(color: .white.opacity(0.09), location: 0.64),
            .init(color: .white.opacity(0), location: 0.78),
            .init(color: .white.opacity(0), location: 1.0),
        ], center: .center, angle: .degrees(20)))
        .allowsHitTesting(false)
    }
}

/// A record that spins on the GPU (Core Animation) and pauses exactly where it stops.
/// SwiftUI only re-renders it when the label changes, so spinning costs almost no CPU.
struct SpinningRecord: NSViewRepresentable {
    var size: CGFloat
    var label: NSImage?
    var spinning: Bool
    var theme = ThemeStore.shared.theme.id

    func makeNSView(context: Context) -> SpinView { SpinView() }

    func updateNSView(_ view: SpinView, context: Context) {
        view.setImage(label, size: size, theme: theme)
        view.setSpinning(spinning)
    }

    final class SpinView: NSView {
        private let disc = CALayer()
        private var imageKey: ObjectIdentifier?
        private var themeKey = ""
        private var renderedSize: CGFloat = 0
        private var isSpinning = true

        override init(frame: NSRect) {
            super.init(frame: frame)
            wantsLayer = true
            layer?.addSublayer(disc)
            let spin = CABasicAnimation(keyPath: "transform.rotation.z")
            spin.fromValue = 0
            spin.toValue = -2 * Double.pi            // clockwise, like a turntable
            spin.duration = 1.8                       // 33⅓ rpm
            spin.repeatCount = .infinity
            spin.isRemovedOnCompletion = false
            disc.add(spin, forKey: "spin")
            setSpinning(false)
        }
        required init?(coder: NSCoder) { fatalError() }

        override func layout() {
            super.layout()
            CATransaction.begin()
            CATransaction.setDisableActions(true)
            disc.frame = bounds
            CATransaction.commit()
        }

        @MainActor func setImage(_ label: NSImage?, size: CGFloat, theme: String) {
            let key = label.map(ObjectIdentifier.init)
            guard key != imageKey || size != renderedSize || theme != themeKey || disc.contents == nil else { return }
            imageKey = key
            themeKey = theme
            renderedSize = size
            let ratio: CGFloat = size < 80 ? 0.32 : 0.2
            let renderer = ImageRenderer(content: VinylDisc(size: size, label: label, labelRatio: ratio, sheen: false))
            renderer.scale = window?.backingScaleFactor ?? 2
            disc.contents = renderer.cgImage
        }

        func setSpinning(_ on: Bool) {
            guard on != isSpinning else { return }
            isSpinning = on
            if on {
                let paused = disc.timeOffset
                disc.speed = 1
                disc.timeOffset = 0
                disc.beginTime = 0
                disc.beginTime = disc.convertTime(CACurrentMediaTime(), from: nil) - paused
            } else {
                let t = disc.convertTime(CACurrentMediaTime(), from: nil)
                disc.speed = 0
                disc.timeOffset = t
            }
        }
    }
}

/// The signature motif: the album sleeve in front, its record sliding half out to the right
/// with the cover as the label. The record spins (on the GPU) while music plays.
struct SleeveWithRecord: View {
    let art: URL?
    var size: CGFloat
    var spinning = false
    var radius: CGFloat? = nil
    @State private var hover = false

    var body: some View {
        let disc = size * 0.96
        ZStack(alignment: .topLeading) {
            WithArtwork(url: art) { img in
                SpinningRecord(size: disc, label: img, spinning: spinning)
                    .frame(width: disc, height: disc)
                    .overlay(Sheen())
            }
            .shadow(color: Retro.shadow.opacity(0.28), radius: size * 0.05, y: size * 0.03)
            .offset(x: size * (hover ? 0.52 : 0.45), y: size * 0.02)
            Cover(url: art, size: size, radius: radius ?? max(3, size * 0.03))
                .shadow(color: Retro.shadow.opacity(0.3), radius: size * 0.06, y: size * 0.04)
        }
        .frame(width: size * 1.41, height: size, alignment: .topLeading)
        .onHover { h in withAnimation(.spring(duration: 0.4)) { hover = h } }
    }
}

// MARK: - Artwork

/// Small in-memory cache of cover art as NSImage (record labels need real images to render).
@MainActor
final class ArtworkStore {
    static let shared = ArtworkStore()
    private let cache = NSCache<NSURL, NSImage>()
    private var inFlight: [URL: Task<NSImage?, Never>] = [:]

    init() { cache.totalCostLimit = 24 << 20 }   // ~24 MB of decoded pixels, oldest dropped first

    func purge() { cache.removeAllObjects() }

    func image(for url: URL?) async -> NSImage? {
        guard let url else { return nil }
        if let hit = cache.object(forKey: url as NSURL) { return hit }
        if let task = inFlight[url] { return await task.value }
        let task = Task<NSImage?, Never> {
            guard let (data, _) = try? await URLSession.shared.data(from: url) else { return nil }
            return NSImage(data: data)
        }
        inFlight[url] = task
        let img = await task.value
        inFlight[url] = nil
        if let img {
            let px = img.representations.first.map { $0.pixelsWide * $0.pixelsHigh } ?? 0
            cache.setObject(img, forKey: url as NSURL, cost: max(1, px * 4))
        }
        return img
    }
}

/// Loads cover art and hands it to `content` (nil while loading).
struct WithArtwork<Content: View>: View {
    let url: URL?
    @ViewBuilder var content: (NSImage?) -> Content
    @State private var image: NSImage?

    var body: some View {
        content(image)
            .task(id: url) { image = await ArtworkStore.shared.image(for: url) }
    }
}

/// Square cover art with a retro placeholder.
struct Cover: View {
    let url: URL?
    var size: CGFloat
    var radius: CGFloat = 4

    var body: some View {
        AsyncImage(url: url, transaction: Transaction(animation: .easeOut(duration: 0.2))) { phase in
            if let img = phase.image {
                img.resizable().aspectRatio(contentMode: .fill)
            } else {
                Retro.linen.overlay(Image(systemName: "music.note").foregroundStyle(Retro.muted))
            }
        }
        .frame(width: size, height: size)
        .clipShape(RoundedRectangle(cornerRadius: radius))
    }
}

// MARK: - Buttons

struct PillButton: ButtonStyle {
    enum Kind { case primary, outline }
    var kind: Kind = .primary

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .font(.system(size: 14, weight: kind == .primary ? .bold : .semibold))
            .padding(.horizontal, 20)
            .frame(height: 42)
            .foregroundStyle(kind == .primary ? Retro.onAccent : Retro.ink)
            .background {
                if kind == .primary {
                    Capsule().fill(Retro.accent)
                } else {
                    Capsule().strokeBorder(Retro.ink, lineWidth: 1.5)
                }
            }
            .contentShape(Capsule())
            .opacity(configuration.isPressed ? 0.75 : 1)
            .scaleEffect(configuration.isPressed ? 0.97 : 1)
    }
}

struct CircleButton: ButtonStyle {
    var size: CGFloat = 42
    var filled = false

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .font(.system(size: size * 0.38, weight: .semibold))
            .frame(width: size, height: size)
            .foregroundStyle(filled ? Retro.barHighlight : Retro.ink)
            .background {
                if filled { Circle().fill(Retro.selectedBg) } else { Circle().strokeBorder(Retro.ink, lineWidth: 1.5) }
            }
            .contentShape(Circle())
            .opacity(configuration.isPressed ? 0.7 : 1)
    }
}

// MARK: - Helpers

func formatLength(_ seconds: Double) -> String {
    let m = Int(seconds) / 60
    return m >= 60 ? "\(m / 60) hr \(m % 60) min" : "\(m) min"
}
