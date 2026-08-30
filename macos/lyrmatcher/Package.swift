// swift-tools-version:5.9
import PackageDescription

let package = Package(
    name: "LyrMatcher",
    platforms: [.macOS(.v13)],
    targets: [
        .target(name: "LyrMatcherCore"),
        .executableTarget(
            name: "LyrMatcher",
            dependencies: ["LyrMatcherCore"]
        ),
        .testTarget(
            name: "LyrMatcherCoreTests",
            dependencies: ["LyrMatcherCore"]
        ),
    ]
)
