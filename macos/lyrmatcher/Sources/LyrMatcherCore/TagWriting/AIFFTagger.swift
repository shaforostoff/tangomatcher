import Foundation

/// AIFF tagging, via the `ID3 ` chunk that every tagger in this space uses.
///
/// AIFF is an IFF container: `FORM<size>AIFF` followed by chunks of `<id><size><data>`, each
/// padded to an even length. Lyrics live in an ID3v2 tag inside a chunk named `ID3 `, so the
/// frame handling is exactly the MP3 one — see `ID3v2Tagger.tagWithLyrics`.
///
/// Unlike MP4, nothing in an AIFF refers to another chunk by absolute offset, so resizing the
/// tag chunk needs no fix-ups beyond the `FORM` size.
enum AIFFTagger {

    /// Canonical spelling. Some taggers write `id3 `; both are recognised on read.
    private static let tagChunkID = "ID3 "

    private struct Chunk {
        var id: String
        var data: Data
    }

    private static func isTagChunk(_ id: String) -> Bool {
        id.caseInsensitiveCompare(tagChunkID) == .orderedSame
    }

    // MARK: - Reading

    private static func parse(_ data: Data) throws -> (formType: String, chunks: [Chunk]) {
        guard data.count >= 12,
              data.slice(0, 4) == Data("FORM".utf8),
              let formType = data.fourCC(at: 8),
              formType == "AIFF" || formType == "AIFC"
        else {
            throw TagWriteError.malformed("AIFF FORM header")
        }

        var chunks: [Chunk] = []
        var cursor = 12

        while cursor + 8 <= data.count {
            guard let id = data.fourCC(at: cursor), let size = data.beUInt32(at: cursor + 4) else {
                throw TagWriteError.malformed("AIFF chunk header")
            }
            // A truncated final chunk is common in the wild; take what is there rather than fail.
            let available = min(Int(size), data.count - (cursor + 8))
            guard let payload = data.slice(cursor + 8, available) else {
                throw TagWriteError.malformed("AIFF chunk body")
            }
            chunks.append(Chunk(id: id, data: Data(payload)))

            // Chunk data is padded to an even length; the pad byte is not counted in the size.
            cursor += 8 + available + (available % 2)
        }

        return (formType, chunks)
    }

    // MARK: - Writing

    static func write(
        translations: [Translation],
        to url: URL,
        overwrite: Bool
    ) throws -> WriteOutcome {
        let data = try Data(contentsOf: url, options: .mappedIfSafe)
        let (formType, chunks) = try parse(data)

        let existingIndex = chunks.firstIndex { isTagChunk($0.id) }
        guard let newTag = try ID3v2Tagger.tagWithLyrics(
            existing: existingIndex.map { chunks[$0].data },
            translations: translations,
            overwrite: overwrite
        ) else {
            return .skippedExisting
        }

        var updated = chunks
        if let existingIndex {
            updated[existingIndex] = Chunk(id: chunks[existingIndex].id, data: newTag)
        } else {
            // Appending keeps COMM first and leaves the sound data where it is.
            updated.append(Chunk(id: tagChunkID, data: newTag))
        }

        var body = Data(formType.utf8)
        for chunk in updated {
            guard chunk.data.count <= UInt32.max else {
                throw TagWriteError.malformed("AIFF chunk is too large")
            }
            body.append(Data.fourCC(chunk.id))
            body.append(Data.be(UInt32(chunk.data.count)))
            body.append(chunk.data)
            if chunk.data.count % 2 == 1 { body.append(0x00) }
        }

        var out = Data("FORM".utf8)
        out.append(Data.be(UInt32(body.count)))
        out.append(body)

        try AtomicFile.replace(at: url) { handle in
            try handle.write(contentsOf: out)
        }
        return .written(translations.count)
    }
}
