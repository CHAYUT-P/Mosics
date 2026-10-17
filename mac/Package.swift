// swift-tools-version:5.10
import PackageDescription

let package = Package(
    name: "Mosics",
    platforms: [.macOS(.v14)],
    targets: [
        .executableTarget(
            name: "Mosics",
            path: "Sources/Mosics",
            swiftSettings: [.unsafeFlags(["-Osize"])]
        )
    ]
)
