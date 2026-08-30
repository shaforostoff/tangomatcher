import Foundation

/// Filename normalisation used on both sides of a title match.
///
/// This is the Swift counterpart of `utils.cpp`'s `deaccent()` / `simplify()` in the Qt app,
/// extended so that the differences the user actually hits between an XML title and a music
/// filename disappear: accents (`ñ`→`n`), underscores used instead of spaces, missing commas
/// or exclamation marks, typographic quotes and dashes, and casing.
///
/// Two characters are deliberately *kept*: `-` and `()`. The `- `/` -` markers baked into the
/// XML filenames encode "title starts / ends a ` - ` separated segment", and the parenthesised
/// distinguisher variant (`Title (`) relies on the brace surviving. See `TitleMatcher`.
public enum TextNormalization {

    /// Characters dropped outright — punctuation that is routinely present in one name and
    /// absent from the other.
    private static let droppedScalars: Set<Unicode.Scalar> = [
        ".", ",", "!", "?", "¡", "¿",
        "\"", "'", "\u{2018}", "\u{2019}", "\u{201C}", "\u{201D}", "\u{00AB}", "\u{00BB}",
        ";", ":", "`", "\u{00B4}",
    ]

    /// Characters folded to a plain ASCII hyphen.
    private static let hyphenScalars: Set<Unicode.Scalar> = [
        "\u{2010}", "\u{2011}", "\u{2012}", "\u{2013}", "\u{2014}", "\u{2015}", "\u{2212}",
    ]

    /// Characters folded to a plain space. Underscores are the common stand-in for spaces in
    /// filenames that came off a filesystem that disliked them.
    private static let spaceScalars: Set<Unicode.Scalar> = ["_", "\u{00A0}"]

    /// Strips inverted/regular `!` and `?` then removes diacritics, so `ñ`→`n`, `á`→`a`,
    /// `ü`→`u`, `ç`→`c`. Mirrors `deaccent()` in `lyrmatcher/utils.cpp`.
    public static func deaccent(_ input: String) -> String {
        var s = input
        for ch in ["¡", "!", "¿", "?"] {
            s = s.replacingOccurrences(of: ch, with: "")
        }
        return s.folding(options: [.diacriticInsensitive], locale: Locale(identifier: "en_US_POSIX"))
    }

    /// Full normalisation: deaccent, fold underscores/dashes/quotes, drop noise punctuation,
    /// collapse whitespace runs, lowercase.
    public static func simplify(_ input: String) -> String {
        var out = String.UnicodeScalarView()
        out.reserveCapacity(input.unicodeScalars.count)

        var pendingSpace = false
        var wroteAnything = false

        for scalar in deaccent(input).unicodeScalars {
            if droppedScalars.contains(scalar) { continue }

            let isSpace = spaceScalars.contains(scalar)
                || CharacterSet.whitespacesAndNewlines.contains(scalar)

            if isSpace {
                // Defer: collapses runs and drops leading/trailing whitespace in one pass.
                if wroteAnything { pendingSpace = true }
                continue
            }

            if pendingSpace {
                out.append(" ")
                pendingSpace = false
            }

            if hyphenScalars.contains(scalar) {
                out.append("-")
            } else {
                out.append(scalar)
            }
            wroteAnything = true
        }

        return String(out).lowercased()
    }

    /// Removes the trailing `_` run the lyrics collection uses to park an alternate version of
    /// a title (`Malena_.xml` next to `Malena.xml`).
    public static func stripVariantSuffix(_ input: String) -> String {
        var s = Substring(input)
        while s.hasSuffix("_") { s = s.dropLast() }
        return String(s)
    }
}
