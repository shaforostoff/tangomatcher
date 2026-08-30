import XCTest
@testable import LyrMatcherCore

final class LyricsDocumentTests: XCTestCase {

    private let sample = """
        <?xml version="1.0" encoding="UTF-8"?>
        <lyrics>
        <translation lang="spa" name="Malena" source="tango-del-dia.livejournal.com">
        Malena canta el tango como ninguna
        y en cada verso pone su corazón.
        </translation>
        <translation lang="rus" name="Малена" source="example.org" translator="Marianna">
        Поёт Малена танго неповторимо,
        </translation>
        </lyrics>
        """

    func testParsesEveryTranslation() throws {
        let document = try LyricsDocument.parse(Data(sample.utf8))
        XCTAssertEqual(document.translations.count, 2)

        XCTAssertEqual(document.translations[0].language, "spa")
        XCTAssertEqual(document.translations[0].name, "Malena")
        XCTAssertEqual(document.translations[0].source, "tango-del-dia.livejournal.com")
        XCTAssertTrue(document.translations[0].contents.contains("Malena canta el tango como ninguna"))

        XCTAssertEqual(document.translations[1].language, "rus")
        XCTAssertEqual(document.translations[1].translator, "Marianna")
    }

    func testAcceptsAuthorAsAnAliasForTranslator() throws {
        let xml = #"<lyrics><translation lang="eng" name="X" author="Someone">text</translation></lyrics>"#
        let document = try LyricsDocument.parse(Data(xml.utf8))
        XCTAssertEqual(document.translations.first?.translator, "Someone")
    }

    func testPrimaryNamePrefersSpanish() throws {
        let document = try LyricsDocument.parse(Data(sample.utf8))
        XCTAssertEqual(document.primaryName, "Malena")
    }

    func testSpanishOnlyDropsTranslations() throws {
        let document = try LyricsDocument.parse(Data(sample.utf8))

        XCTAssertEqual(document.translations(spanishOnly: false).map(\.language), ["spa", "rus"])
        XCTAssertEqual(document.translations(spanishOnly: true).map(\.language), ["spa"])
        XCTAssertEqual(document.translations(spanishOnly: true).first?.name, "Malena")
    }

    func testSpanishOnlyYieldsNothingWhenThereIsNoSpanishVersion() throws {
        let xml = #"<lyrics><translation lang="eng" name="X">text</translation></lyrics>"#
        let document = try LyricsDocument.parse(Data(xml.utf8))

        XCTAssertFalse(document.isEmpty)
        XCTAssertTrue(document.translations(spanishOnly: true).isEmpty)
    }

    func testPreviewTextHonoursTheSpanishOnlyFilter() throws {
        let document = try LyricsDocument.parse(Data(sample.utf8))

        XCTAssertTrue(document.previewText().contains("[rus]"))
        XCTAssertFalse(document.previewText(spanishOnly: true).contains("[rus]"))
        XCTAssertTrue(document.previewText(spanishOnly: true).contains("[spa] Malena"))
    }

    func testLanguageCodeFallsBackToUnd() {
        XCTAssertEqual(Translation(language: "spa").id3LanguageCode, "spa")
        XCTAssertEqual(Translation(language: "en").id3LanguageCode, "und")
        XCTAssertEqual(Translation(language: "pt-BR").id3LanguageCode, "ptb")
    }

    func testUnspecifiedLanguageMeansSpanish() {
        for raw in ["", "  ", "\n"] {
            let translation = Translation(language: raw)
            XCTAssertEqual(translation.effectiveLanguage, "spa", "raw: \(raw.debugDescription)")
            XCTAssertEqual(translation.id3LanguageCode, "spa", "raw: \(raw.debugDescription)")
            XCTAssertTrue(translation.isSpanish, "raw: \(raw.debugDescription)")
        }

        XCTAssertTrue(Translation(language: "SPA").isSpanish)
        XCTAssertFalse(Translation(language: "eng").isSpanish)
    }

    func testTranslationWithoutLangAttributeIsKeptBySpanishOnly() throws {
        let xml = """
            <lyrics>
            <translation name="Malena" source="s">original</translation>
            <translation lang="eng" name="Malena" source="s">translated</translation>
            </lyrics>
            """
        let document = try LyricsDocument.parse(Data(xml.utf8))

        XCTAssertEqual(document.translations.count, 2)
        XCTAssertEqual(
            document.translations(spanishOnly: true).map(\.contents),
            ["original"]
        )
        XCTAssertEqual(document.primaryName, "Malena")
        XCTAssertTrue(document.previewText(spanishOnly: true).hasPrefix("[spa] Malena"))
    }

    func testUnlabelledTranslationIsTaggedAsSpanish() throws {
        let xml = #"<lyrics><translation name="Malena">original</translation></lyrics>"#
        let document = try LyricsDocument.parse(Data(xml.utf8))
        XCTAssertEqual(document.translations.first?.id3LanguageCode, "spa")
    }

    func testMalformedXMLThrows() {
        XCTAssertThrowsError(try LyricsDocument.parse(Data("<lyrics><translation>".utf8)))
    }

    func testPayloadShapesMatchTheQtOutput() {
        let translation = Translation(name: "Poema", language: "spa", contents: "\n  line one\nline two  \n")

        XCTAssertEqual(
            LyricsPayload.id3Text(for: translation),
            "Poema\r\n\r\nline one\r\nline two\r\n\r\n"
        )
        XCTAssertEqual(
            LyricsPayload.combined([translation]),
            "Poema\r\n\r\nline one\r\nline two\r\n\r\n\r\n\r\n"
        )
    }

    /// Guards against re-introducing CR duplication if a source file already uses CRLF.
    func testCRLFInputIsNotDoubled() {
        let translation = Translation(name: "Poema", contents: "line one\r\nline two")
        XCTAssertFalse(LyricsPayload.id3Text(for: translation).contains("\r\r"))
    }
}

/// Parses a real file from the repository's `lyrics-xmldata` folder when it is reachable, so the
/// parser is exercised against the actual data shape rather than only a hand-written sample.
final class RealLyricsDataTests: XCTestCase {

    private var dataDirectory: URL? {
        // Tests run from .build/<config>; walk up to the repository root.
        var url = URL(fileURLWithPath: #filePath)
        for _ in 0..<8 {
            url = url.deletingLastPathComponent()
            let candidate = url.appendingPathComponent("lyrics-xmldata")
            if FileManager.default.fileExists(atPath: candidate.path) { return candidate }
        }
        return nil
    }

    func testParsesASampleOfTheRealCollection() throws {
        guard let dataDirectory else { throw XCTSkip("lyrics-xmldata is not present") }

        let files = try FileManager.default.contentsOfDirectory(
            at: dataDirectory,
            includingPropertiesForKeys: nil
        ).filter { $0.pathExtension == "xml" }
        XCTAssertGreaterThan(files.count, 100)

        var parsed = 0
        for url in files {
            let document = try LyricsDocument.load(contentsOf: url)
            XCTAssertFalse(
                document.translations.isEmpty,
                "\(url.lastPathComponent) yielded no translations"
            )
            parsed += 1
        }
        XCTAssertEqual(parsed, files.count)
    }

    func testEveryFileNameProducesAUsableSearchPlan() throws {
        guard let dataDirectory else { throw XCTSkip("lyrics-xmldata is not present") }

        let names = try FileManager.default.contentsOfDirectory(
            at: dataDirectory,
            includingPropertiesForKeys: nil
        ).filter { $0.pathExtension == "xml" }.map { $0.deletingPathExtension().lastPathComponent }

        for name in names {
            let plan = TitleMatcher.plan(xmlBaseName: name, addMinuses: false)
            XCTAssertTrue(plan.isUsable, "\(name) produced an empty search stem")
            XCTAssertFalse(plan.normalizedBareTitle.isEmpty, "\(name) produced an empty bare title")

            // A title must always match a filename built from itself.
            let synthetic = "Canaro - \(TitleMatcher.bareTitle(xmlBaseName: name)) - 1935"
            XCTAssertNotNil(
                TitleMatcher.tier(
                    forNormalizedFileName: TextNormalization.simplify(synthetic),
                    plan: TitleMatcher.plan(xmlBaseName: name, addMinuses: true),
                    allowFuzzy: false
                ),
                "\(name) did not match its own canonical filename"
            )
        }
    }
}
