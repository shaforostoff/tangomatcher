import Foundation

public enum AudioFormat: String, Sendable {
    case mp3
    case mp4   // .m4a / .mp4 / .m4b
    case flac
    case aiff  // .aif / .aiff / .aifc

    public static func from(pathExtension: String) -> AudioFormat? {
        switch pathExtension.lowercased() {
        case "mp3": return .mp3
        case "m4a", "mp4", "m4b": return .mp4
        case "flac": return .flac
        case "aif", "aiff", "aifc": return .aiff
        default: return nil
        }
    }
}

/// One indexed music file.
public struct MusicFile: Identifiable, Hashable, Sendable {
    public let url: URL
    /// Path relative to the scanned root, shown in the results list.
    public let relativePath: String
    /// Filename without extension — what the title is matched against.
    public let baseName: String
    public let normalizedBaseName: String
    public let format: AudioFormat

    public var id: String { relativePath }

    public init(
        url: URL,
        relativePath: String,
        baseName: String,
        normalizedBaseName: String,
        format: AudioFormat
    ) {
        self.url = url
        self.relativePath = relativePath
        self.baseName = baseName
        self.normalizedBaseName = normalizedBaseName
        self.format = format
    }

    /// Builds an entry for a single file, deriving the derived fields. Returns `nil` for
    /// extensions LyrMatcher cannot tag.
    public init?(url: URL, relativeTo root: URL? = nil) {
        guard let format = AudioFormat.from(pathExtension: url.pathExtension) else { return nil }
        let baseName = url.deletingPathExtension().lastPathComponent
        let path = url.standardizedFileURL.path
        let prefix = root.map { $0.standardizedFileURL.path + "/" }
        self.init(
            url: url,
            relativePath: prefix.flatMap { path.hasPrefix($0) ? String(path.dropFirst($0.count)) : nil }
                ?? url.lastPathComponent,
            baseName: baseName,
            normalizedBaseName: TextNormalization.simplify(baseName),
            format: format
        )
    }
}

/// A matched file plus why it matched.
public struct MatchedFile: Identifiable, Hashable, Sendable {
    public let file: MusicFile
    public let tier: MatchTier

    public var id: String { file.id }
}

/// A recursive, cached scan of the music root.
///
/// The Qt app re-walked the whole tree for every XML file it displayed. Building the index once
/// and matching against precomputed normalised names keeps selection instant on large libraries.
public struct MusicIndex: Sendable {
    public let root: URL
    public let files: [MusicFile]
    /// Files skipped because their extension is not one of the taggable formats.
    public let skippedCount: Int

    public init(root: URL, files: [MusicFile], skippedCount: Int = 0) {
        self.root = root
        self.files = files
        self.skippedCount = skippedCount
    }

    public static let empty = MusicIndex(root: URL(fileURLWithPath: "/"), files: [])

    public static func scan(root: URL, isCancelled: () -> Bool = { false }) -> MusicIndex {
        let fm = FileManager.default
        let rootPath = root.standardizedFileURL.path
        let prefix = rootPath.hasSuffix("/") ? rootPath : rootPath + "/"

        guard let enumerator = fm.enumerator(
            at: root,
            includingPropertiesForKeys: [.isRegularFileKey],
            options: [.skipsHiddenFiles, .skipsPackageDescendants]
        ) else {
            return MusicIndex(root: root, files: [])
        }

        var files: [MusicFile] = []
        var skipped = 0

        for case let url as URL in enumerator {
            if isCancelled() { break }
            guard (try? url.resourceValues(forKeys: [.isRegularFileKey]))?.isRegularFile == true else {
                continue
            }
            guard let format = AudioFormat.from(pathExtension: url.pathExtension) else {
                skipped += 1
                continue
            }

            let path = url.standardizedFileURL.path
            let relative = path.hasPrefix(prefix) ? String(path.dropFirst(prefix.count)) : path
            let baseName = url.deletingPathExtension().lastPathComponent

            files.append(
                MusicFile(
                    url: url,
                    relativePath: relative,
                    baseName: baseName,
                    normalizedBaseName: TextNormalization.simplify(baseName),
                    format: format
                )
            )
        }

        files.sort { $0.relativePath.localizedStandardCompare($1.relativePath) == .orderedAscending }
        return MusicIndex(root: root, files: files, skippedCount: skipped)
    }

    /// All files matching one lyrics file, best tier first.
    ///
    /// - Parameter resultFilter: the extra free-text filter from the UI, applied case
    ///   insensitively to the relative path exactly like `QStringList::filter` did.
    public func matches(
        for plan: MatchPlan,
        allowFuzzy: Bool,
        resultFilter: String = ""
    ) -> [MatchedFile] {
        guard plan.isUsable else { return [] }

        var result: [MatchedFile] = []
        for file in files {
            guard let tier = TitleMatcher.tier(
                forNormalizedFileName: file.normalizedBaseName,
                plan: plan,
                allowFuzzy: allowFuzzy
            ) else { continue }
            result.append(MatchedFile(file: file, tier: tier))
        }

        if !resultFilter.isEmpty {
            result = result.filter {
                $0.file.relativePath.range(of: resultFilter, options: .caseInsensitive) != nil
            }
        }

        result.sort {
            $0.tier == $1.tier
                ? $0.file.relativePath.localizedStandardCompare($1.file.relativePath) == .orderedAscending
                : $0.tier < $1.tier
        }
        return result
    }
}

/// The list of `*.xml` lyrics files in the chosen lyrics folder.
public struct LyricsFolder: Sendable {
    public let root: URL
    public let files: [LyricsFile]

    public static let empty = LyricsFolder(root: URL(fileURLWithPath: "/"), files: [])

    public init(root: URL, files: [LyricsFile]) {
        self.root = root
        self.files = files
    }

    public static func scan(root: URL) -> LyricsFolder {
        let contents = (try? FileManager.default.contentsOfDirectory(
            at: root,
            includingPropertiesForKeys: [.isRegularFileKey],
            options: [.skipsHiddenFiles]
        )) ?? []

        let files = contents
            .filter { $0.pathExtension.lowercased() == "xml" }
            .map { LyricsFile(url: $0) }
            .sorted { $0.baseName.localizedStandardCompare($1.baseName) == .orderedAscending }

        return LyricsFolder(root: root, files: files)
    }
}

public struct LyricsFile: Identifiable, Hashable, Sendable {
    public let url: URL
    /// Filename without the `.xml` extension — the literal search stem before markers.
    public let baseName: String

    public var id: String { url.path }

    public init(url: URL) {
        self.url = url
        self.baseName = url.deletingPathExtension().lastPathComponent
    }
}
