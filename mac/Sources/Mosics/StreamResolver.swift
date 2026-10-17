import Foundation

/// Turns a videoId into a playable audio-only URL using yt-dlp.
/// We ask for YouTube's HLS audio streams (234/233), which AVPlayer plays natively and which
/// don't hit the anti-bot 403s that the plain m4a download URLs do.
actor StreamResolver {
    static let shared = StreamResolver()

    enum Failure: LocalizedError {
        case missingYtDlp, failed(String)
        var errorDescription: String? {
            switch self {
            case .missingYtDlp: "yt-dlp not found. Install it with: brew install yt-dlp"
            case .failed(let msg): msg.isEmpty ? "Couldn't load this song." : msg
            }
        }
    }

    private var cache: [String: (url: URL, at: Date)] = [:]
    private var inFlight: [String: Task<URL, Error>] = [:]

    static let ytDlpPath: String? = {
        let bundled = Bundle.main.resourceURL?.appendingPathComponent("yt-dlp").path
        let candidates = [bundled, "/opt/homebrew/bin/yt-dlp", "/usr/local/bin/yt-dlp",
                          NSHomeDirectory() + "/.local/bin/yt-dlp"].compactMap { $0 }
        return candidates.first { FileManager.default.isExecutableFile(atPath: $0) }
    }()

    func url(for id: String) async throws -> URL {
        // Signed stream URLs live ~6h; reuse for 4h.
        if let hit = cache[id], Date().timeIntervalSince(hit.at) < 4 * 3600 { return hit.url }
        if let task = inFlight[id] { return try await task.value }
        let task = Task { try await Self.resolve(id) }
        inFlight[id] = task
        defer { inFlight[id] = nil }
        let url = try await task.value
        cache[id] = (url, Date())
        return url
    }

    /// Warm the cache for the next song so skipping feels instant.
    nonisolated func prefetch(_ id: String) {
        Task { _ = try? await url(for: id) }
    }

    private static func resolve(_ id: String) async throws -> URL {
        guard let exe = ytDlpPath else { throw Failure.missingYtDlp }
        let p = Process()
        p.executableURL = URL(fileURLWithPath: exe)
        p.arguments = ["-f", "234/233/140/bestaudio", "-g", "--no-playlist", "--no-warnings",
                       "https://www.youtube.com/watch?v=" + id]
        var env = ProcessInfo.processInfo.environment   // yt-dlp needs deno on PATH for YouTube
        env["PATH"] = "/opt/homebrew/bin:/usr/local/bin:" + (env["PATH"] ?? "/usr/bin:/bin")
        p.environment = env
        let out = Pipe(), err = Pipe()
        p.standardOutput = out
        p.standardError = err
        try p.run()
        // Read both pipes concurrently so a chatty stderr can't block the process.
        async let outData = Task.detached { out.fileHandleForReading.readDataToEndOfFile() }.value
        async let errData = Task.detached { err.fileHandleForReading.readDataToEndOfFile() }.value
        let (o, e) = await (outData, errData)
        p.waitUntilExit()
        let line = String(decoding: o, as: UTF8.self).split(separator: "\n").first.map(String.init) ?? ""
        guard p.terminationStatus == 0, let url = URL(string: line), url.scheme == "https" else {
            let msg = String(decoding: e, as: UTF8.self)
                .split(separator: "\n").last { $0.hasPrefix("ERROR") }.map(String.init) ?? ""
            throw Failure.failed(msg.replacingOccurrences(of: "ERROR: ", with: ""))
        }
        return url
    }
}
