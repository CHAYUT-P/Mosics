import Darwin
import Foundation
import Observation

/// A row of suggested songs on the Home page.
struct Shelf: Identifiable, Codable, Hashable {
    var id: String
    var title: String
    var subtitle: String
    var tracks: [Track]
}

/// Builds the Home page: hand-curated genre mixes plus mixes made from what you like and play.
///
/// Genre mixes come from curated artist lists rather than YouTube's radio, because radio drifts
/// towards global pop within a few songs. Each refresh samples different artists, so it stays fresh.
@Observable @MainActor
final class Suggestions {
    private(set) var shelves: [Shelf] = []
    private(set) var loading = false
    private(set) var updatedAt: Date?

    struct Mix: Sendable {
        let id, title, subtitle: String
        /// Each entry: search name, plus extra spellings the catalog might use (romanised / native script).
        let artists: [[String]]
    }

    nonisolated static let mixes: [Mix] = [
        Mix(id: "thai-indie", title: "Thai Indie", subtitle: "Bangkok bedroom pop, warm guitars and late-night synths",
            artists: [["Phum Viphurit"], ["Polycat", "โพลีแคท"], ["Television Off"], ["Safeplanet"], ["Scrubb", "สครับบ์"],
                      ["YENA", "เยนา"], ["Gym and Swim"], ["Plastic Plastic"], ["Jelly Rocket"], ["The Toys", "เดอะทอยส์"],
                      ["Moving and Cut"], ["Lipta"], ["Max Jenmana", "แม็กซ์ เจนมานะ"], ["Tilly Birds"], ["Zweed n' Roll"],
                      ["Folk9"], ["Part Time Musicians"], ["H 3 F"], ["Yellow Fang"], ["Desktop Error"],
                      ["Solitude Is Bliss"], ["Musketeers", "มัสคีเทียร์"], ["Hugo"], ["Whal & Dolph", "Whal&Dolph"]]),
        Mix(id: "city-pop", title: "City Pop", subtitle: "Tokyo after dark, 1978–1989",
            artists: [["Mariya Takeuchi", "竹内まりや"], ["Miki Matsubara", "松原みき"], ["Tatsuro Yamashita", "山下達郎"],
                      ["Anri", "杏里"], ["Taeko Onuki", "Taeko Ohnuki", "大貫妙子"], ["Junko Ohashi", "大橋純子"],
                      ["Hiroshi Sato", "佐藤博"], ["Toshiki Kadomatsu", "角松敏生"], ["Momoko Kikuchi", "菊池桃子"],
                      ["Junko Yagami", "八神純子"], ["Tomoko Aran", "亜蘭知子"], ["Masayoshi Takanaka", "高中正義"],
                      ["Omega Tribe", "オメガトライブ"], ["Yurie Kokubu", "国分友里恵"], ["Meiko Nakahara", "中原めいこ"],
                      ["Hitomi Tohyama", "当山ひとみ"], ["Takako Mamiya", "間宮貴子"], ["EPO"], ["Akiko Yano", "矢野顕子"]]),
        Mix(id: "japan", title: "Japan Indie & Anime", subtitle: "Your Name skies, Tokyo bedrooms, soft guitars",
            artists: [["RADWIMPS"], ["Yorushika", "ヨルシカ"], ["Fujii Kaze", "藤井 風", "藤井風"], ["Aimyon", "あいみょん"],
                      ["Vaundy"], ["Kenshi Yonezu", "米津玄師"], ["Official HIGE DANdism", "Official髭男dism"], ["King Gnu"],
                      ["YOASOBI"], ["Aimer"], ["Spitz", "スピッツ"], ["back number"], ["Gen Hoshino", "星野源"], ["Uru"],
                      ["Yuuri", "優里"], ["Lamp"], ["KIRINJI", "キリンジ"], ["never young beach"], ["Ichiko Aoba", "青葉市子"],
                      ["Suchmos"], ["Yogee New Waves"], ["Nulbarich"], ["Kirinji"], ["Mrs. GREEN APPLE"], ["Ado"],
                      ["Joe Hisaishi", "久石譲"]]),
        Mix(id: "britpop", title: "If You Like Oasis", subtitle: "Britpop and timeless English guitar songs",
            artists: [["Oasis"], ["Blur"], ["The Verve"], ["Pulp"], ["Suede"], ["The Stone Roses"], ["The La's"],
                      ["Travis"], ["Stereophonics"], ["Ocean Colour Scene"], ["Supergrass"], ["Manic Street Preachers"],
                      ["The Smiths"], ["The Charlatans"], ["Kula Shaker"], ["Cast"], ["Embrace"], ["Shed Seven"],
                      ["The Beatles"], ["The Kinks"], ["Keane"], ["The Kooks"]]),
    ]

    private let cacheFile: URL = {
        let dir = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("Mosics", isDirectory: true)
        return dir.appendingPathComponent("home.json")
    }()

    private struct Cache: Codable {
        var shelves: [Shelf]
        var at: Date
    }

    init() {
        if let data = try? Data(contentsOf: cacheFile), let c = try? JSONDecoder().decode(Cache.self, from: data) {
            shelves = c.shelves
            updatedAt = c.at
        }
    }

    /// Refreshes if forced, empty, or older than 12 hours.
    func refresh(library: Library, force: Bool = false) async {
        guard !loading else { return }
        if !force, let at = updatedAt, Date().timeIntervalSince(at) < 12 * 3600, !shelves.isEmpty { return }
        loading = true
        defer { loading = false }

        let liked = library.liked, recent = library.recent
        async let personal = Self.personalShelves(liked: liked, recent: recent)
        // One mix at a time, a few searches at a time: each search response is a large JSON tree,
        // and parsing dozens at once inflated memory for the rest of the session.
        var genres: [Shelf] = []
        for mix in Self.mixes {
            if let shelf = await Self.build(mix) { genres.append(shelf) }
        }
        let result = await personal + genres
        guard !result.isEmpty else { return }   // offline: keep what we had
        shelves = result
        updatedAt = Date()
        if let data = try? JSONEncoder().encode(Cache(shelves: result, at: Date())) {
            try? data.write(to: cacheFile, options: .atomic)
        }
        _ = malloc_zone_pressure_relief(nil, 0)   // give the parsing scratch memory back to the system
    }

    // MARK: Genre mixes

    nonisolated private static func build(_ mix: Mix) async -> Shelf? {
        let picked = Array(mix.artists.shuffled().prefix(12))
        let perArtist = await withTaskGroup(of: [Track].self) { group in
            var all: [[Track]] = []
            for (n, names) in picked.enumerated() {
                if n >= 4, let done = await group.next(), !done.isEmpty { all.append(done) }   // ≤ 4 in flight
                group.addTask {
                    guard let results = try? await YTMusic.search(names[0]) else { return [] }
                    // Keep only songs really by this artist (search also returns covers and namesakes).
                    let own = results.filter { t in names.contains { matches(t.artist, $0) } }
                    // Prefer original recordings over demos, live takes, remixes and covers.
                    let originals = own.filter { !isVariant($0.title) }
                    return Array((originals.count >= 2 ? originals : own).prefix(6).shuffled().prefix(2))
                }
            }
            for await tracks in group where !tracks.isEmpty { all.append(tracks) }
            return all
        }
        var seen = Set<String>(), tracks: [Track] = []
        // Round-robin so the same artist never plays twice in a row.
        for round in 0..<2 {
            for list in perArtist where round < list.count && seen.insert(list[round].id).inserted {
                tracks.append(list[round])
            }
        }
        guard tracks.count >= 4 else { return nil }
        return Shelf(id: mix.id, title: mix.title, subtitle: mix.subtitle, tracks: tracks)
    }

    /// True if any credited artist ("A & B", "A, B", "A feat. B") is exactly `name`, ignoring case/accents/punctuation.
    nonisolated static func matches(_ artist: String, _ name: String) -> Bool {
        func norm(_ s: String) -> String {
            s.lowercased().folding(options: .diacriticInsensitive, locale: nil).filter { $0.isLetter || $0.isNumber }
        }
        let n = norm(name)
        guard !n.isEmpty else { return false }
        var parts = [artist]
        for sep in [" & ", ", ", " feat. ", " ft. ", " x ", " and ", "、"] {
            parts = parts.flatMap { $0.components(separatedBy: sep) }
        }
        return parts.contains { norm($0) == n }
    }

    nonisolated static func isVariant(_ title: String) -> Bool {
        let t = title.lowercased()
        return ["demo", "live", "remix", " mix)", "instrumental", "karaoke", "acoustic", "ver.", "version)", "cover", "sped up", "slowed"]
            .contains { t.contains($0) } && !t.contains("restored") && !t.contains("remaster")
            || t.contains("demo")
    }

    // MARK: From your listening

    nonisolated private static func personalShelves(liked: [Track], recent: [Track]) async -> [Shelf] {
        var shelves: [Shelf] = []
        if !recent.isEmpty {
            shelves.append(Shelf(id: "recent", title: "Jump Back In", subtitle: "Recently played",
                                 tracks: Array(recent.prefix(20))))
        }
        // Rank artists: a like counts three times as much as a play.
        var score: [String: Int] = [:]
        for t in liked { score[mainArtist(t), default: 0] += 3 }
        for t in recent.prefix(50) { score[mainArtist(t), default: 0] += 1 }
        let known = Set((liked + recent).map(\.id))

        // "Made for You": radios from your latest likes/plays, interleaved, songs you haven't heard.
        let seeds = Array((liked.prefix(2) + recent.prefix(3)).reduce(into: [Track]()) { acc, t in
            if !acc.contains(where: { $0.id == t.id }) { acc.append(t) }
        }.prefix(3))
        if !seeds.isEmpty {
            let radios = await withTaskGroup(of: [Track].self) { group in
                for s in seeds { group.addTask { (try? await YTMusic.radio(for: s.id)) ?? [] } }
                var all: [[Track]] = []
                for await r in group { all.append(Array(r.dropFirst())) }
                return all
            }
            var seen = known, mix: [Track] = []
            for i in 0..<30 {
                for r in radios where i < r.count && seen.insert(r[i].id).inserted { mix.append(r[i]) }
            }
            if mix.count >= 4 {
                shelves.append(Shelf(id: "made-for-you", title: "Made for You",
                                     subtitle: "New finds, painted from what you play", tracks: Array(mix.prefix(24))))
            }
        }

        // "Because you like <artist>" for your top artist.
        if let top = score.max(by: { $0.value < $1.value })?.key, !top.isEmpty,
           let seed = (liked + recent).first(where: { mainArtist($0) == top }),
           let radio = try? await YTMusic.radio(for: seed.id) {
            var sameArtist = 0
            let picks = radio.dropFirst().filter { t in
                if mainArtist(t) == top { sameArtist += 1; return sameArtist <= 3 }
                return true
            }
            if picks.count >= 4 {
                shelves.append(Shelf(id: "because", title: "Because You Like \(top)", subtitle: "Kindred spirits",
                                     tracks: Array(picks.prefix(24))))
            }
        }
        return shelves
    }

    nonisolated static func mainArtist(_ t: Track) -> String { mainArtist(t.artist) }

    nonisolated static func mainArtist(_ credit: String) -> String {
        for sep in [" & ", ", ", " x ", " feat. "] {
            if let r = credit.range(of: sep) { return String(credit[..<r.lowerBound]) }
        }
        return credit
    }
}
