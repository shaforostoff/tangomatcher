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

/// A literal search string derived from the XML filename, plus its normalised form.
public struct SearchStem: Hashable, Sendable {
    public let raw: String
    public let normalized: String

    public init(raw: String) {
        self.raw = raw
        self.normalized = TextNormalization.simplify(raw)
    }
}

/// Everything needed to test music filenames against one lyrics XML file.
public struct MatchPlan: Sendable {
    /// Literal stems, in the order the Qt app built them.
    public let stems: [SearchStem]
    /// The title with the `- ` / ` -` boundary markers and `_` variant suffix removed, used as
    /// the reference string for fuzzy scoring.
    public let bareTitle: String
    /// Normalised `bareTitle`.
    public let normalizedBareTitle: String

    public var isUsable: Bool { stems.contains { !$0.normalized.isEmpty } }
}

public enum TitleMatcher {

    /// Ratio of edit distance to title length below which a fuzzy hit is accepted. Same value
    /// the discography matcher in `src/mainwindow.cpp` used.
    public static let fuzzyThreshold = 0.2

    /// Builds the search stems for an XML file, mirroring `MainWindow::displayLyrics()`.
    ///
    /// - Parameters:
    ///   - xmlBaseName: XML filename with the `.xml` extension already removed.
    ///   - addMinuses: the Qt "Add minuses" toggle — forces the `- `/` -` boundary markers on
    ///     so a short title only matches when it occupies a whole ` - ` separated segment.
    public static func plan(xmlBaseName: String, addMinuses: Bool) -> MatchPlan {
        var stem = xmlBaseName
        if addMinuses && !stem.hasPrefix("- ") { stem = "- " + stem }
        if addMinuses && !stem.hasSuffix(" -") { stem = stem + " -" }

        var stems = [SearchStem(raw: stem)]

        // Qt also globbed `*<stem sans trailing '-'>(*` so that `Title -` still finds
        // `Title (instrumental)`. Qt gated this on the first glob having hit; we always add it,
        // since it can only widen the result set and the gate looked accidental.
        if stem.hasSuffix(" -") {
            stems.append(SearchStem(raw: String(stem.dropLast()) + "("))
        }

        let bare = bareTitle(xmlBaseName: xmlBaseName)
        return MatchPlan(
            stems: stems,
            bareTitle: bare,
            normalizedBareTitle: TextNormalization.simplify(bare)
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
        for stem in plan.stems where !stem.normalized.isEmpty {
            if normalized.contains(stem.normalized) { return .literal }
        }

        guard allowFuzzy, !plan.normalizedBareTitle.isEmpty else { return nil }
        guard let distance = bestSegmentDistance(
            in: normalized,
            to: plan.normalizedBareTitle
        ) else { return nil }

        let ratio = Double(distance) / Double(plan.normalizedBareTitle.count)
        return ratio < fuzzyThreshold ? .fuzzy(distance: distance) : nil
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
