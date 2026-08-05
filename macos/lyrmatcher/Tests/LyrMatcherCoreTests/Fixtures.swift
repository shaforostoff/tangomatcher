import Foundation
@testable import LyrMatcherCore

/// Hand-built minimal files for the tag writers. They contain no real audio — the taggers copy
/// the audio payload through untouched, so arbitrary bytes exercise the same code paths.
enum Fixtures {

    static func temporaryDirectory(_ label: String) -> URL {
        let url = URL(fileURLWithPath: NSTemporaryDirectory())
            .appendingPathComponent("lyrmatcher-\(label)-\(UUID().uuidString)")
        try? FileManager.default.createDirectory(at: url, withIntermediateDirectories: true)
        return url
    }

    static let audioBytes = Data((0..<2048).map { UInt8($0 % 251) })

    // MARK: MP3

    /// An ID3v2.3 tag carrying one TIT2 frame, followed by fake audio.
    static func mp3WithID3v23(title: String = "Poema") -> Data {
        var payload = Data([0x00])                     // ISO-8859-1
        payload.append(Data(title.utf8))

        var frames = Data(Data("TIT2".utf8))
        frames.append(Data.be(UInt32(payload.count)))
        frames.append(Data.be(UInt16(0)))
        frames.append(payload)

        let padding = 64
        var out = Data("ID3".utf8)
        out.append(contentsOf: [0x03, 0x00, 0x00])
        out.append(Data.synchsafe(UInt32(frames.count + padding)))
        out.append(frames)
        out.append(Data(repeating: 0, count: padding))
        out.append(audioBytes)
        return out
    }

    /// A bare MPEG stream with no tag at all.
    static func mp3WithoutTag() -> Data {
        var out = Data([0xFF, 0xFB, 0x90, 0x00])
        out.append(audioBytes)
        return out
    }

    // MARK: FLAC

    static func flac(existingLyrics: String? = nil) -> Data {
        var out = Data("fLaC".utf8)

        let streamInfo = Data(repeating: 0, count: 34)
        out.append(0x00)                                   // type 0, not last
        out.append(Data.be24(UInt32(streamInfo.count)))
        out.append(streamInfo)

        var comment = FLACTagger.VorbisComment(vendor: "reference libFLAC", fields: [
            .init(key: "TITLE", value: "Poema"),
            .init(key: "ARTIST", value: "Francisco Canaro"),
        ])
        if let existingLyrics {
            comment.fields.append(.init(key: "LYRICS", value: existingLyrics))
        }
        let commentData = FLACTagger.serializeVorbisComment(comment)

        out.append(0x80 | 0x04)                            // type 4, last block
        out.append(Data.be24(UInt32(commentData.count)))
        out.append(commentData)

        out.append(audioBytes)
        return out
    }

    // MARK: AIFF

    /// `FORM…AIFF` with a COMM chunk, optional `ID3 ` chunk, then SSND.
    ///
    /// - Parameter existingTag: raw ID3v2 bytes to plant in an `ID3 ` chunk ahead of the audio.
    static func aiff(existingTag: Data? = nil, oddSizedChunk: Bool = false) -> Data {
        func chunk(_ id: String, _ payload: Data) -> Data {
            var out = Data(id.utf8)
            out.append(Data.be(UInt32(payload.count)))
            out.append(payload)
            if payload.count % 2 == 1 { out.append(0x00) }   // pad to even
            return out
        }

        var body = Data("AIFF".utf8)
        body.append(chunk("COMM", Data(repeating: 0x11, count: 18)))
        if oddSizedChunk {
            body.append(chunk("NAME", Data("odd".utf8)))     // 3 bytes: forces a pad byte
        }
        if let existingTag { body.append(chunk("ID3 ", existingTag)) }
        body.append(chunk("SSND", audioBytes))

        var out = Data("FORM".utf8)
        out.append(Data.be(UInt32(body.count)))
        out.append(body)
        return out
    }

    /// Walks an AIFF and returns its chunks as (id, payload), so tests can assert on structure.
    static func aiffChunks(in data: Data) throws -> [(id: String, payload: Data)] {
        guard data.slice(0, 4) == Data("FORM".utf8) else {
            throw TagWriteError.malformed("not an AIFF")
        }
        var result: [(String, Data)] = []
        var cursor = 12
        while cursor + 8 <= data.count {
            guard let id = data.fourCC(at: cursor),
                  let size = data.beUInt32(at: cursor + 4),
                  let payload = data.slice(cursor + 8, Int(size))
            else { throw TagWriteError.malformed("AIFF chunk") }
            result.append((id, Data(payload)))
            cursor += 8 + Int(size) + (Int(size) % 2)
        }
        return result
    }

    // MARK: MP4

    static func atom(_ type: String, _ body: Data) -> Data {
        var out = Data.be(UInt32(body.count + 8))
        out.append(Data(type.utf8))
        out.append(body)
        return out
    }

    /// `ftyp` + `moov` (with a `stco` pointing into `mdat`) + `mdat`, in that order, so any change
    /// in `moov` size forces a chunk-offset fix-up.
    ///
    /// - Parameter existingLyrics: when non-nil, a `©lyr` atom is planted in `moov/udta/meta/ilst`.
    static func mp4(existingLyrics: String? = nil) -> (data: Data, chunkOffset: UInt32) {
        let ftyp = atom("ftyp", Data("M4A M4A mp42isom".utf8))

        func moov(chunkOffset: UInt32) -> Data {
            var stcoBody = Data([0, 0, 0, 0])              // version / flags
            stcoBody.append(Data.be(UInt32(1)))            // one entry
            stcoBody.append(Data.be(chunkOffset))

            let stbl = atom("stbl", atom("stco", stcoBody))
            let trak = atom("trak", atom("mdia", atom("minf", stbl)))
            let mvhd = atom("mvhd", Data(repeating: 0, count: 100))

            var body = mvhd
            body.append(trak)

            if let existingLyrics {
                var dataBody = Data([0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00])
                dataBody.append(Data(existingLyrics.utf8))

                var lyr = Data.be(UInt32(dataBody.count + 16))
                lyr.append(Data([0xA9, 0x6C, 0x79, 0x72]))
                lyr.append(Data.be(UInt32(dataBody.count + 8)))
                lyr.append(Data("data".utf8))
                lyr.append(dataBody)

                var metaBody = Data([0, 0, 0, 0])          // meta is a full atom
                metaBody.append(atom("hdlr", Data(repeating: 0, count: 25)))
                metaBody.append(atom("ilst", lyr))
                body.append(atom("udta", atom("meta", metaBody)))
            }

            return atom("moov", body)
        }

        // The chunk offset depends on moov's size, which is independent of the offset value
        // itself, so one sizing pass is enough.
        let sized = moov(chunkOffset: 0)
        let chunkOffset = UInt32(ftyp.count + sized.count + 8)

        var out = ftyp
        out.append(moov(chunkOffset: chunkOffset))
        out.append(atom("mdat", audioBytes))
        return (out, chunkOffset)
    }

    // MARK: Reading back

    /// Pulls the `©lyr` text out of an MP4, walking the atom tree the same way the writer does.
    static func mp4Lyrics(in data: Data) throws -> String? {
        func find(_ path: [String], in data: Data, from: Int, to: Int) throws -> MP4Tagger.RawAtom? {
            let atoms = try MP4Tagger.atoms(in: data, from: from, to: to)
            guard let head = path.first else { return nil }
            guard let match = atoms.first(where: { $0.typeString == head }) else { return nil }
            if path.count == 1 { return match }

            var start = match.payloadRange.lowerBound
            if head == "meta" {
                let payload = Data(data.slice(match.payloadRange.lowerBound, match.payloadRange.count) ?? Data())
                start += MP4Tagger.metaPrefixLength(payload)
            }
            return try find(Array(path.dropFirst()), in: data, from: start, to: match.payloadRange.upperBound)
        }

        guard let ilst = try find(["moov", "udta", "meta", "ilst"], in: data, from: 0, to: data.count)
        else { return nil }

        let entries = try MP4Tagger.atoms(
            in: data,
            from: ilst.payloadRange.lowerBound,
            to: ilst.payloadRange.upperBound
        )
        guard let lyr = entries.first(where: { $0.type == Data([0xA9, 0x6C, 0x79, 0x72]) }) else {
            return nil
        }

        let dataAtoms = try MP4Tagger.atoms(
            in: data,
            from: lyr.payloadRange.lowerBound,
            to: lyr.payloadRange.upperBound
        )
        guard let payload = dataAtoms.first(where: { $0.typeString == "data" }) else { return nil }
        // Skip the data atom's version/flags and locale words.
        guard let text = data.slice(payload.payloadRange.lowerBound + 8, payload.payloadRange.count - 8)
        else { return nil }
        return String(decoding: text, as: UTF8.self)
    }

    /// First chunk offset in the file's single `stco` table.
    static func mp4FirstChunkOffset(in data: Data) throws -> UInt32? {
        func search(_ from: Int, _ to: Int) throws -> UInt32? {
            for atom in try MP4Tagger.atoms(in: data, from: from, to: to) {
                if atom.typeString == "stco" {
                    return data.beUInt32(at: atom.payloadRange.lowerBound + 8)
                }
                if ["moov", "trak", "mdia", "minf", "stbl"].contains(atom.typeString) {
                    if let found = try search(atom.payloadRange.lowerBound, atom.payloadRange.upperBound) {
                        return found
                    }
                }
            }
            return nil
        }
        return try search(0, data.count)
    }

    /// Decodes every `USLT` frame of an ID3v2 tag into (language, description, text).
    static func id3Lyrics(in data: Data) throws -> [(language: String, description: String, text: String)] {
        let parsed = try ID3v2Tagger.parse(data)
        guard let tag = parsed.tag else { return [] }

        return tag.frames.filter { $0.id == ID3v2Tagger.uslt }.compactMap { frame in
            let payload = frame.payload
            guard let encoding = payload.byte(at: 0),
                  let languageData = payload.slice(1, 3)
            else { return nil }
            let language = String(decoding: languageData, as: UTF8.self)

            switch encoding {
            case 0x03:
                let rest = Data(payload.dropFirst(4))
                guard let terminator = rest.firstIndex(of: 0x00) else { return nil }
                let description = String(decoding: rest[rest.startIndex..<terminator], as: UTF8.self)
                let text = String(decoding: rest[rest.index(after: terminator)...], as: UTF8.self)
                return (language, description, text)

            case 0x01:
                var cursor = 4
                guard let description = readUTF16(payload, from: &cursor),
                      let text = readUTF16(payload, from: &cursor, toEnd: true)
                else { return nil }
                return (language, description, text)

            default:
                return nil
            }
        }
    }

    /// Reads one UTF-16 string with BOM, stopping at the 0x0000 terminator unless `toEnd`.
    private static func readUTF16(_ data: Data, from cursor: inout Int, toEnd: Bool = false) -> String? {
        guard let bom = data.slice(cursor, 2) else { return nil }
        let littleEndian = Array(bom) == [0xFF, 0xFE]
        cursor += 2

        var units: [UInt16] = []
        while cursor + 2 <= data.count {
            guard let lo = data.byte(at: cursor), let hi = data.byte(at: cursor + 1) else { break }
            let unit = littleEndian
                ? UInt16(lo) | (UInt16(hi) << 8)
                : (UInt16(lo) << 8) | UInt16(hi)
            cursor += 2
            if unit == 0 && !toEnd { return String(decoding: units, as: UTF16.self) }
            units.append(unit)
        }
        return toEnd ? String(decoding: units, as: UTF16.self) : nil
    }
}
