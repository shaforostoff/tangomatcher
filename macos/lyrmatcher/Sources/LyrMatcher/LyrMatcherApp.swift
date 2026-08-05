import AppKit
import SwiftUI

@main
struct LyrMatcherApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) private var delegate
    @StateObject private var model = AppModel()

    var body: some Scene {
        WindowGroup("LyrMatcher") {
            ContentView()
                .environmentObject(model)
                .background(WindowConfigurator(autosaveName: "LyrMatcherMainWindow"))
        }
        .defaultSize(width: 980, height: 620)
        .commands {
            CommandGroup(replacing: .newItem) {}

            CommandMenu("Match") {
                Button("Focus Filter") {
                    NotificationCenter.default.post(name: .focusLyricsFilter, object: nil)
                }
                .keyboardShortcut("l", modifiers: .command)

                Button("Find Next With Matches") {
                    model.findNextWithMatches()
                }
                .keyboardShortcut("g", modifiers: .command)

                Divider()

                Button("Write Lyrics") { model.writeSelected() }
                    .keyboardShortcut(.return, modifiers: .command)

                Button("Write Lyrics and Find Next") {
                    model.writeSelected()
                    model.findNextWithMatches()
                }
                .keyboardShortcut(.return, modifiers: [.command, .shift])

                Button("Write ALL Lyrics…") { model.confirmWriteAll() }

                Divider()

                Toggle("Add Minuses", isOn: Binding(
                    get: { model.addMinuses }, set: { model.addMinuses = $0 }
                ))
                Toggle("Fuzzy Matching", isOn: Binding(
                    get: { model.allowFuzzy }, set: { model.allowFuzzy = $0 }
                ))
                Toggle("Overwrite Existing Lyrics", isOn: Binding(
                    get: { model.overwrite }, set: { model.overwrite = $0 }
                ))

                Divider()

                Button("Rescan Music Folder") { model.rescanMusicFolder() }
                    .keyboardShortcut("r", modifiers: .command)
            }
        }
    }
}

final class AppDelegate: NSObject, NSApplicationDelegate {
    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }

    func applicationDidFinishLaunching(_ notification: Notification) {
        NSApp.setActivationPolicy(.regular)
        NSApp.activate(ignoringOtherApps: true)
    }
}

/// Gives the window a frame autosave name so its size and position survive a relaunch, the way
/// the Qt version stored `geometry` in QSettings.
private struct WindowConfigurator: NSViewRepresentable {
    let autosaveName: String

    func makeNSView(context: Context) -> NSView {
        let view = NSView()
        DispatchQueue.main.async {
            guard let window = view.window else { return }
            window.setFrameAutosaveName(autosaveName)
        }
        return view
    }

    func updateNSView(_ nsView: NSView, context: Context) {}
}
