import AppKit
import LyrMatcherCore
import SwiftUI

struct ContentView: View {
    @EnvironmentObject private var model: AppModel
    @FocusState private var filterFocused: Bool

    var body: some View {
        VStack(spacing: 0) {
            HSplitView {
                lyricsList
                    .frame(minWidth: 220, idealWidth: 260)
                matchPane
                    .frame(minWidth: 380)
            }
            Divider()
            statusBar
        }
        .frame(minWidth: 760, minHeight: 480)
        .toolbar { toolbarContent }
        .onReceive(NotificationCenter.default.publisher(for: .focusLyricsFilter)) { _ in
            filterFocused = true
        }
    }

    // MARK: Left pane — the lyrics XML files

    private var lyricsList: some View {
        VStack(spacing: 0) {
            TextField("Filter lyrics files…", text: $model.lyricsFilter)
                .textFieldStyle(.roundedBorder)
                .focused($filterFocused)
                .padding(6)

            ScrollViewReader { proxy in
                List(model.visibleLyricsFiles, selection: $model.selection) { file in
                    Text(file.baseName)
                        .lineLimit(1)
                        .truncationMode(.middle)
                        .tag(file.id)
                }
                .listStyle(.inset(alternatesRowBackgrounds: true))
                .onChange(of: model.revealRequest) { request in
                    guard let request else { return }
                    withAnimation(.easeInOut(duration: 0.15)) {
                        proxy.scrollTo(request.id, anchor: .center)
                    }
                }
            }

            Divider()
            HStack(spacing: 6) {
                Text(folderLabel(model.lyricsFolderPath, fallback: "No lyrics folder"))
                    .font(.caption)
                    .foregroundStyle(.secondary)
                    .lineLimit(1)
                    .truncationMode(.head)
                Spacer()
                Button("Choose…") { chooseFolder(into: \.lyricsFolderPath) }
                    .controlSize(.small)
            }
            .padding(6)
        }
    }

    // MARK: Right pane — lyrics preview, music folder, matches

    private var matchPane: some View {
        VSplitView {
            lyricsPreview
                .frame(minHeight: 140, idealHeight: 260)

            VStack(spacing: 6) {
                HStack(spacing: 6) {
                    TextField("Root path of folder with music", text: $model.musicFolderPath)
                        .textFieldStyle(.roundedBorder)
                    Button("Choose…") { chooseFolder(into: \.musicFolderPath) }
                    Button {
                        model.rescanMusicFolder()
                    } label: {
                        Image(systemName: "arrow.clockwise")
                    }
                    .help("Rescan the music folder")
                    .disabled(model.musicFolderPath.isEmpty)
                }

                TextField("Additionally filter results here", text: $model.resultFilter)
                    .textFieldStyle(.roundedBorder)

                matchList

                HStack {
                    Text(matchSummary)
                        .font(.caption)
                        .foregroundStyle(.secondary)
                    Spacer()
                    Button("Write tags") { model.writeSelected() }
                        .keyboardShortcut(.return, modifiers: .command)
                        .disabled(model.matches.isEmpty
                            || model.translationsToWrite.isEmpty
                            || model.isWriting)
                }
            }
            .padding(6)
            .frame(minHeight: 200)
        }
    }

    private var lyricsPreview: some View {
        ScrollView {
            // Shows exactly what would be written, so the Spanish-only filter is visible.
            Text(model.document.previewText(spanishOnly: model.spanishOnly))
                .font(.system(.body, design: .default))
                .textSelection(.enabled)
                .frame(maxWidth: .infinity, alignment: .leading)
                .padding(8)
        }
        .background(Color(nsColor: .textBackgroundColor))
    }

    private var matchList: some View {
        List(model.matches) { match in
            HStack(spacing: 8) {
                Text(match.file.relativePath)
                    .lineLimit(1)
                    .truncationMode(.head)
                Spacer(minLength: 8)
                if case .fuzzy = match.tier {
                    Text(match.tier.label)
                        .font(.caption2)
                        .padding(.horizontal, 5)
                        .padding(.vertical, 1)
                        .background(Color.orange.opacity(0.25), in: Capsule())
                }
                Text(match.file.format.rawValue)
                    .font(.caption2)
                    .foregroundStyle(.secondary)
            }
            .contentShape(Rectangle())
            .onTapGesture(count: 2) { model.play(match) }
            .contextMenu {
                Button("Play") { model.play(match) }
                Button("Reveal in Finder") { model.revealInFinder(match) }
            }
        }
        .listStyle(.inset(alternatesRowBackgrounds: true))
        .frame(minHeight: 80)
    }

    private var statusBar: some View {
        HStack(spacing: 8) {
            if let progress = model.progress {
                if let fraction = progress.fraction {
                    ProgressView(value: fraction)
                        .frame(width: 140)
                    Text("\(progress.completed)/\(progress.total)")
                        .font(.caption.monospacedDigit())
                        .foregroundStyle(.secondary)
                } else {
                    ProgressView().controlSize(.small)
                }
                Text(progress.label)
                    .font(.caption)
                    .lineLimit(1)
                    .truncationMode(.middle)
                Spacer()
                Button("Cancel") { model.cancelWriting() }
                    .controlSize(.small)
            } else {
                if model.isScanning {
                    ProgressView().controlSize(.small)
                }
                Text(model.status)
                    .font(.caption)
                    .lineLimit(1)
                    .truncationMode(.middle)
                Spacer()
            }
        }
        .padding(.horizontal, 8)
        .padding(.vertical, 4)
        .frame(height: 22)
    }

    @ToolbarContentBuilder
    private var toolbarContent: some ToolbarContent {
        ToolbarItemGroup {
            Toggle("Add minuses", isOn: $model.addMinuses)
                .toggleStyle(.button)
                .help("Require the title to occupy a whole \" - \" separated part of the filename")
            Toggle("Fuzzy", isOn: $model.allowFuzzy)
                .toggleStyle(.button)
                .help("Fall back to edit-distance matching when nothing matches literally")
            Toggle("Overwrite", isOn: $model.overwrite)
                .toggleStyle(.button)
                .help("Replace lyrics that are already present instead of skipping the file")
            Toggle("Spanish only", isOn: $model.spanishOnly)
                .toggleStyle(.button)
                .help("Spanish only (Exclude translations) — write just the spa lyrics, "
                    + "leaving out the English and Russian translations")
        }
    }

    // MARK: Helpers

    private var matchSummary: String {
        let total = model.matches.count
        guard total > 0 else {
            return model.musicIndex.files.isEmpty ? "No music indexed" : "No matches"
        }
        let fuzzy = model.matches.filter { if case .fuzzy = $0.tier { return true } else { return false } }.count
        return fuzzy == 0 ? "\(total) matches" : "\(total) matches (\(fuzzy) fuzzy)"
    }

    private func folderLabel(_ path: String, fallback: String) -> String {
        path.isEmpty ? fallback : (path as NSString).abbreviatingWithTildeInPath
    }

    private func chooseFolder(into keyPath: ReferenceWritableKeyPath<AppModel, String>) {
        let panel = NSOpenPanel()
        panel.canChooseDirectories = true
        panel.canChooseFiles = false
        panel.allowsMultipleSelection = false
        let current = model[keyPath: keyPath]
        if !current.isEmpty { panel.directoryURL = URL(fileURLWithPath: current) }
        guard panel.runModal() == .OK, let url = panel.url else { return }
        model[keyPath: keyPath] = url.path
    }
}

extension Notification.Name {
    static let focusLyricsFilter = Notification.Name("LyrMatcher.focusLyricsFilter")
}
