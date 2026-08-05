import Foundation

/// How strongly a music file matched the lyrics file.
public enum MatchTier: Comparable, Hashable, Sendable {
    /// The normalised search stem occurs verbatim in the normalised filename — the Qt app's
    /// `*stem*` glob, run over normalised text.
    case literal
    /// No literal hit; a ` - ` separated segment of the filename is within the edit-distance
    /// threshold of the bare title.
    case fuzzy(distance: Int)

    public static func < (lhs: MatchTier, rhs: MatchTier) -> Bool {
        switch (lhs, rhs) {
        case (.literal, .literal): return false
        case (.literal, .fuzzy): return true
        case (.fuzzy, .literal): return false
        case let (.fuzzy(a), .fuzzy(b)): return a < b
        }
    }

    public var label: String {
        switch self {
        case .literal: return "exact"
        case let .fuzzy(distance): return "fuzzy ~\(distance)"
        }
    }
}

/// Everything needed to test music filenames against one lyrics XML file.
public struct MatchPlan: Sendable {
    /// The title with the `- ` / ` -` markers and the `_` variant suffix removed.
    public let bareTitle: String
    /// Normalised `bareTitle` — the string actually looked for in a filename.
    public let normalizedBareTitle: String
    /// The title must start the filename or a ` - ` separated field.
    public let requireSegmentStart: Bool
    /// The title must end the filename or a ` - ` separated field.
    public let requireSegmentEnd: Bool

    public var isUsable: Bool { !normalizedBareTitle.isEmpty }
}

public enum TitleMatcher {

    /// Ratio of edit distance to title length below which a fuzzy hit is accepted. Same value
    /// the discography matcher in `src/mainwindow.cpp` used.
    public static let fuzzyThreshold = 0.2

    /// Builds the match rule for one XML file.
    ///
    /// The `- ` / ` -` markers in the XML filename are read as boundary constraints rather than
    /// as literal text. Qt spliced them into a `*stem*` glob, which silently requires something
    /// to follow the title — so `Una carta -.xml` could never match `014 - Una carta.flac`,
    /// where the title ends the name. Treating them as constraints keeps what they are for
    /// (`- Nada` must not match `Nada mas`) and works at either end of the filename.
    ///
    /// - Parameters:
    ///   - xmlBaseName: XML filename with the `.xml` extension already removed.
    ///   - addMinuses: the Qt "Add minuses" toggle — applies both constraints to a title whose
    ///     filename carries no markers of its own.
    public static func plan(xmlBaseName: String, addMinuses: Bool) -> MatchPlan {
        let bare = bareTitle(xmlBaseName: xmlBaseName)
        return MatchPlan(
            bareTitle: bare,
            normalizedBareTitle: TextNormalization.simplify(bare),
            requireSegmentStart: addMinuses || xmlBaseName.hasPrefix("- "),
            requireSegmentEnd: addMinuses || xmlBaseName.hasSuffix(" -")
        )
    }

    /// Strips the collection's filename decorations: leading `- `, trailing ` -`, trailing `_`.
    public static func bareTitle(xmlBaseName: String) -> String {
        var s = Substring(xmlBaseName)
        if s.hasPrefix("- ") { s = s.dropFirst(2) }
        if s.hasSuffix(" -") { s = s.dropLast(2) }
        while s.hasSuffix("_") { s = s.dropLast() }
        return String(s).trimmingCharacters(in: .whitespaces)
    }

    /// Tests one candidate. Returns `nil` when it does not match at all.
    ///
    /// - Parameter allowFuzzy: enables the edit-distance fallback, only consulted when no
    ///   literal stem matched.
    public static func tier(
        forNormalizedFileName normalized: String,
        plan: MatchPlan,
        allowFuzzy: Bool
    ) -> MatchTier? {
        if literalMatch(in: normalized, plan: plan) { return .literal }

        guard allowFuzzy, !plan.normalizedBareTitle.isEmpty else { return nil }
        guard let distance = bestSegmentDistance(
            in: normalized,
            to: plan.normalizedBareTitle
        ) else { return nil }

        let ratio = Double(distance) / Double(plan.normalizedBareTitle.count)
        return ratio < fuzzyThreshold ? .fuzzy(distance: distance) : nil
    }

    /// True when the title occurs in the filename at a position satisfying the plan's boundary
    /// constraints. Every occurrence is tried, not just the first — `Poema` in
    /// `Poemas - Poema - 1935` matches on the second one.
    static func literalMatch(in name: String, plan: MatchPlan) -> Bool {
        let title = plan.normalizedBareTitle
        guard !title.isEmpty else { return false }

        var searchFrom = name.startIndex
        while let found = name.range(of: title, range: searchFrom..<name.endIndex) {
            if (!plan.requireSegmentStart || isSegmentStart(name, found.lowerBound)),
               (!plan.requireSegmentEnd || isSegmentEnd(name, found.upperBound)) {
                return true
            }
            searchFrom = name.index(after: found.lowerBound)
        }
        return false
    }

    /// Start of the name, or just after a ` - ` field separator.
    private static func isSegmentStart(_ name: String, _ index: String.Index) -> Bool {
        if index == name.startIndex { return true }
        guard let twoBack = name.index(index, offsetBy: -2, limitedBy: name.startIndex) else {
            return false
        }
        return name[twoBack..<index] == "- "
    }

    /// End of the name, the start of the next ` - ` field, a ` (distinguisher)` — the case Qt
    /// handled with a second `<title> (` glob — or a catalogue number.
    private static func isSegmentEnd(_ name: String, _ index: String.Index) -> Bool {
        if index == name.endIndex { return true }
        guard let twoOn = name.index(index, offsetBy: 2, limitedBy: name.endIndex) else {
            return false
        }
        let next = name[index..<twoOn]
        if next == " -" || next == " (" { return true }
        return startsNumericToken(name, at: index)
    }

    /// True when the title is followed by a space and then a purely numeric token, as in
    /// `Cielo 10093 rp` or `A mi no me interesa 10407-1 tt`.
    ///
    /// The point of the end constraint is that the title must not run into another *word*
    /// (`Nada` must not match `Nada mas`). A catalogue or track number is not part of a title,
    /// so it ends one just as a ` - ` separator does.
    private static func startsNumericToken(_ name: String, at index: String.Index) -> Bool {
        guard name[index] == " " else { return false }
        var cursor = name.index(after: index)
        var sawDigit = false

        while cursor < name.endIndex, name[cursor] != " " {
            let character = name[cursor]
            if character.isNumber {
                sawDigit = true
            } else if character != "-" && character != "." {
                return false
            }
            cursor = name.index(after: cursor)
        }
        return sawDigit
    }

    /// Smallest edit distance between `title` and any ` - ` separated segment of `fileName`,
    /// with a trailing parenthesised distinguisher stripped off each segment.
    static func bestSegmentDistance(in fileName: String, to title: String) -> Int? {
        var best: Int?
        for segment in segments(of: fileName) {
            let distance = Levenshtein.distance(Array(segment), Array(title))
            if best == nil || distance < best! { best = distance }
        }
        return best
    }

    /// Splits a normalised filename into the ` - ` separated fields the collection's naming
    /// scheme uses (`Orquesta - Vocalist - Title - Year`), plus the whole string.
    static func segments(of fileName: String) -> [String] {
        var result: [String] = [fileName]
        for piece in fileName.components(separatedBy: " - ") {
            let trimmed = stripTrailingParenthetical(piece).trimmingCharacters(in: .whitespaces)
            if !trimmed.isEmpty { result.append(trimmed) }
        }
        return result
    }

    private static func stripTrailingParenthetical(_ s: String) -> String {
        guard s.hasSuffix(")"), let open = s.lastIndex(of: "(") else { return s }
        return String(s[s.startIndex..<open])
    }
}

enum Levenshtein {
    /// Two-row Levenshtein distance. The Qt version in `mainwindow.cpp` swapped its rows inside
    /// the inner loop, which made it return something other than the edit distance; this one is
    /// the straightforward correct formulation.
    static func distance(_ a: [Character], _ b: [Character]) -> Int {
        if a.isEmpty { return b.count }
        if b.isEmpty { return a.count }

        var previous = Array(0...b.count)
        var current = [Int](repeating: 0, count: b.count + 1)

        for i in 0..<a.count {
            current[0] = i + 1
            for j in 0..<b.count {
                let substitution = previous[j] + (a[i] == b[j] ? 0 : 1)
                current[j + 1] = min(min(current[j] + 1, previous[j + 1] + 1), substitution)
            }
            swap(&previous, &current)
        }
        return previous[b.count]
    }
}
