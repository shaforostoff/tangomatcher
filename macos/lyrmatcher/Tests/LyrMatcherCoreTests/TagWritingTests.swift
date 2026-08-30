import AVFoundation
import XCTest
@testable import LyrMatcherCore

private let spanish = Translation(
    name: "Poema",
    language: "spa",
    source: "todotango",
    contents: "\nFue un ensueño de dulce amor,\nhoras de dicha y de querer.\n"
)

private let english = Translation(
    name: "Poem",
    language: "eng",
    source: "tango.info",
    translator: "Anon",
    contents: "It was a daydream of sweet love,\nhours of joy and of longing.\n"
)

class TaggerTestCase: XCTestCase {
    var directory: URL!

    override func setUpWithError() throws {
        directory = Fixtures.temporaryDirectory(name)
            .appendingPathComponent("t", isDirectory: true)
        try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
    }

    override func tearDownWithError() throws {
        try? FileManager.default.removeItem(at: directory.deletingLastPathComponent())
    }

    func makeFile(_ name: String, _ data: Data) throws -> MusicFile {
        let url = directory.appendingPathComponent(name)
        try data.write(to: url)
        return MusicFile(
            url: url,
            relativePath: name,
            baseName: url.deletingPathExtension().lastPathComponent,
            normalizedBaseName: TextNormalization.simplify(url.deletingPathExtension().lastPathComponent),
            format: AudioFormat.from(pathExtension: url.pathExtension)!
        )
    }
}

// MARK: - MP3

final class ID3v2TaggerTests: TaggerTestCase {

    func testWritesOneFrameForEachTranslationAndKeepsExistingFrames() throws {
        let file = try makeFile("song.mp3", Fixtures.mp3WithID3v23())

        let outcome = try TagWriter.write(
            translations: [spanish, english],
            to: file,
            overwrite: false
        )
        XCTAssertEqual(outcome, .written(2))

        let written = try Data(contentsOf: file.url)
        let frames = try Fixtures.id3Lyrics(in: written)
        XCTAssertEqual(frames.count, 2)
        XCTAssertEqual(frames.map(\.language), ["spa", "eng"])
        XCTAssertEqual(frames.map(\.description), ["Poema", "Poem"])
        XCTAssertTrue(frames[0].text.contains("Fue un ensueño de dulce amor,"))
        XCTAssertTrue(frames[1].text.contains("hours of joy and of longing."))

        // The pre-existing title frame must survive.
        let tag = try XCTUnwrap(try ID3v2Tagger.parse(written).tag)
        XCTAssertTrue(tag.frames.contains { $0.id == "TIT2" })
        // An ID3v2.3 tag stays v2.3 so the UTF-16 encoding we chose remains legal.
        XCTAssertEqual(tag.majorVersion, 3)
    }

    func testAudioStreamIsPreservedByteForByte() throws {
        let file = try makeFile("song.mp3", Fixtures.mp3WithID3v23())
        _ = try TagWriter.write(translations: [spanish], to: file, overwrite: false)

        let written = try Data(contentsOf: file.url)
        let parsed = try ID3v2Tagger.parse(written)
        let audio = written.suffix(from: written.index(written.startIndex, offsetBy: parsed.audioOffset))
        XCTAssertEqual(Data(audio), Fixtures.audioBytes)
    }

    func testCreatesAV24TagWhenTheFileHasNone() throws {
        let file = try makeFile("song.mp3", Fixtures.mp3WithoutTag())
        XCTAssertEqual(try TagWriter.write(translations: [spanish], to: file, overwrite: false), .written(1))

        let written = try Data(contentsOf: file.url)
        let tag = try XCTUnwrap(try ID3v2Tagger.parse(written).tag)
        XCTAssertEqual(tag.majorVersion, 4)
        XCTAssertEqual(try Fixtures.id3Lyrics(in: written).count, 1)
        // Untagged audio is preserved, tag simply prepended.
        XCTAssertTrue(written.suffix(Fixtures.audioBytes.count) == Fixtures.audioBytes)
    }

    func testSkipsFileThatAlreadyHasLyricsUnlessOverwriteIsOn() throws {
        let file = try makeFile("song.mp3", Fixtures.mp3WithID3v23())
        _ = try TagWriter.write(translations: [spanish], to: file, overwrite: false)

        XCTAssertEqual(
            try TagWriter.write(translations: [english], to: file, overwrite: false),
            .skippedExisting
        )
        XCTAssertEqual(try Fixtures.id3Lyrics(in: Data(contentsOf: file.url)).map(\.description), ["Poema"])

        XCTAssertEqual(
            try TagWriter.write(translations: [english], to: file, overwrite: true),
            .written(1)
        )
        XCTAssertEqual(try Fixtures.id3Lyrics(in: Data(contentsOf: file.url)).map(\.description), ["Poem"])
    }

    func testSpanishOnlyDocumentWritesASingleFrame() throws {
        let document = LyricsDocument(translations: [spanish, english])
        let file = try makeFile("song.mp3", Fixtures.mp3WithID3v23())

        XCTAssertEqual(
            try TagWriter.write(
                translations: document.translations(spanishOnly: true),
                to: file,
                overwrite: true
            ),
            .written(1)
        )

        let frames = try Fixtures.id3Lyrics(in: Data(contentsOf: file.url))
        XCTAssertEqual(frames.map(\.language), ["spa"])
        XCTAssertEqual(frames.map(\.description), ["Poema"])
    }

    func testRewritingIsIdempotent() throws {
        let file = try makeFile("song.mp3", Fixtures.mp3WithID3v23())
        _ = try TagWriter.write(translations: [spanish, english], to: file, overwrite: true)
        let first = try Data(contentsOf: file.url)
        _ = try TagWriter.write(translations: [spanish, english], to: file, overwrite: true)
        XCTAssertEqual(try Data(contentsOf: file.url), first)
    }

    func testLyricsUseCRLFLineEndings() throws {
        let file = try makeFile("song.mp3", Fixtures.mp3WithoutTag())
        _ = try TagWriter.write(translations: [spanish], to: file, overwrite: false)

        let text = try XCTUnwrap(try Fixtures.id3Lyrics(in: Data(contentsOf: file.url)).first?.text)
        XCTAssertTrue(text.hasPrefix("Poema\r\n\r\n"))

        // Every LF must be preceded by a CR, and no CR may stand alone.
        let scalars = Array(text.unicodeScalars)
        XCTAssertGreaterThan(scalars.filter { $0 == "\n" }.count, 0)
        for (index, scalar) in scalars.enumerated() {
            if scalar == "\n" { XCTAssertEqual(index > 0 ? scalars[index - 1] : " ", "\r") }
            if scalar == "\r" { XCTAssertEqual(index + 1 < scalars.count ? scalars[index + 1] : " ", "\n") }
        }
    }

    func testUnsynchronisedTagIsDecodedBeforeRewriting() throws {
        // A v2.3 tag carrying the unsync flag. Frame sizes describe the decoded payload
        // (00 FF 41), while the bytes on disk carry the inserted 00 after the FF.
        let decodedSize = 3

        var frames = Data("TIT2".utf8)
        frames.append(Data.be(UInt32(decodedSize)))
        frames.append(Data.be(UInt16(0)))
        frames.append(Data([0x00, 0xFF, 0x00, 0x41]))

        var data = Data("ID3".utf8)
        data.append(contentsOf: [0x03, 0x00, 0x80])          // unsynchronisation flag
        data.append(Data.synchsafe(UInt32(frames.count)))
        data.append(frames)
        data.append(Fixtures.audioBytes)

        let file = try makeFile("unsync.mp3", data)
        _ = try TagWriter.write(translations: [spanish], to: file, overwrite: false)

        let tag = try XCTUnwrap(try ID3v2Tagger.parse(Data(contentsOf: file.url)).tag)
        let title = try XCTUnwrap(tag.frames.first { $0.id == "TIT2" })
        XCTAssertEqual(Array(title.payload), [0x00, 0xFF, 0x41])
    }
}

// MARK: - FLAC

final class FLACTaggerTests: TaggerTestCase {

    func testWritesCombinedLyricsAndKeepsOtherFields() throws {
        let file = try makeFile("song.flac", Fixtures.flac())
        XCTAssertEqual(
            try TagWriter.write(translations: [spanish, english], to: file, overwrite: false),
            .written(2)
        )

        let comment = try readComment(file.url)
        XCTAssertEqual(comment.value(for: "TITLE"), "Poema")
        XCTAssertEqual(comment.value(for: "ARTIST"), "Francisco Canaro")

        let lyrics = try XCTUnwrap(comment.value(for: "LYRICS"))
        XCTAssertTrue(lyrics.hasPrefix("Poema\r\n\r\n"))
        XCTAssertTrue(lyrics.contains("Poem\r\n\r\n"))
        XCTAssertTrue(lyrics.contains("hours of joy and of longing."))
    }

    func testAudioIsPreserved() throws {
        let file = try makeFile("song.flac", Fixtures.flac())
        _ = try TagWriter.write(translations: [spanish], to: file, overwrite: false)
        XCTAssertEqual(Data(try Data(contentsOf: file.url).suffix(Fixtures.audioBytes.count)), Fixtures.audioBytes)
    }

    func testSkipsExistingLyricsUnlessOverwrite() throws {
        let file = try makeFile("song.flac", Fixtures.flac(existingLyrics: "old lyrics"))
        XCTAssertEqual(
            try TagWriter.write(translations: [spanish], to: file, overwrite: false),
            .skippedExisting
        )
        XCTAssertEqual(try readComment(file.url).value(for: "LYRICS"), "old lyrics")

        XCTAssertEqual(
            try TagWriter.write(translations: [spanish], to: file, overwrite: true),
            .written(1)
        )
        XCTAssertNotEqual(try readComment(file.url).value(for: "LYRICS"), "old lyrics")
    }

    func testIdenticalLyricsReportUnchanged() throws {
        let file = try makeFile("song.flac", Fixtures.flac())
        _ = try TagWriter.write(translations: [spanish], to: file, overwrite: true)
        XCTAssertEqual(
            try TagWriter.write(translations: [spanish], to: file, overwrite: true),
            .unchanged
        )
    }

    func testCreatesCommentBlockWhenAbsent() throws {
        // STREAMINFO only, marked as the last block.
        var data = Data("fLaC".utf8)
        data.append(0x80)
        data.append(Data.be24(34))
        data.append(Data(repeating: 0, count: 34))
        data.append(Fixtures.audioBytes)

        let file = try makeFile("bare.flac", data)
        XCTAssertEqual(try TagWriter.write(translations: [spanish], to: file, overwrite: false), .written(1))
        XCTAssertNotNil(try readComment(file.url).value(for: "LYRICS"))
    }

    private func readComment(_ url: URL) throws -> FLACTagger.VorbisComment {
        let data = try Data(contentsOf: url)
        var cursor = 4
        while true {
            let header = try XCTUnwrap(data.byte(at: cursor))
            let length = Int(try XCTUnwrap(data.beUInt24(at: cursor + 1)))
            if header & 0x7F == 4 {
                return try FLACTagger.parseVorbisComment(Data(try XCTUnwrap(data.slice(cursor + 4, length))))
            }
            if header & 0x80 != 0 { break }
            cursor += 4 + length
        }
        throw TagWriteError.malformed("no Vorbis comment")
    }
}

// MARK: - AIFF

final class AIFFTaggerTests: TaggerTestCase {

    func testWritesAnID3ChunkWhenTheFileHasNone() throws {
        let file = try makeFile("song.aif", Fixtures.aiff())
        XCTAssertEqual(
            try TagWriter.write(translations: [spanish, english], to: file, overwrite: false),
            .written(2)
        )

        let written = try Data(contentsOf: file.url)
        let chunks = try Fixtures.aiffChunks(in: written)
        XCTAssertEqual(chunks.map(\.id), ["COMM", "SSND", "ID3 "])

        let frames = try Fixtures.id3Lyrics(in: try XCTUnwrap(chunks.last?.payload))
        XCTAssertEqual(frames.map(\.language), ["spa", "eng"])
        XCTAssertEqual(frames.map(\.description), ["Poema", "Poem"])
    }

    func testReplacesAnExistingID3ChunkAndKeepsItsOtherFrames() throws {
        // Seed the chunk with a tag that already carries a title frame.
        let seed = try XCTUnwrap(
            try ID3v2Tagger.parse(Fixtures.mp3WithID3v23(title: "Poema")).tag
        )
        let file = try makeFile("song.aif", Fixtures.aiff(existingTag: try ID3v2Tagger.serialize(seed)))

        XCTAssertEqual(
            try TagWriter.write(translations: [spanish], to: file, overwrite: false),
            .written(1)
        )

        let chunks = try Fixtures.aiffChunks(in: try Data(contentsOf: file.url))
        XCTAssertEqual(chunks.map(\.id), ["COMM", "ID3 ", "SSND"], "chunk order must be preserved")

        let tagData = try XCTUnwrap(chunks.first { $0.id == "ID3 " }?.payload)
        let tag = try XCTUnwrap(try ID3v2Tagger.parse(tagData).tag)
        XCTAssertTrue(tag.frames.contains { $0.id == "TIT2" })
        XCTAssertEqual(try Fixtures.id3Lyrics(in: tagData).count, 1)
    }

    func testAudioAndFormSizeStayCorrect() throws {
        let file = try makeFile("song.aif", Fixtures.aiff())
        _ = try TagWriter.write(translations: [spanish], to: file, overwrite: false)

        let written = try Data(contentsOf: file.url)
        XCTAssertEqual(
            Int(try XCTUnwrap(written.beUInt32(at: 4))),
            written.count - 8,
            "FORM size must cover everything after the size field"
        )

        let sound = try XCTUnwrap(try Fixtures.aiffChunks(in: written).first { $0.id == "SSND" })
        XCTAssertEqual(sound.payload, Fixtures.audioBytes)
    }

    func testOddSizedChunksKeepTheirPadding() throws {
        let file = try makeFile("song.aif", Fixtures.aiff(oddSizedChunk: true))
        _ = try TagWriter.write(translations: [spanish], to: file, overwrite: false)

        let written = try Data(contentsOf: file.url)
        let chunks = try Fixtures.aiffChunks(in: written)
        // Parsing at all proves the pad byte was re-emitted; SSND after it must be intact.
        XCTAssertEqual(chunks.map(\.id), ["COMM", "NAME", "SSND", "ID3 "])
        XCTAssertEqual(chunks.first { $0.id == "NAME" }?.payload, Data("odd".utf8))
        XCTAssertEqual(chunks.first { $0.id == "SSND" }?.payload, Fixtures.audioBytes)
    }

    func testSkipsExistingLyricsUnlessOverwrite() throws {
        let file = try makeFile("song.aif", Fixtures.aiff())
        _ = try TagWriter.write(translations: [spanish], to: file, overwrite: false)

        XCTAssertEqual(
            try TagWriter.write(translations: [english], to: file, overwrite: false),
            .skippedExisting
        )
        XCTAssertEqual(
            try TagWriter.write(translations: [english], to: file, overwrite: true),
            .written(1)
        )

        let tagData = try XCTUnwrap(
            try Fixtures.aiffChunks(in: try Data(contentsOf: file.url)).first { $0.id == "ID3 " }?.payload
        )
        XCTAssertEqual(try Fixtures.id3Lyrics(in: tagData).map(\.description), ["Poem"])
    }

    func testRewritingIsIdempotent() throws {
        let file = try makeFile("song.aif", Fixtures.aiff())
        _ = try TagWriter.write(translations: [spanish, english], to: file, overwrite: true)
        let first = try Data(contentsOf: file.url)
        _ = try TagWriter.write(translations: [spanish, english], to: file, overwrite: true)
        XCTAssertEqual(try Data(contentsOf: file.url), first)
    }

    func testRejectsSomethingThatIsNotAnAIFF() throws {
        let file = try makeFile("bogus.aif", Data(repeating: 0x42, count: 512))
        XCTAssertThrowsError(try TagWriter.write(translations: [spanish], to: file, overwrite: true))
    }
}

// MARK: - MP4

final class MP4TaggerTests: TaggerTestCase {

    func testWritesLyricsAndRebasesChunkOffsets() throws {
        let fixture = Fixtures.mp4()
        let file = try makeFile("song.m4a", fixture.data)
        let originalSize = fixture.data.count

        XCTAssertEqual(
            try TagWriter.write(translations: [spanish, english], to: file, overwrite: false),
            .written(2)
        )

        let written = try Data(contentsOf: file.url)
        let lyrics = try XCTUnwrap(try Fixtures.mp4Lyrics(in: written))
        XCTAssertTrue(lyrics.hasPrefix("Poema\r\n\r\n"))
        XCTAssertTrue(lyrics.contains("Poem\r\n\r\n"))

        // moov grew, so the chunk offset must have moved by exactly the same amount.
        let delta = written.count - originalSize
        XCTAssertGreaterThan(delta, 0)
        XCTAssertEqual(
            try Fixtures.mp4FirstChunkOffset(in: written),
            fixture.chunkOffset + UInt32(delta)
        )

        // And the offset must still point at the first byte of mdat's payload.
        let mdat = try XCTUnwrap(
            try MP4Tagger.atoms(in: written, from: 0, to: written.count)
                .first { $0.typeString == "mdat" }
        )
        XCTAssertEqual(
            Int(try XCTUnwrap(try Fixtures.mp4FirstChunkOffset(in: written))),
            mdat.payloadRange.lowerBound
        )
        XCTAssertEqual(
            Data(try XCTUnwrap(written.slice(mdat.payloadRange.lowerBound, mdat.payloadRange.count))),
            Fixtures.audioBytes
        )
    }

    func testSkipsExistingLyricsUnlessOverwrite() throws {
        let file = try makeFile("song.m4a", Fixtures.mp4(existingLyrics: "old lyrics").data)

        XCTAssertEqual(
            try TagWriter.write(translations: [spanish], to: file, overwrite: false),
            .skippedExisting
        )
        XCTAssertEqual(try Fixtures.mp4Lyrics(in: Data(contentsOf: file.url)), "old lyrics")

        XCTAssertEqual(
            try TagWriter.write(translations: [spanish], to: file, overwrite: true),
            .written(1)
        )
        XCTAssertTrue(try XCTUnwrap(try Fixtures.mp4Lyrics(in: Data(contentsOf: file.url))).hasPrefix("Poema"))
    }

    func testShrinkingMoovAlsoRebasesOffsets() throws {
        let long = String(repeating: "x", count: 4096)
        let fixture = Fixtures.mp4(existingLyrics: long)
        let file = try makeFile("song.m4a", fixture.data)

        _ = try TagWriter.write(translations: [spanish], to: file, overwrite: true)

        let written = try Data(contentsOf: file.url)
        let mdat = try XCTUnwrap(
            try MP4Tagger.atoms(in: written, from: 0, to: written.count).first { $0.typeString == "mdat" }
        )
        XCTAssertLessThan(written.count, fixture.data.count)
        XCTAssertEqual(
            Int(try XCTUnwrap(try Fixtures.mp4FirstChunkOffset(in: written))),
            mdat.payloadRange.lowerBound
        )
    }

    func testCreatesTheWholeUdtaChainWhenMissing() throws {
        let fixture = Fixtures.mp4()
        XCTAssertNil(try Fixtures.mp4Lyrics(in: fixture.data))

        let file = try makeFile("song.m4a", fixture.data)
        _ = try TagWriter.write(translations: [spanish], to: file, overwrite: false)

        let written = try Data(contentsOf: file.url)
        XCTAssertNotNil(try Fixtures.mp4Lyrics(in: written))
        // The created meta atom must carry an mdir handler.
        XCTAssertTrue(written.range(of: Data("mdir".utf8)) != nil)
    }
}

// MARK: - End to end against a real AAC file

/// Encodes a real M4A with `afconvert`, tags it, then reads it back with AVFoundation. This is
/// the check that the atom surgery produces a file the system frameworks still accept.
final class RealAudioFileTests: TaggerTestCase {

    func testTaggedM4AStillDecodesAndCarriesLyrics() throws {
        guard FileManager.default.isExecutableFile(atPath: "/usr/bin/afconvert") else {
            throw XCTSkip("afconvert is unavailable")
        }

        let wav = directory.appendingPathComponent("tone.wav")
        try makeSineWAV().write(to: wav)

        let m4a = directory.appendingPathComponent("tone.m4a")
        let convert = Process()
        convert.executableURL = URL(fileURLWithPath: "/usr/bin/afconvert")
        convert.arguments = ["-f", "m4af", "-d", "aac", wav.path, m4a.path]
        convert.standardOutput = FileHandle.nullDevice
        convert.standardError = FileHandle.nullDevice
        try convert.run()
        convert.waitUntilExit()
        guard convert.terminationStatus == 0, FileManager.default.fileExists(atPath: m4a.path) else {
            throw XCTSkip("afconvert could not produce an M4A on this machine")
        }

        let framesBefore = try AVAudioFile(forReading: m4a).length
        XCTAssertGreaterThan(framesBefore, 0)

        let file = MusicFile(
            url: m4a,
            relativePath: "tone.m4a",
            baseName: "tone",
            normalizedBaseName: "tone",
            format: .mp4
        )
        XCTAssertEqual(
            try TagWriter.write(translations: [spanish, english], to: file, overwrite: false),
            .written(2)
        )

        // Still decodable, same number of frames — proves the chunk offsets survived.
        let reopened = try AVAudioFile(forReading: m4a)
        XCTAssertEqual(reopened.length, framesBefore)

        let buffer = try XCTUnwrap(
            AVAudioPCMBuffer(pcmFormat: reopened.processingFormat, frameCapacity: 4096)
        )
        try reopened.read(into: buffer)
        XCTAssertGreaterThan(buffer.frameLength, 0)

        // And AVFoundation resolves the atom as the iTunes lyrics item.
        let items = AVMetadataItem.metadataItems(
            from: AVURLAsset(url: m4a).metadata,
            filteredByIdentifier: .iTunesMetadataLyrics
        )
        let lyrics = items.first?.stringValue
        XCTAssertNotNil(lyrics, "©lyr not visible to AVFoundation")
        XCTAssertTrue(try XCTUnwrap(lyrics).hasPrefix("Poema"))
        XCTAssertTrue(try XCTUnwrap(lyrics).contains("Poem\r\n\r\n"))

        // The pre-existing iTunes gapless-playback tag must survive the rewrite.
        XCTAssertTrue(
            AVURLAsset(url: m4a).metadata.contains { $0.key as? String == "com.apple.iTunes.iTunSMPB" }
        )
    }

    /// 0.5 s of 440 Hz, 16-bit mono at 44.1 kHz, as a canonical RIFF/WAVE file.
    private func makeSineWAV() -> Data {
        let sampleRate = 44_100
        let frames = sampleRate / 2
        var samples = Data()
        for i in 0..<frames {
            let value = Int16(16_000 * sin(2 * .pi * 440 * Double(i) / Double(sampleRate)))
            samples.append(UInt8(truncatingIfNeeded: value))
            samples.append(UInt8(truncatingIfNeeded: value >> 8))
        }

        var out = Data("RIFF".utf8)
        out.append(Data.le(UInt32(36 + samples.count)))
        out.append(Data("WAVEfmt ".utf8))
        out.append(Data.le(16))                      // PCM chunk size
        out.append(Data([0x01, 0x00, 0x01, 0x00]))   // PCM, mono
        out.append(Data.le(UInt32(sampleRate)))
        out.append(Data.le(UInt32(sampleRate * 2)))  // byte rate
        out.append(Data([0x02, 0x00, 0x10, 0x00]))   // block align, bits per sample
        out.append(Data("data".utf8))
        out.append(Data.le(UInt32(samples.count)))
        out.append(samples)
        return out
    }
}
