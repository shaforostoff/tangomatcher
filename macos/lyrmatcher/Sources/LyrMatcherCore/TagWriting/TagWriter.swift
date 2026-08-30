import Foundation

/// What happened to one file during a write.
public enum WriteOutcome: Equatable, Sendable {
    /// Lyrics written; the payload holds the number of translations embedded.
    case written(Int)
    /// The file already carries lyrics and "Overwrite" is off.
    case skippedExisting
    /// The file already carries exactly these lyrics.
    case unchanged
    /// Not a taggable format.
    case unsupported
}

public enum TagWriteError: LocalizedError, Equatable {
    case unsupportedTagVersion(String)
    case malformed(String)
    case chunkOffsetOverflow

    public var errorDescription: String? {
        switch self {
        case let .unsupportedTagVersion(version):
            return "Unsupported tag version: \(version)."
        case let .malformed(what):
            return "Malformed \(what)."
        case .chunkOffsetOverflow:
            return "The new tag pushes a chunk offset past 4 GB; this file needs 64-bit offsets."
        }
    }
}

public enum WriteResult: Sendable {
    case done(WriteOutcome)
    /// Already rendered to a message, so the report can cross threads.
    case failed(String)
}

/// One file's result, for the summary the UI shows after a batch write.
public struct WriteReport: Sendable {
    public var file: MusicFile
    public var result: WriteResult

    public init(file: MusicFile, result: WriteResult) {
        self.file = file
        self.result = result
    }
}

public enum TagWriter {

    /// Embeds every translation of a lyrics document into one music file.
    ///
    /// - MP3: one `USLT` frame per translation, each tagged with its own language code, so a
    ///   player can offer the Spanish original and the translations side by side.
    /// - AIFF: the same `USLT` frames, in the file's `ID3 ` chunk.
    /// - FLAC: a single `LYRICS` Vorbis comment holding all translations.
    /// - MP4/M4A: a single `©lyr` atom holding all translations.
    ///
    /// This mirrors what the Qt app wrote, including the CRLF line endings.
    public static func write(
        translations: [Translation],
        to file: MusicFile,
        overwrite: Bool
    ) throws -> WriteOutcome {
        guard !translations.isEmpty else { return .unchanged }

        switch file.format {
        case .mp3:
            return try ID3v2Tagger.write(translations: translations, to: file.url, overwrite: overwrite)
        case .flac:
            return try FLACTagger.write(translations: translations, to: file.url, overwrite: overwrite)
        case .mp4:
            return try MP4Tagger.write(translations: translations, to: file.url, overwrite: overwrite)
        case .aiff:
            return try AIFFTagger.write(translations: translations, to: file.url, overwrite: overwrite)
        }
    }

    /// `write` with the error already captured, for batch loops that must not stop on one bad
    /// file. Safe to call off the main thread.
    public static func report(
        translations: [Translation],
        to file: MusicFile,
        overwrite: Bool
    ) -> WriteReport {
        do {
            let outcome = try write(translations: translations, to: file, overwrite: overwrite)
            return WriteReport(file: file, result: .done(outcome))
        } catch {
            return WriteReport(file: file, result: .failed(error.localizedDescription))
        }
    }
}

/// Writes a replacement next to the original and swaps it in, so an interrupted write can never
/// leave a half-tagged music file behind.
enum AtomicFile {
    static func replace(at url: URL, write body: (FileHandle) throws -> Void) throws {
        let directory = url.deletingLastPathComponent()
        let temporary = directory.appendingPathComponent(
            ".lyrmatcher-\(UUID().uuidString).\(url.pathExtension)"
        )

        guard FileManager.default.createFile(atPath: temporary.path, contents: nil) else {
            throw CocoaError(.fileWriteNoPermission, userInfo: [NSFilePathErrorKey: temporary.path])
        }

        do {
            let handle = try FileHandle(forWritingTo: temporary)
            do {
                try body(handle)
                try handle.close()
            } catch {
                try? handle.close()
                throw error
            }
            _ = try FileManager.default.replaceItemAt(url, withItemAt: temporary)
        } catch {
            try? FileManager.default.removeItem(at: temporary)
            throw error
        }
    }
}
