import Foundation

/// One `<translation>` element of a lyrics XML file.
public struct Translation: Hashable, Sendable {
    public var name: String
    public var language: String
    public var source: String
    public var translator: String
    public var year: String
    public var contents: String

    public init(
        name: String = "",
        language: String = "",
        source: String = "",
        translator: String = "",
        year: String = "",
        contents: String = ""
    ) {
        self.name = name
        self.language = language
        self.source = source
        self.translator = translator
        self.year = year
        self.contents = contents
    }

    /// The language to treat this entry as.
    ///
    /// A `<translation>` with no `lang` attribute (or an empty one) is the Spanish original —
    /// the lyrics collection only ever labels the translations away from it.
    public var effectiveLanguage: String {
        let trimmed = language.trimmingCharacters(in: .whitespacesAndNewlines)
        return trimmed.isEmpty ? LyricsDocument.spanishLanguageCode : trimmed
    }

    public var isSpanish: Bool {
        effectiveLanguage.caseInsensitiveCompare(LyricsDocument.spanishLanguageCode) == .orderedSame
    }

    /// Three-letter ISO-639-2 code for the ID3 `USLT` frame; `und` when the `lang` attribute is
    /// present but too short to be a usable code.
    public var id3LanguageCode: String {
        let ascii = effectiveLanguage.lowercased().unicodeScalars
            .filter { (0x61...0x7A).contains($0.value) }
        let code = String(String.UnicodeScalarView(ascii))
        guard code.count >= 3 else { return "und" }
        return String(code.prefix(3))
    }
}

/// The parsed contents of one `*.xml` lyrics file.
public struct LyricsDocument: Sendable {
    public var translations: [Translation]

    public init(translations: [Translation] = []) {
        self.translations = translations
    }

    public var isEmpty: Bool { translations.isEmpty }

    /// ISO-639-2 code for the Spanish original, as the collection writes it.
    public static let spanishLanguageCode = "spa"

    /// The title shown in the UI — the Spanish original when present, otherwise the first entry.
    public var primaryName: String {
        translations.first(where: \.isSpanish)?.name
            ?? translations.first?.name
            ?? ""
    }

    /// The translations to embed.
    ///
    /// - Parameter spanishOnly: keeps just the Spanish original, dropping the English and Russian
    ///   translations many of the XML files also carry. Entries with no `lang` attribute count as
    ///   Spanish — see `Translation.effectiveLanguage`.
    public func translations(spanishOnly: Bool) -> [Translation] {
        guard spanishOnly else { return translations }
        return translations.filter(\.isSpanish)
    }

    /// Human-readable rendering for the preview pane, matching `MainWindow::displayLyrics`.
    public func previewText(spanishOnly: Bool = false) -> String {
        translations(spanishOnly: spanishOnly)
            .map { "[\($0.effectiveLanguage)] \($0.name)\n\($0.contents)\n" }
            .joined(separator: "\n")
    }

    public static func load(contentsOf url: URL) throws -> LyricsDocument {
        let data = try Data(contentsOf: url)
        return try parse(data)
    }

    public static func parse(_ data: Data) throws -> LyricsDocument {
        let parser = XMLParser(data: data)
        let delegate = TranslationCollector()
        parser.delegate = delegate
        guard parser.parse() else {
            throw parser.parserError ?? LyricsError.malformedXML
        }
        return LyricsDocument(translations: delegate.translations)
    }
}

public enum LyricsError: LocalizedError {
    case malformedXML

    public var errorDescription: String? {
        switch self {
        case .malformedXML: return "The lyrics XML file could not be parsed."
        }
    }
}

/// Collects every `<translation>` element regardless of nesting, the way
/// `QDomDocument::elementsByTagName("translation")` did.
private final class TranslationCollector: NSObject, XMLParserDelegate {
    private(set) var translations: [Translation] = []
    private var current: Translation?
    private var buffer = ""

    func parser(
        _ parser: XMLParser,
        didStartElement elementName: String,
        namespaceURI: String?,
        qualifiedName: String?,
        attributes: [String: String]
    ) {
        guard elementName == "translation" else { return }
        current = Translation(
            name: attributes["name"] ?? "",
            language: attributes["lang"] ?? "",
            source: attributes["source"] ?? "",
            // The collection writes `translator`; the Qt code read `author`. Accept both.
            translator: attributes["translator"] ?? attributes["author"] ?? "",
            year: attributes["year"] ?? ""
        )
        buffer = ""
    }

    func parser(_ parser: XMLParser, foundCharacters string: String) {
        guard current != nil else { return }
        buffer += string
    }

    func parser(_ parser: XMLParser, foundCDATA CDATABlock: Data) {
        guard current != nil, let text = String(data: CDATABlock, encoding: .utf8) else { return }
        buffer += text
    }

    func parser(
        _ parser: XMLParser,
        didEndElement elementName: String,
        namespaceURI: String?,
        qualifiedName: String?
    ) {
        guard elementName == "translation", var translation = current else { return }
        translation.contents = buffer
        translations.append(translation)
        current = nil
        buffer = ""
    }
}
