import Foundation

/// Builds the exact strings the Qt app wrote into tags, so files tagged by either version look
/// the same to players.
public enum LyricsPayload {

    /// Body of one `USLT` frame: `name`, blank line, lyrics, trailing blank line.
    public static func id3Text(for translation: Translation) -> String {
        crlf("\(translation.name)\n\n\(trimmed(translation.contents))\n\n")
    }

    /// `USLT` content descriptor — the translation's title, which is what players show as the
    /// label when a file carries several language variants.
    public static func id3Description(for translation: Translation) -> String {
        translation.name
    }

    /// Single blob used for the FLAC `LYRICS` field and the MP4 `©lyr` atom: every translation
    /// one after another, separated by blank lines.
    public static func combined(_ translations: [Translation]) -> String {
        crlf(
            translations
                .map { "\(trimmed($0.name))\n\n\(trimmed($0.contents))\n\n\n\n" }
                .joined()
        )
    }

    private static func trimmed(_ s: String) -> String {
        s.trimmingCharacters(in: .whitespacesAndNewlines)
    }

    /// Windows players (foobar2000 among them) want CRLF inside lyrics frames.
    private static func crlf(_ s: String) -> String {
        s.replacingOccurrences(of: "\r\n", with: "\n")
            .replacingOccurrences(of: "\r", with: "\n")
            .replacingOccurrences(of: "\n", with: "\r\n")
    }
}
