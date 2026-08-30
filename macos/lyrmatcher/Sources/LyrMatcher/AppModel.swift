import AppKit
import Combine
import Foundation
import LyrMatcherCore

/// Everything the UI binds to. Mirrors `MainWindow`'s state: the two folders, the two filter
/// fields, the three matching toggles, and the current selection.
@MainActor
final class AppModel: ObservableObject {

    // MARK: Folders

    @Published private(set) var lyricsFolder = LyricsFolder.empty
    @Published private(set) var musicIndex = MusicIndex.empty
    @Published private(set) var isScanning = false

    @Published var lyricsFolderPath: String = Defaults.lyricsFolderPath {
        didSet {
            guard lyricsFolderPath != oldValue else { return }
            Defaults.lyricsFolderPath = lyricsFolderPath
            reloadLyricsFolder()
        }
    }

    @Published var musicFolderPath: String = Defaults.musicFolderPath {
        didSet {
            guard musicFolderPath != oldValue else { return }
            Defaults.musicFolderPath = musicFolderPath
            rescanMusicFolder()
        }
    }

    // MARK: Filters and toggles

    @Published var lyricsFilter = "" { didSet { refreshVisibleLyricsFiles() } }
    @Published var resultFilter = "" { didSet { refreshMatches() } }

    @Published var addMinuses = Defaults.addMinuses {
        didSet { Defaults.addMinuses = addMinuses; refreshMatches() }
    }
    @Published var overwrite = Defaults.overwrite {
        didSet { Defaults.overwrite = overwrite }
    }
    @Published var allowFuzzy = Defaults.allowFuzzy {
        didSet { Defaults.allowFuzzy = allowFuzzy; refreshMatches() }
    }

    // MARK: Selection and derived state

    @Published private(set) var visibleLyricsFiles: [LyricsFile] = []
    @Published var selection: LyricsFile.ID? { didSet { reloadSelection() } }
    @Published private(set) var document = LyricsDocument()
    @Published private(set) var matches: [MatchedFile] = []
    @Published private(set) var status = ""

    private var scanTask: Task<Void, Never>?

    init() {
        reloadLyricsFolder()
        rescanMusicFolder()
    }

    var selectedFile: LyricsFile? {
        guard let selection else { return nil }
        return lyricsFolder.files.first { $0.id == selection }
    }

    // MARK: Folder loading

    func reloadLyricsFolder() {
        guard !lyricsFolderPath.isEmpty else {
            lyricsFolder = .empty
            refreshVisibleLyricsFiles()
            return
        }
        lyricsFolder = LyricsFolder.scan(root: URL(fileURLWithPath: lyricsFolderPath))
        refreshVisibleLyricsFiles()
        if selectedFile == nil { selection = visibleLyricsFiles.first?.id }
        status = "\(lyricsFolder.files.count) lyrics files."
    }

    func rescanMusicFolder() {
        scanTask?.cancel()
        guard !musicFolderPath.isEmpty else {
            musicIndex = .empty
            refreshMatches()
            return
        }

        let root = URL(fileURLWithPath: musicFolderPath)
        isScanning = true
        scanTask = Task {
            let index = await Task.detached(priority: .userInitiated) {
                MusicIndex.scan(root: root, isCancelled: { Task.isCancelled })
            }.value
            guard !Task.isCancelled else { return }
            self.musicIndex = index
            self.isScanning = false
            self.status = "Indexed \(index.files.count) music files."
            self.refreshMatches()
        }
    }

    private func refreshVisibleLyricsFiles() {
        if lyricsFilter.isEmpty {
            visibleLyricsFiles = lyricsFolder.files
        } else {
            visibleLyricsFiles = lyricsFolder.files.filter {
                $0.baseName.range(of: lyricsFilter, options: .caseInsensitive) != nil
            }
        }
        if let selection, !visibleLyricsFiles.contains(where: { $0.id == selection }) {
            self.selection = visibleLyricsFiles.first?.id
        }
    }

    // MARK: Selection

    private func reloadSelection() {
        guard let file = selectedFile else {
            document = LyricsDocument()
            matches = []
            return
        }
        document = (try? LyricsDocument.load(contentsOf: file.url)) ?? LyricsDocument()
        refreshMatches()
    }

    private func refreshMatches() {
        guard let file = selectedFile else {
            matches = []
            return
        }
        matches = musicIndex.matches(
            for: TitleMatcher.plan(xmlBaseName: file.baseName, addMinuses: addMinuses),
            allowFuzzy: allowFuzzy,
            resultFilter: resultFilter
        )
    }

    /// Matches for an arbitrary lyrics file, used by the batch operations.
    private func matches(for file: LyricsFile, addMinuses: Bool) -> [MatchedFile] {
        musicIndex.matches(
            for: TitleMatcher.plan(xmlBaseName: file.baseName, addMinuses: addMinuses),
            allowFuzzy: allowFuzzy,
            resultFilter: resultFilter
        )
    }

    // MARK: Actions

    func play(_ match: MatchedFile) {
        NSWorkspace.shared.open(match.file.url)
    }

    func revealInFinder(_ match: MatchedFile) {
        NSWorkspace.shared.activateFileViewerSelecting([match.file.url])
    }

    /// Selects the next lyrics file that has at least one matching music file.
    ///
    /// Like the Qt version this searches with the boundary markers forced on, so it skips over
    /// entries that would only match loosely, then restores the user's own setting for display.
    @discardableResult
    func findNextWithMatches() -> Bool {
        guard !visibleLyricsFiles.isEmpty else { return false }
        let start = visibleLyricsFiles.firstIndex { $0.id == selection }.map { $0 + 1 } ?? 0

        for index in start..<visibleLyricsFiles.count {
            let candidate = visibleLyricsFiles[index]
            if !matches(for: candidate, addMinuses: true).isEmpty {
                selection = candidate.id
                return true
            }
        }
        status = "No further lyrics files have matching music files."
        return false
    }

    func writeSelected() {
        guard let file = selectedFile else { return }
        let reports = write(document: document, to: matches)
        status = summary(of: reports, prefix: file.baseName)
    }

    /// Write ALL edits every matching file in the library at once, so make the user say yes.
    func confirmWriteAll() {
        let candidates = visibleLyricsFiles.filter { !matches(for: $0, addMinuses: addMinuses).isEmpty }
        guard !candidates.isEmpty else {
            status = "No lyrics files have matching music files."
            return
        }

        let overwriteNote = overwrite
            ? "Overwrite is ON — lyrics already in those files will be replaced."
            : "Overwrite is off — files that already have lyrics are skipped."

        let alert = NSAlert()
        alert.messageText = "Write lyrics for \(candidates.count) titles?"
        alert.informativeText = """
            This embeds lyrics into every music file matching the \(candidates.count) \
            currently listed lyrics files.
            \(overwriteNote)
            """
        alert.alertStyle = .warning
        alert.addButton(withTitle: "Write All")
        alert.addButton(withTitle: "Cancel")
        guard alert.runModal() == .alertFirstButtonReturn else { return }

        writeAll()
    }

    /// Writes lyrics for every lyrics file that has matches, in one pass.
    func writeAll() {
        var reports: [WriteReport] = []
        var seenStems = Set<String>()

        for file in visibleLyricsFiles {
            // `Malena_.xml` and `Malena.xml` normalise to the same stem; writing both would just
            // have the second overwrite the first.
            let stem = TextNormalization.simplify(TitleMatcher.bareTitle(xmlBaseName: file.baseName))
            guard seenStems.insert(stem).inserted else { continue }

            let hits = matches(for: file, addMinuses: addMinuses)
            guard !hits.isEmpty else { continue }
            guard let document = try? LyricsDocument.load(contentsOf: file.url),
                  !document.isEmpty
            else { continue }

            reports += write(document: document, to: hits)
        }

        status = summary(of: reports, prefix: "All")
    }

    private func write(document: LyricsDocument, to matches: [MatchedFile]) -> [WriteReport] {
        matches.map { match in
            do {
                let outcome = try TagWriter.write(
                    translations: document.translations,
                    to: match.file,
                    overwrite: overwrite
                )
                return WriteReport(file: match.file, outcome: .success(outcome))
            } catch {
                return WriteReport(file: match.file, outcome: .failure(error))
            }
        }
    }

    private func summary(of reports: [WriteReport], prefix: String) -> String {
        guard !reports.isEmpty else { return "\(prefix): nothing to write." }

        var written = 0, skipped = 0, unchanged = 0
        var failures: [String] = []

        for report in reports {
            switch report.outcome {
            case .success(.written): written += 1
            case .success(.skippedExisting): skipped += 1
            case .success(.unchanged): unchanged += 1
            case .success(.unsupported): skipped += 1
            case let .failure(error):
                failures.append("\(report.file.relativePath): \(error.localizedDescription)")
            }
        }

        var parts = ["\(prefix): \(written) written"]
        if skipped > 0 { parts.append("\(skipped) skipped (already tagged)") }
        if unchanged > 0 { parts.append("\(unchanged) unchanged") }
        if !failures.isEmpty { parts.append("\(failures.count) failed — \(failures[0])") }
        return parts.joined(separator: ", ") + "."
    }
}

/// `QSettings` equivalent.
enum Defaults {
    private static let store = UserDefaults.standard

    static var lyricsFolderPath: String {
        get { store.string(forKey: "lyricsFolder") ?? "" }
        set { store.set(newValue, forKey: "lyricsFolder") }
    }
    static var musicFolderPath: String {
        get { store.string(forKey: "folderToMatch") ?? "" }
        set { store.set(newValue, forKey: "folderToMatch") }
    }
    static var addMinuses: Bool {
        get { store.bool(forKey: "addMinuses") }
        set { store.set(newValue, forKey: "addMinuses") }
    }
    static var overwrite: Bool {
        get { store.bool(forKey: "overwrite") }
        set { store.set(newValue, forKey: "overwrite") }
    }
    static var allowFuzzy: Bool {
        get { store.bool(forKey: "allowFuzzy") }
        set { store.set(newValue, forKey: "allowFuzzy") }
    }
}
