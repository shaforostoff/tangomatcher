import Foundation

/// FLAC metadata-block reader/writer, scoped to the `LYRICS` Vorbis comment field.
///
/// The Qt app only ever reached the Xiph comment for FLAC (`flacf->ID3v2Tag()` without the
/// create flag returns null on a file that has no ID3 tag, which is the normal case), so this
/// writes the Vorbis comment and leaves every other block untouched.
enum FLACTagger {

    private static let lyricsKey = "LYRICS"
    private static let paddingSize = 1024

    private enum BlockType: UInt8 {
        case streamInfo = 0
        case padding = 1
        case vorbisComment = 4
    }

    private struct Block {
        var type: UInt8
        var data: Data
    }

    private struct Layout {
        /// Anything before the `fLaC` magic — an ID3v2 tag some taggers prepend. Copied through.
        var preamble: Data
        var blocks: [Block]
        var audioOffset: Int
    }

    // MARK: - Reading

    private static func parse(_ data: Data) throws -> Layout {
        let magic = Data("fLaC".utf8)
        var start = 0

        if data.slice(0, 3) == Data("ID3".utf8) {
            guard let size = data.synchsafeUInt32(at: 6) else {
                throw TagWriteError.malformed("ID3v2 header ahead of FLAC stream")
            }
            start = 10 + Int(size)
        }
        guard data.slice(start, 4) == magic else {
            throw TagWriteError.malformed("missing fLaC signature")
        }

        var blocks: [Block] = []
        var cursor = start + 4

        while true {
            guard let header = data.byte(at: cursor), let length = data.beUInt24(at: cursor + 1) else {
                throw TagWriteError.malformed("FLAC metadata block header")
            }
            let isLast = (header & 0x80) != 0
            let type = header & 0x7F
            guard let payload = data.slice(cursor + 4, Int(length)) else {
                throw TagWriteError.malformed("FLAC metadata block body")
            }
            blocks.append(Block(type: type, data: Data(payload)))
            cursor += 4 + Int(length)
            if isLast { break }
        }

        return Layout(
            preamble: Data(data.slice(0, start) ?? Data()),
            blocks: blocks,
            audioOffset: cursor
        )
    }

    // MARK: - Vorbis comment

    struct VorbisComment {
        struct Field {
            var key: String
            var value: String
        }

        var vendor: String
        /// Preserved in file order; keys are compared case-insensitively per the spec.
        var fields: [Field]

        func value(for key: String) -> String? {
            fields.first { $0.key.caseInsensitiveCompare(key) == .orderedSame }?.value
        }

        mutating func removeAll(_ key: String) {
            fields.removeAll { $0.key.caseInsensitiveCompare(key) == .orderedSame }
        }
    }

    static func parseVorbisComment(_ data: Data) throws -> VorbisComment {
        guard let vendorLength = data.leUInt32(at: 0),
              let vendorData = data.slice(4, Int(vendorLength))
        else { throw TagWriteError.malformed("Vorbis comment vendor string") }

        var cursor = 4 + Int(vendorLength)
        guard let count = data.leUInt32(at: cursor) else {
            throw TagWriteError.malformed("Vorbis comment count")
        }
        cursor += 4

        var fields: [VorbisComment.Field] = []
        for _ in 0..<count {
            guard let length = data.leUInt32(at: cursor),
                  let entry = data.slice(cursor + 4, Int(length))
            else { throw TagWriteError.malformed("Vorbis comment entry") }
            cursor += 4 + Int(length)

            let text = String(decoding: entry, as: UTF8.self)
            if let separator = text.firstIndex(of: "=") {
                fields.append(
                    VorbisComment.Field(
                        key: String(text[text.startIndex..<separator]),
                        value: String(text[text.index(after: separator)...])
                    )
                )
            }
        }

        return VorbisComment(vendor: String(decoding: vendorData, as: UTF8.self), fields: fields)
    }

    static func serializeVorbisComment(_ comment: VorbisComment) -> Data {
        var out = Data()
        let vendor = Data(comment.vendor.utf8)
        out.append(Data.le(UInt32(vendor.count)))
        out.append(vendor)
        out.append(Data.le(UInt32(comment.fields.count)))
        for field in comment.fields {
            let entry = Data("\(field.key)=\(field.value)".utf8)
            out.append(Data.le(UInt32(entry.count)))
            out.append(entry)
        }
        return out
    }

    // MARK: - Top level

    static func write(
        translations: [Translation],
        to url: URL,
        overwrite: Bool
    ) throws -> WriteOutcome {
        let data = try Data(contentsOf: url, options: .mappedIfSafe)
        let layout = try parse(data)

        let commentIndex = layout.blocks.firstIndex { $0.type == BlockType.vorbisComment.rawValue }
        var comment = try commentIndex.map { try parseVorbisComment(layout.blocks[$0].data) }
            ?? VorbisComment(vendor: "LyrMatcher", fields: [])

        let newValue = LyricsPayload.combined(translations)
        if let existing = comment.value(for: lyricsKey) {
            if existing == newValue { return .unchanged }
            guard overwrite else { return .skippedExisting }
        }

        comment.removeAll(lyricsKey)
        comment.fields.append(VorbisComment.Field(key: lyricsKey, value: newValue))

        var blocks = layout.blocks.filter { $0.type != BlockType.padding.rawValue }
        let newBlock = Block(
            type: BlockType.vorbisComment.rawValue,
            data: serializeVorbisComment(comment)
        )
        if let index = blocks.firstIndex(where: { $0.type == BlockType.vorbisComment.rawValue }) {
            blocks[index] = newBlock
        } else {
            // STREAMINFO must stay first; the comment can go straight after it.
            blocks.insert(newBlock, at: min(1, blocks.count))
        }
        blocks.append(Block(type: BlockType.padding.rawValue, data: Data(repeating: 0, count: paddingSize)))

        var head = layout.preamble
        head.append(Data("fLaC".utf8))
        for (index, block) in blocks.enumerated() {
            guard block.data.count <= 0xFF_FFFF else {
                throw TagWriteError.malformed("FLAC metadata block is too large")
            }
            let isLast = index == blocks.count - 1
            head.append(block.type | (isLast ? 0x80 : 0x00))
            head.append(Data.be24(UInt32(block.data.count)))
            head.append(block.data)
        }

        guard let audio = data.slice(layout.audioOffset, data.count - layout.audioOffset) else {
            throw TagWriteError.malformed("FLAC audio stream")
        }

        try AtomicFile.replace(at: url) { handle in
            try handle.write(contentsOf: head)
            try handle.write(contentsOf: audio)
        }
        return .written(translations.count)
    }
}
