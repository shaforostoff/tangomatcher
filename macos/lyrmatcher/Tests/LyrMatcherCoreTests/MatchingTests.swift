import XCTest
@testable import LyrMatcherCore

final class NormalizationTests: XCTestCase {

    func testDropsAccents() {
        XCTAssertEqual(TextNormalization.simplify("Muñeca brava"), "muneca brava")
        XCTAssertEqual(TextNormalization.simplify("Adiós, Pampa mía"), "adios pampa mia")
        XCTAssertEqual(TextNormalization.simplify("Garçon"), "garcon")
    }

    func testUnderscoresBecomeSpaces() {
        XCTAssertEqual(TextNormalization.simplify("El_ultimo_cafe"), "el ultimo cafe")
        XCTAssertEqual(
            TextNormalization.simplify("Di_Sarli___Bahia_Blanca"),
            "di sarli bahia blanca"
        )
    }

    func testDropsPunctuationButKeepsHyphensAndBraces() {
        XCTAssertEqual(TextNormalization.simplify("¡Rie, payaso!"), "rie payaso")
        XCTAssertEqual(TextNormalization.simplify("¿Donde estas, corazon?"), "donde estas corazon")
        XCTAssertEqual(TextNormalization.simplify("Nena (a)"), "nena (a)")
        XCTAssertEqual(TextNormalization.simplify("- Volver -"), "- volver -")
    }

    func testFoldsTypographicDashesAndQuotes() {
        XCTAssertEqual(TextNormalization.simplify("Canaro \u{2013} Poema"), "canaro - poema")
        XCTAssertEqual(TextNormalization.simplify("D\u{2019}Arienzo"), "darienzo")
    }

    func testCollapsesAndTrimsWhitespace() {
        XCTAssertEqual(TextNormalization.simplify("  Tres   esquinas \n"), "tres esquinas")
    }
}

final class TitleMatcherTests: XCTestCase {

    private func matches(
        _ xmlBaseName: String,
        _ fileBaseName: String,
        addMinuses: Bool = false,
        fuzzy: Bool = false
    ) -> MatchTier? {
        TitleMatcher.tier(
            forNormalizedFileName: TextNormalization.simplify(fileBaseName),
            plan: TitleMatcher.plan(xmlBaseName: xmlBaseName, addMinuses: addMinuses),
            allowFuzzy: fuzzy
        )
    }

    func testMatchesAcrossAccentAndPunctuationDifferences() {
        XCTAssertEqual(
            matches("Que vas buscando muneca", "Di Sarli - Qué vas buscando muñeca - 1940"),
            .literal
        )
        XCTAssertEqual(
            matches("Adios, Pampa mia", "Canaro - Adios Pampa mia - Famá - 1945"),
            .literal
        )
        XCTAssertEqual(
            matches("Rie, Payaso", "Charlo_-_Rie_Payaso_-_1930"),
            .literal
        )
    }

    func testLeadingMarkerRequiresSegmentStart() {
        // "- Nada" must sit at the start of a " - " separated part.
        XCTAssertEqual(matches("- Nada", "Demare - Nada - Berón - 1944"), .literal)
        // "Enamorada" contains "nada" but not "- nada".
        XCTAssertNil(matches("- Nada", "Canaro - Enamorada - 1936"))
    }

    func testTrailingMarkerRequiresSegmentEnd() {
        XCTAssertEqual(matches("Intima -", "Di Sarli - Intima - 1938"), .literal)
        XCTAssertNil(matches("Intima -", "Di Sarli - Intimamente - 1938"))
    }

    func testTrailingMarkerAlsoAcceptsParentheticalDistinguisher() {
        // Qt built a second glob "<title> (" for exactly this case.
        XCTAssertEqual(
            matches("Poema -", "Canaro - Poema (instrumental) - 1935"),
            .literal
        )
    }

    func testAddMinusesTightensAnUnmarkedTitle() {
        // Unmarked, "Nada" also hits the longer title it is a prefix of.
        XCTAssertEqual(matches("Nada", "Canaro - Nada mas - 1936"), .literal)
        XCTAssertNil(matches("Nada", "Canaro - Nada mas - 1936", addMinuses: true))
        XCTAssertEqual(matches("Nada", "Demare - Nada - Berón - 1944", addMinuses: true), .literal)
    }

    func testAddMinusesDoesNotDoubleExistingMarkers() {
        let plan = TitleMatcher.plan(xmlBaseName: "- Volver -", addMinuses: true)
        XCTAssertEqual(plan.stems.first?.raw, "- Volver -")
    }

    func testBareTitleStripsAllDecorations() {
        XCTAssertEqual(TitleMatcher.bareTitle(xmlBaseName: "- Nada mas -"), "Nada mas")
        XCTAssertEqual(TitleMatcher.bareTitle(xmlBaseName: "Malena_"), "Malena")
        XCTAssertEqual(TitleMatcher.bareTitle(xmlBaseName: "Uno__ -"), "Uno")
    }

    func testFuzzyFallbackCatchesMisspellings() {
        // One transposed letter in a 12-character title: 2/12 < 0.2 threshold... barely not.
        XCTAssertNil(matches("Cambalache", "Discepolo - Cambalage - 1934", fuzzy: false))
        XCTAssertEqual(
            matches("Nostalgias", "Charlo - Nostalgia - 1936", fuzzy: true),
            .fuzzy(distance: 1)
        )
    }

    func testFuzzyRejectsUnrelatedTitles() {
        XCTAssertNil(matches("Nostalgias", "Canaro - Milonga sentimental - 1933", fuzzy: true))
    }

    func testFuzzyIsOnlyAFallback() {
        // A literal hit must never be downgraded to fuzzy.
        XCTAssertEqual(matches("Malena", "Troilo - Malena - Fiorentino", fuzzy: true), .literal)
    }

    func testLevenshtein() {
        XCTAssertEqual(Levenshtein.distance(Array("kitten"), Array("sitting")), 3)
        XCTAssertEqual(Levenshtein.distance(Array("malena"), Array("malena")), 0)
        XCTAssertEqual(Levenshtein.distance(Array(""), Array("uno")), 3)
    }
}

final class MusicIndexTests: XCTestCase {

    private func index(_ names: [String]) throws -> MusicIndex {
        let root = URL(fileURLWithPath: NSTemporaryDirectory())
            .appendingPathComponent("lyrmatcher-index-\(UUID().uuidString)")
        try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
        addTeardownBlock { try? FileManager.default.removeItem(at: root) }

        for name in names {
            let url = root.appendingPathComponent(name)
            try FileManager.default.createDirectory(
                at: url.deletingLastPathComponent(),
                withIntermediateDirectories: true
            )
            try Data("x".utf8).write(to: url)
        }
        return MusicIndex.scan(root: root)
    }

    func testScanFindsAudioRecursivelyAndIgnoresOtherFiles() throws {
        let scanned = try index([
            "Canaro/Canaro - Poema - 1935.mp3",
            "Di Sarli/Di Sarli - Bahia Blanca - 1957.flac",
            "Troilo/Troilo - Malena - Fiorentino.m4a",
            "Canaro/cover.jpg",
        ])

        XCTAssertEqual(scanned.files.count, 3)
        XCTAssertEqual(scanned.skippedCount, 1)
        XCTAssertEqual(
            Set(scanned.files.map(\.format)),
            Set([.mp3, .flac, .mp4])
        )
        XCTAssertTrue(scanned.files.contains { $0.relativePath == "Canaro/Canaro - Poema - 1935.mp3" })
    }

    func testMatchesRespectResultFilter() throws {
        let scanned = try index([
            "Canaro - Poema - 1935.mp3",
            "Di Sarli - Poema - 1940.mp3",
        ])
        let plan = TitleMatcher.plan(xmlBaseName: "Poema -", addMinuses: false)

        XCTAssertEqual(scanned.matches(for: plan, allowFuzzy: false).count, 2)
        XCTAssertEqual(
            scanned.matches(for: plan, allowFuzzy: false, resultFilter: "di sarli").count,
            1
        )
    }
}
