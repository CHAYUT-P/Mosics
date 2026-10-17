// Renders the app icon PNGs into an .iconset folder. Usage: swift make-icon.swift <out.iconset>
// Swiss Red: a black record with a red sunset label on a warm white tile.
import AppKit

let out = URL(fileURLWithPath: CommandLine.arguments[1])
try? FileManager.default.createDirectory(at: out, withIntermediateDirectories: true)

func rgb(_ hex: UInt32, _ a: CGFloat = 1) -> NSColor {
    NSColor(srgbRed: CGFloat((hex >> 16) & 0xFF) / 255, green: CGFloat((hex >> 8) & 0xFF) / 255,
            blue: CGFloat(hex & 0xFF) / 255, alpha: a)
}

func render(_ px: Int) -> Data {
    let s = CGFloat(px)
    let rep = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: px, pixelsHigh: px, bitsPerSample: 8,
                               samplesPerPixel: 4, hasAlpha: true, isPlanar: false,
                               colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0)!
    NSGraphicsContext.saveGraphicsState()
    NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: rep)
    let ctx = NSGraphicsContext.current!.cgContext

    // Cream tile
    let inset = s * 0.09
    let tile = NSRect(x: inset, y: inset, width: s - 2 * inset, height: s - 2 * inset)
    let tilePath = NSBezierPath(roundedRect: tile, xRadius: tile.width * 0.225, yRadius: tile.width * 0.225)
    NSGradient(colors: [rgb(0xFAF9F6), rgb(0xE9E8E2)])!.draw(in: tilePath, angle: -90)

    // Record, slightly off-centre like it's sliding out of a sleeve
    let c = CGPoint(x: s * 0.5, y: s * 0.5)
    let R = tile.width * 0.40
    ctx.saveGState()
    ctx.setShadow(offset: CGSize(width: 0, height: -s * 0.012), blur: s * 0.03, color: rgb(0x2A211C, 0.35).cgColor)
    rgb(0x111111).setFill()
    NSBezierPath(ovalIn: NSRect(x: c.x - R, y: c.y - R, width: 2 * R, height: 2 * R)).fill()
    ctx.restoreGState()
    if px >= 64 {
        let step = max(1.2, s / 120)
        var r = R * 0.42
        var i = 0
        while r < R - step {
            let ring = NSBezierPath(ovalIn: NSRect(x: c.x - r, y: c.y - r, width: 2 * r, height: 2 * r))
            ring.lineWidth = i % 9 == 4 ? step * 0.9 : step * 0.4
            (i % 9 == 4 ? rgb(0x3A322E) : rgb(0x2C2522)).setStroke()
            ring.stroke()
            r += step
            i += 1
        }
    }
    // Sheen
    ctx.saveGState()
    NSBezierPath(ovalIn: NSRect(x: c.x - R, y: c.y - R, width: 2 * R, height: 2 * R)).addClip()
    for (a0, a1, alpha) in [(30.0, 62.0, 0.16), (210.0, 238.0, 0.11)] {
        let p = NSBezierPath()
        p.move(to: c)
        p.appendArc(withCenter: c, radius: R, startAngle: a0, endAngle: a1)
        p.close()
        rgb(0xFFFFFF, alpha).setFill()
        p.fill()
    }
    ctx.restoreGState()

    // Sunset label: sun sinking into stripes
    let lr = R * 0.38
    let label = NSBezierPath(ovalIn: NSRect(x: c.x - lr, y: c.y - lr, width: 2 * lr, height: 2 * lr))
    ctx.saveGState()
    label.addClip()
    rgb(0xD7261E).setFill(); NSRect(x: c.x - lr, y: c.y - lr, width: 2 * lr, height: 2 * lr).fill()
    let sunR = lr * 0.62
    rgb(0xFFFFFF).setFill()
    NSBezierPath(ovalIn: NSRect(x: c.x - sunR, y: c.y - lr * 0.45 - sunR, width: 2 * sunR, height: 2 * sunR)).fill()
    let bands: [(CGFloat, CGFloat, UInt32)] = [(-0.18, -0.36, 0x121212), (-0.46, -0.64, 0xFFFFFF), (-0.74, -1.0, 0x121212)]
    for (top, bottom, col) in bands {
        rgb(col).setFill()
        NSRect(x: c.x - lr, y: c.y + bottom * lr, width: 2 * lr, height: (top - bottom) * lr).fill()
    }
    ctx.restoreGState()
    rgb(0xF5F4F0).setFill()
    let hole = max(1.5, s * 0.018)
    NSBezierPath(ovalIn: NSRect(x: c.x - hole, y: c.y - hole, width: 2 * hole, height: 2 * hole)).fill()

    NSGraphicsContext.restoreGraphicsState()
    return rep.representation(using: .png, properties: [:])!
}

for base in [16, 32, 128, 256, 512] {
    try! render(base).write(to: out.appendingPathComponent("icon_\(base)x\(base).png"))
    try! render(base * 2).write(to: out.appendingPathComponent("icon_\(base)x\(base)@2x.png"))
}
