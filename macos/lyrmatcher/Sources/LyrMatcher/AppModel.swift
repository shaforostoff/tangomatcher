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
    @Published var spanishOnly = Defaults.spanishOnly {
        didSet { Defaults.spanishOnly = spanishOnly }
    }

    // MARK: Selection and derived state

    @Published private(set) var visibleLyricsFiles: [LyricsFile] = []
    @Published var selection: LyricsFile.ID? { didSet { reloadSelection() } }
    @Published private(set) var document = LyricsDocument()
    @Published private(set) var matches: [MatchedFile] = []
    @Published private(set) var status = ""

    /// Non-nil while a write is running. `total == 0` means the work list is still being built.
    @Published var progress: BatchProgress?
    var isWriting: Bool { progress != nil }

    private var scanTask: Task<Void, Never>?
    private var writeTask: Task<Void, Never>?

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
    /// Searches with the user's own "Add minuses" setting. Qt forced the boundary markers on for
    /// the scan, which meant it walked past every title the results list would have shown.
    @discardableResult
    func findNextWithMatches() -> Bool {
        guard !visibleLyricsFiles.isEmpty else { return false }
        let start = visibleLyricsFiles.firstIndex { $0.id == selection }.map { $0 + 1 } ?? 0

        for index in start..<visibleLyricsFiles.count {
            let candidate = visibleLyricsFiles[index]
            if !matches(for: candidate, addMinuses: addMinuses).isEmpty {
                selection = candidate.id
                return true
            }
        }
        status = start >= visibleLyricsFiles.count
            ? "At the end of the list — nothing further to search."
            : "No further lyrics files have matching music files."
        return false
    }

    /// The translations that would actually be embedded, after the Spanish-only filter.
    var translationsToWrite: [LyrMatcherCore.Translation] {
        document.translations(spanishOnly: spanishOnly)
    }

    func writeSelected() {
        guard let file = selectedFile else { return }
        guard !translationsToWrite.isEmpty else {
            status = "\(file.baseName): no Spanish lyrics in this file."
            return
        }
        run(
            jobs: [WriteJob(title: file.baseName, lyricsURL: file.url, targets: matches.map(\.file))],
            label: file.baseName
        )
    }

    /// Write ALL edits every matching file in the library at once, so make the user say yes.
    func confirmWriteAll() {
        guard !isWriting else { return }
        guard !visibleLyricsFiles.isEmpty else {
            status = "No lyrics files listed."
            return
        }

        let overwriteNote = overwrite
            ? "Overwrite is ON — lyrics already in those files will be replaced."
            : "Overwrite is off — files that already have lyrics are skipped."
        let languageNote = spanishOnly
            ? "Spanish only is ON — translations are left out, and titles with no Spanish lyrics are skipped."
            : "All translations in each file will be written."

        let alert = NSAlert()
        alert.messageText = "Write lyrics for every matching file?"
        alert.informativeText = """
            This scans the \(visibleLyricsFiles.count) currently listed lyrics files and embeds \
            lyrics into every music file they match.
            \(overwriteNote)
            \(languageNote)
            """
        alert.alertStyle = .warning
        alert.addButton(withTitle: "Write All")
        alert.addButton(withTitle: "Cancel")
        guard alert.runModal() == .alertFirstButtonReturn else { return }

        writeAll()
    }

    /// Writes lyrics for every listed lyrics file that has matches.
    func writeAll() {
        run(jobs: nil, label: "All")
    }

    func cancelWriting() {
        writeTask?.cancel()
        progress?.label = "Cancelling…"
    }

    // MARK: Batch engine

    /// One lyrics file and the music files it will be written into.
    private struct WriteJob: Sendable {
        let title: String
        let lyricsURL: URL
        let targets: [MusicFile]
    }

    /// Runs a batch off the main thread, publishing progress after every file.
    ///
    /// - Parameter jobs: `nil` means "work the whole visible list", which is itself expensive
    ///   enough on a large library to be worth doing in the background.
    private func run(jobs: [WriteJob]?, label: String) {
        guard !isWriting else { return }

        progress = BatchProgress(completed: 0, total: jobs?.reduce(0) { $0 + $1.targets.count } ?? 0,
                                 label: jobs == nil ? "Finding matches…" : label)

        // Snapshot everything the background work needs; all of it is Sendable.
        let files = visibleLyricsFiles
        let index = musicIndex
        let addMinuses = self.addMinuses
        let allowFuzzy = self.allowFuzzy
        let resultFilter = self.resultFilter
        let spanishOnly = self.spanishOnly
        let overwrite = self.overwrite

        writeTask = Task {
            let work: [WriteJob]
            if let jobs {
                work = jobs
            } else {
                work = await Task.detached(priority: .userInitiated) {
                    Self.buildJobs(
                        from: files, index: index, addMinuses: addMinuses,
                        allowFuzzy: allowFuzzy, resultFilter: resultFilter
                    )
                }.value
            }

            guard !Task.isCancelled else { return finish([], 0, label: label, cancelled: true) }

            progress?.total = work.reduce(0) { $0 + $1.targets.count }

            var reports: [WriteReport] = []
            var withoutSpanish = 0

            for job in work {
                guard !Task.isCancelled else { break }

                // Parsing the XML is cheap but there can be hundreds of them.
                let translations = await Task.detached(priority: .userInitiated) {
                    (try? LyricsDocument.load(contentsOf: job.lyricsURL))?
                        .translations(spanishOnly: spanishOnly) ?? []
                }.value

                guard !translations.isEmpty else {
                    withoutSpanish += 1
                    progress?.completed += job.targets.count
                    continue
                }

                for target in job.targets {
                    guard !Task.isCancelled else { break }
                    progress?.label = "\(job.title) → \(target.relativePath)"

                    // The expensive part: whole-file rewrites, several MB each.
                    let report = await Task.detached(priority: .userInitiated) {
                        TagWriter.report(
                            translations: translations, to: target, overwrite: overwrite
                        )
                    }.value

                    reports.append(report)
                    progress?.completed += 1
                }
            }

            finish(reports, withoutSpanish, label: label, cancelled: Task.isCancelled)
        }
    }

    /// Pairs every lyrics file that has matches with its music files. Pure, so it can run
    /// off the main actor.
    private nonisolated static func buildJobs(
        from files: [LyricsFile],
        index: MusicIndex,
        addMinuses: Bool,
        allowFuzzy: Bool,
        resultFilter: String
    ) -> [WriteJob] {
        var jobs: [WriteJob] = []
        var seenStems = Set<String>()

        for file in files {
            // `Malena_.xml` and `Malena.xml` normalise to the same stem; writing both would just
            // have the second overwrite the first.
            let stem = TextNormalization.simplify(TitleMatcher.bareTitle(xmlBaseName: file.baseName))
            guard seenStems.insert(stem).inserted else { continue }

            let hits = index.matches(
                for: TitleMatcher.plan(xmlBaseName: file.baseName, addMinuses: addMinuses),
                allowFuzzy: allowFuzzy,
                resultFilter: resultFilter
            )
            guard !hits.isEmpty else { continue }
            jobs.append(WriteJob(title: file.baseName, lyricsURL: file.url, targets: hits.map(\.file)))
        }
        return jobs
    }

    private func finish(
        _ reports: [WriteReport],
        _ withoutSpanish: Int,
        label: String,
        cancelled: Bool
    ) {
        writeTask = nil
        progress = nil
        status = summary(of: reports, prefix: label, withoutSpanish: withoutSpanish)
        if cancelled { status = "Cancelled. " + status }
    }

    private func summary(
        of reports: [WriteReport],
        prefix: String,
        withoutSpanish: Int = 0
    ) -> String {
        guard !reports.isEmpty else {
            return withoutSpanish > 0
                ? "\(prefix): nothing to write — \(withoutSpanish) titles have no Spanish lyrics."
                : "\(prefix): nothing to write."
        }

        var written = 0, skipped = 0, unchanged = 0
        var failures: [String] = []

        for report in reports {
            switch report.result {
            case .done(.written): written += 1
            case .done(.skippedExisting): skipped += 1
            case .done(.unchanged): unchanged += 1
            case .done(.unsupported): skipped += 1
            case let .failed(message):
                failures.append("\(report.file.relativePath): \(message)")
            }
        }

        var parts = ["\(prefix): \(written) written"]
        if skipped > 0 { parts.append("\(skipped) skipped (already tagged)") }
        if unchanged > 0 { parts.append("\(unchanged) unchanged") }
        if withoutSpanish > 0 { parts.append("\(withoutSpanish) titles have no Spanish lyrics") }
        if !failures.isEmpty { parts.append("\(failures.count) failed — \(failures[0])") }
        return parts.joined(separator: ", ") + "."
    }
}

/// Progress of a running write, shown in the status bar.
struct BatchProgress {
    var completed: Int
    var total: Int
    var label: String

    /// `nil` while the work list is still being built, which drives an indeterminate bar.
    var fraction: Double? {
        total > 0 ? Double(completed) / Double(total) : nil
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
    /// On by default: requiring the title to be a whole ` - ` separated field is what stops
    /// `Nada` matching `Nada mas que un corazon`, and it costs almost no coverage.
    static var addMinuses: Bool {
        get { store.object(forKey: "addMinuses") as? Bool ?? true }
        set { store.set(newValue, forKey: "addMinuses") }
    }
    static var overwrite: Bool {
        get { store.bool(forKey: "overwrite") }
        set { store.set(newValue, forKey: "overwrite") }
    }
    static var spanishOnly: Bool {
        get { store.bool(forKey: "spanishOnly") }
        set { store.set(newValue, forKey: "spanishOnly") }
    }
    static var allowFuzzy: Bool {
        get { store.bool(forKey: "allowFuzzy") }
        set { store.set(newValue, forKey: "allowFuzzy") }
    }
}
