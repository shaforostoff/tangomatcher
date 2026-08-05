import Foundation

/// Minimal ID3v2 reader/writer, scoped to what LyrMatcher needs: preserve every existing frame
/// byte-for-byte, replace the `USLT` (unsynchronised lyrics) frames, write the tag back.
///
/// Supported on read: v2.2 (frame IDs upgraded to their v2.3 equivalents), v2.3 and v2.4,
/// including whole-tag unsynchronisation and extended headers. On write the tag keeps the
/// version it had — v2.3 tags get UTF-16 lyrics (the only Unicode encoding v2.3 allows), v2.4
/// and freshly created tags get UTF-8, which is what the Qt/TagLib version wrote.
enum ID3v2Tagger {

    static let uslt = "USLT"
    private static let paddingSize = 1024

    struct Frame {
        var id: String
        /// v2.3/v2.4 frame flags, preserved verbatim for frames we do not touch.
        var flags: UInt16
        var payload: Data
    }

    struct Tag {
        /// 3 or 4. v2.2 input is upgraded to 3 on parse.
        var majorVersion: UInt8
        var frames: [Frame]
    }

    struct ParseResult {
        var tag: Tag?
        /// First byte of audio data — everything from here on is copied through untouched.
        var audioOffset: Int
    }

    // MARK: - Reading

    static func parse(_ data: Data) throws -> ParseResult {
        guard data.count >= 10, data.slice(0, 3) == Data("ID3".utf8) else {
            return ParseResult(tag: nil, audioOffset: 0)
        }

        guard let major = data.byte(at: 3), major >= 2, major <= 4 else {
            // An ID3v2 version we do not understand: refuse rather than corrupt the file.
            throw TagWriteError.unsupportedTagVersion("ID3v2.\(data.byte(at: 3) ?? 0)")
        }
        guard let flags = data.byte(at: 5), let size = data.synchsafeUInt32(at: 6) else {
            throw TagWriteError.malformed("ID3v2 header")
        }

        let hasFooter = (major == 4) && (flags & 0x10) != 0
        let audioOffset = 10 + Int(size) + (hasFooter ? 10 : 0)
        guard audioOffset <= data.count else { throw TagWriteError.malformed("ID3v2 tag size") }

        guard var body = data.slice(10, Int(size)) else {
            throw TagWriteError.malformed("ID3v2 body")
        }
        body = Data(body)  // rebase indices to 0

        // v2.2/v2.3 apply unsynchronisation to the whole tag; v2.4 does it per frame.
        if (flags & 0x80) != 0 && major < 4 {
            body = body.deunsynchronised()
        }

        var cursor = 0
        if (flags & 0x40) != 0 {
            cursor += extendedHeaderSize(body, majorVersion: major)
        }

        let frames = try parseFrames(body, from: cursor, majorVersion: major)
        return ParseResult(
            tag: Tag(majorVersion: major == 2 ? 3 : major, frames: frames),
            audioOffset: audioOffset
        )
    }

    private static func extendedHeaderSize(_ body: Data, majorVersion: UInt8) -> Int {
        if majorVersion == 4 {
            // v2.4: synchsafe size that includes the size field itself.
            return Int(body.synchsafeUInt32(at: 0) ?? 0)
        }
        // v2.3: plain size that excludes the four size bytes.
        return 4 + Int(body.beUInt32(at: 0) ?? 0)
    }

    private static func parseFrames(
        _ body: Data,
        from start: Int,
        majorVersion: UInt8
    ) throws -> [Frame] {
        var frames: [Frame] = []
        var cursor = start
        let headerSize = majorVersion == 2 ? 6 : 10

        while cursor + headerSize <= body.count {
            // A run of zero bytes is the padding that ends the frame area.
            if body.byte(at: cursor) == 0 { break }

            let rawID: String
            let size: Int
            let flags: UInt16

            if majorVersion == 2 {
                guard let id = body.slice(cursor, 3).map({ String(decoding: $0, as: UTF8.self) }),
                      let s = body.beUInt24(at: cursor + 3)
                else { break }
                rawID = id
                size = Int(s)
                flags = 0
            } else {
                guard let id = body.fourCC(at: cursor),
                      let f = body.beUInt16(at: cursor + 8)
                else { break }
                rawID = id
                flags = f
                if majorVersion == 4 {
                    // Some encoders write v2.4 tags with plain (non-synchsafe) frame sizes.
                    // Prefer the synchsafe reading, fall back when it does not fit.
                    let synchsafe = body.synchsafeUInt32(at: cursor + 4).map(Int.init)
                    let plain = body.beUInt32(at: cursor + 4).map(Int.init)
                    if let s = synchsafe, cursor + 10 + s <= body.count {
                        size = s
                    } else if let s = plain, cursor + 10 + s <= body.count {
                        size = s
                    } else {
                        break
                    }
                } else {
                    guard let s = body.beUInt32(at: cursor + 4) else { break }
                    size = Int(s)
                }
            }

            guard size >= 0, cursor + headerSize + size <= body.count else { break }
            guard let payload = body.slice(cursor + headerSize, size) else { break }

            if let id = normalisedFrameID(rawID, majorVersion: majorVersion) {
                frames.append(Frame(id: id, flags: flags, payload: Data(payload)))
            }
            cursor += headerSize + size
        }

        return frames
    }

    /// v2.2 used three-character frame IDs. Map the ones with a v2.3/v2.4 equivalent and drop
    /// the rest rather than emit an ID no player will understand.
    private static let v22ToV23: [String: String] = [
        "TT1": "TIT1", "TT2": "TIT2", "TT3": "TIT3", "TP1": "TPE1", "TP2": "TPE2",
        "TP3": "TPE3", "TP4": "TPE4", "TAL": "TALB", "TRK": "TRCK", "TYE": "TYER",
        "TCO": "TCON", "TCM": "TCOM", "TEN": "TENC", "TXT": "TEXT", "TBP": "TBPM",
        "TPA": "TPOS", "TLE": "TLEN", "TPB": "TPUB", "COM": "COMM", "ULT": "USLT",
        "TDA": "TDAT", "TIM": "TIME", "TSE": "TSSE", "TCP": "TCMP",
        // PIC is deliberately absent: its payload layout differs from APIC's, so upgrading it
        // would produce an unreadable picture frame.
    ]

    private static func normalisedFrameID(_ raw: String, majorVersion: UInt8) -> String? {
        guard majorVersion == 2 else {
            return raw.allSatisfy { $0.isUppercase || $0.isNumber } ? raw : nil
        }
        return v22ToV23[raw]
    }

    // MARK: - Writing

    /// Builds a `USLT` payload for one translation.
    ///
    /// Layout: text-encoding byte, three-byte language code, null-terminated content descriptor,
    /// then the lyrics themselves.
    static func usltPayload(
        text: String,
        description: String,
        language: String,
        majorVersion: UInt8
    ) -> Data {
        var payload = Data()

        var lang = Array(language.utf8.prefix(3))
        while lang.count < 3 { lang.append(UInt8(ascii: "u")) }

        if majorVersion >= 4 {
            payload.append(0x03)  // UTF-8
            payload.append(contentsOf: lang)
            payload.append(Data(description.utf8))
            payload.append(0x00)
            payload.append(Data(text.utf8))
        } else {
            payload.append(0x01)  // UTF-16 with BOM
            payload.append(contentsOf: lang)
            payload.append(UTF16Text.encode(description))
            payload.append(contentsOf: [0x00, 0x00])
            payload.append(UTF16Text.encode(text))
        }

        return payload
    }

    static func serialize(_ tag: Tag) throws -> Data {
        var frameData = Data()

        for frame in tag.frames {
            guard frame.id.utf8.count == 4 else {
                throw TagWriteError.malformed("frame id \(frame.id)")
            }
            guard frame.payload.count <= 0x0FFF_FFFF else {
                throw TagWriteError.malformed("frame \(frame.id) is too large")
            }
            frameData.append(Data(frame.id.utf8))
            frameData.append(
                tag.majorVersion >= 4
                    ? Data.synchsafe(UInt32(frame.payload.count))
                    : Data.be(UInt32(frame.payload.count))
            )
            frameData.append(Data.be(frame.flags))
            frameData.append(frame.payload)
        }

        let total = frameData.count + paddingSize
        guard total <= 0x0FFF_FFFF else { throw TagWriteError.malformed("ID3v2 tag is too large") }

        var out = Data("ID3".utf8)
        out.append(tag.majorVersion)
        out.append(0x00)                       // revision
        out.append(0x00)                       // flags: no unsynchronisation, no extended header
        out.append(Data.synchsafe(UInt32(total)))
        out.append(frameData)
        out.append(Data(repeating: 0, count: paddingSize))
        return out
    }

    // MARK: - Top level

    /// Rebuilds an ID3v2 tag with this document's lyrics in place of any it already had.
    ///
    /// Shared by the MP3 writer and the AIFF one, which keeps its tag inside an `ID3 ` chunk.
    ///
    /// - Parameter existing: the raw tag bytes, or `nil`/empty when the file carries none.
    /// - Returns: the new tag bytes, or `nil` when the file already has lyrics and `overwrite`
    ///   is off.
    static func tagWithLyrics(
        existing: Data?,
        translations: [Translation],
        overwrite: Bool
    ) throws -> Data? {
        var tag = try existing.flatMap { try parse($0).tag } ?? Tag(majorVersion: 4, frames: [])

        if tag.frames.contains(where: { $0.id == uslt }) {
            guard overwrite else { return nil }
            tag.frames.removeAll { $0.id == uslt }
        }

        for translation in translations {
            tag.frames.append(
                Frame(
                    id: uslt,
                    flags: 0,
                    payload: usltPayload(
                        text: LyricsPayload.id3Text(for: translation),
                        description: LyricsPayload.id3Description(for: translation),
                        language: translation.id3LanguageCode,
                        majorVersion: tag.majorVersion
                    )
                )
            )
        }

        return try serialize(tag)
    }

    static func write(
        translations: [Translation],
        to url: URL,
        overwrite: Bool
    ) throws -> WriteOutcome {
        let data = try Data(contentsOf: url, options: .mappedIfSafe)
        let parsed = try parse(data)

        guard let newTag = try tagWithLyrics(
            existing: parsed.tag == nil ? nil : data.slice(0, parsed.audioOffset),
            translations: translations,
            overwrite: overwrite
        ) else {
            return .skippedExisting
        }

        guard let audio = data.slice(parsed.audioOffset, data.count - parsed.audioOffset) else {
            throw TagWriteError.malformed("audio stream")
        }

        try AtomicFile.replace(at: url) { handle in
            try handle.write(contentsOf: newTag)
            try handle.write(contentsOf: audio)
        }
        return .written(translations.count)
    }
}
