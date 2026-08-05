import Foundation

/// MP4/M4A atom surgery for the iTunes lyrics atom `©lyr` (`moov/udta/meta/ilst/©lyr`).
///
/// Growing `moov` shifts every byte after it, so when `moov` sits ahead of the media data the
/// sample tables (`stco` / `co64`) have to be re-based by the same delta — otherwise the file
/// still parses but plays silence. That fix-up is the bulk of this file.
enum MP4Tagger {

    private static let lyrType = Data([0xA9, 0x6C, 0x79, 0x72])  // '©lyr'

    // MARK: - Raw atom walking

    struct RawAtom {
        let type: Data
        let headerSize: Int
        /// Whole atom, header included.
        let range: Range<Int>

        var payloadRange: Range<Int> { (range.lowerBound + headerSize)..<range.upperBound }
        var typeString: String { String(decoding: type, as: UTF8.self) }
    }

    static func atoms(in data: Data, from: Int, to: Int) throws -> [RawAtom] {
        var result: [RawAtom] = []
        var cursor = from

        while cursor + 8 <= to {
            guard let size32 = data.beUInt32(at: cursor), let type = data.slice(cursor + 4, 4) else {
                throw TagWriteError.malformed("MP4 atom header")
            }

            let size: Int
            let headerSize: Int
            switch size32 {
            case 1:
                guard let big = data.beUInt64(at: cursor + 8), big <= UInt64(Int.max) else {
                    throw TagWriteError.malformed("MP4 64-bit atom size")
                }
                size = Int(big)
                headerSize = 16
            case 0:
                size = to - cursor      // extends to the end of its parent
                headerSize = 8
            default:
                size = Int(size32)
                headerSize = 8
            }

            guard size >= headerSize, cursor + size <= to else {
                throw TagWriteError.malformed("MP4 atom size at offset \(cursor)")
            }
            result.append(RawAtom(type: Data(type), headerSize: headerSize, range: cursor..<(cursor + size)))
            cursor += size
        }

        return result
    }

    // MARK: - Editable node tree

    indirect enum Node {
        /// Bytes after the 8-byte header, kept verbatim.
        case leaf(type: Data, payload: Data)
        /// `prefix` carries a full atom's version/flags word when the container has one.
        case container(type: Data, prefix: Data, children: [Node])

        var type: Data {
            switch self {
            case let .leaf(type, _): return type
            case let .container(type, _, _): return type
            }
        }

        var typeString: String { String(decoding: type, as: UTF8.self) }

        var serialized: Data {
            var body = Data()
            switch self {
            case let .leaf(_, payload):
                body = payload
            case let .container(_, prefix, children):
                body = prefix
                for child in children { body.append(child.serialized) }
            }
            var out = Data.be(UInt32(body.count + 8))
            out.append(type)
            out.append(body)
            return out
        }
    }

    /// Containers we descend into when editing. Everything else stays an opaque leaf.
    private static let editableContainers: Set<String> = ["moov", "udta", "ilst"]

    private static func node(from data: Data, atom: RawAtom) throws -> Node {
        let typeString = atom.typeString

        if typeString == "meta" {
            let payload = Data(data.slice(atom.payloadRange.lowerBound, atom.payloadRange.count) ?? Data())
            let prefixLength = metaPrefixLength(payload)
            let children = try atoms(
                in: data,
                from: atom.payloadRange.lowerBound + prefixLength,
                to: atom.payloadRange.upperBound
            ).map { try node(from: data, atom: $0) }
            return .container(
                type: atom.type,
                prefix: Data(payload.prefix(prefixLength)),
                children: children
            )
        }

        if editableContainers.contains(typeString) {
            let children = try atoms(
                in: data,
                from: atom.payloadRange.lowerBound,
                to: atom.payloadRange.upperBound
            ).map { try node(from: data, atom: $0) }
            return .container(type: atom.type, prefix: Data(), children: children)
        }

        return .leaf(
            type: atom.type,
            payload: Data(data.slice(atom.payloadRange.lowerBound, atom.payloadRange.count) ?? Data())
        )
    }

    /// `meta` is a full atom (4-byte version/flags before its children) in every conforming file,
    /// but some encoders emit it as a plain container. Sniff which one this is.
    static func metaPrefixLength(_ payload: Data) -> Int {
        if let size = payload.beUInt32(at: 4), size >= 8, Int(size) + 4 <= payload.count,
           let type = payload.fourCC(at: 8), isPlausibleAtomType(type) {
            return 4
        }
        if let size = payload.beUInt32(at: 0), size >= 8, Int(size) <= payload.count,
           let type = payload.fourCC(at: 4), isPlausibleAtomType(type) {
            return 0
        }
        return 4
    }

    private static func isPlausibleAtomType(_ type: String) -> Bool {
        let scalars = type.unicodeScalars
        return scalars.count == 4 && scalars.allSatisfy { $0.value >= 0x20 && $0.value < 0x7F }
    }

    // MARK: - ilst editing

    /// An iTunes metadata value: a `data` atom holding version/flags, a locale word, and the text.
    private static func textDataAtom(_ text: String) -> Data {
        var body = Data([0x00, 0x00, 0x00, 0x01])  // version 0, flags 1 = UTF-8 text
        body.append(contentsOf: [0x00, 0x00, 0x00, 0x00])  // locale
        body.append(Data(text.utf8))

        var out = Data.be(UInt32(body.count + 8))
        out.append(Data("data".utf8))
        out.append(body)
        return out
    }

    private static func hdlrAtom() -> Data {
        var body = Data([0x00, 0x00, 0x00, 0x00])          // version / flags
        body.append(contentsOf: [0x00, 0x00, 0x00, 0x00])  // predefined
        body.append(Data("mdir".utf8))                     // handler type
        body.append(Data("appl".utf8))                     // reserved, as iTunes writes it
        body.append(Data(repeating: 0, count: 9))          // reserved + empty name

        var out = Data.be(UInt32(body.count + 8))
        out.append(Data("hdlr".utf8))
        out.append(body)
        return out
    }

    /// Inserts or replaces `©lyr` under `moov/udta/meta/ilst`, creating the containers as needed.
    /// Returns `nil` when the atom is already present and `overwrite` is off.
    private static func applyLyrics(to moov: Node, text: String, overwrite: Bool) -> Node? {
        func editIlst(_ node: Node) -> Node? {
            guard case let .container(type, prefix, children) = node else { return nil }
            if children.contains(where: { $0.type == lyrType }) && !overwrite { return nil }
            var updated = children.filter { $0.type != lyrType }
            updated.append(.leaf(type: lyrType, payload: textDataAtom(text)))
            return .container(type: type, prefix: prefix, children: updated)
        }

        /// Walks down `path`, creating missing containers, then applies `editIlst` at the leaf.
        func descend(_ node: Node, path: [String]) -> Node? {
            guard case let .container(type, prefix, children) = node else { return nil }

            guard let next = path.first else { return editIlst(node) }

            if let index = children.firstIndex(where: { $0.typeString == next }) {
                guard let child = descend(children[index], path: Array(path.dropFirst())) else {
                    return nil
                }
                var updated = children
                updated[index] = child
                return .container(type: type, prefix: prefix, children: updated)
            }

            // Missing link in the chain — create it, then keep descending into the new node.
            let empty: Node
            if next == "meta" {
                // A newly made `meta` needs the `mdir`/`appl` handler or players ignore `ilst`.
                empty = .container(
                    type: Data("meta".utf8),
                    prefix: Data([0, 0, 0, 0]),
                    children: [.leaf(type: Data("hdlr".utf8), payload: Data(hdlrAtom().dropFirst(8)))]
                )
            } else {
                empty = .container(type: Data(next.utf8), prefix: Data(), children: [])
            }
            guard let created = descend(empty, path: Array(path.dropFirst())) else { return nil }

            var updated = children
            // `udta` conventionally goes last inside `moov`; anything else can follow suit.
            updated.append(created)
            return .container(type: type, prefix: prefix, children: updated)
        }

        return descend(moov, path: ["udta", "meta", "ilst"])
    }

    // MARK: - Chunk offset fix-up

    private static let sampleTablePath: Set<String> = ["moov", "trak", "mdia", "minf", "stbl"]

    /// Adds `delta` to every chunk offset at or past `threshold`.
    static func adjustChunkOffsets(in moov: Data, delta: Int, threshold: UInt64) throws -> Data {
        guard delta != 0 else { return moov }
        var bytes = [UInt8](moov)

        func be32(_ at: Int) -> UInt32 {
            (UInt32(bytes[at]) << 24) | (UInt32(bytes[at + 1]) << 16)
                | (UInt32(bytes[at + 2]) << 8) | UInt32(bytes[at + 3])
        }
        func write32(_ at: Int, _ value: UInt32) {
            bytes[at] = UInt8(truncatingIfNeeded: value >> 24)
            bytes[at + 1] = UInt8(truncatingIfNeeded: value >> 16)
            bytes[at + 2] = UInt8(truncatingIfNeeded: value >> 8)
            bytes[at + 3] = UInt8(truncatingIfNeeded: value)
        }
        func be64(_ at: Int) -> UInt64 {
            (0..<8).reduce(UInt64(0)) { ($0 << 8) | UInt64(bytes[at + $1]) }
        }
        func write64(_ at: Int, _ value: UInt64) {
            for i in 0..<8 { bytes[at + i] = UInt8(truncatingIfNeeded: value >> ((7 - i) * 8)) }
        }

        func shifted(_ offset: UInt64) -> UInt64 {
            offset >= threshold ? UInt64(Int64(offset) + Int64(delta)) : offset
        }

        func walk(_ from: Int, _ to: Int) throws {
            var cursor = from
            while cursor + 8 <= to {
                let size32 = be32(cursor)
                let type = String(decoding: bytes[(cursor + 4)..<(cursor + 8)], as: UTF8.self)
                let size: Int
                let headerSize: Int
                switch size32 {
                case 1:
                    size = Int(be64(cursor + 8)); headerSize = 16
                case 0:
                    size = to - cursor; headerSize = 8
                default:
                    size = Int(size32); headerSize = 8
                }
                guard size >= headerSize, cursor + size <= to else { return }

                let bodyStart = cursor + headerSize
                switch type {
                case "stco":
                    let count = Int(be32(bodyStart + 4))
                    for i in 0..<count {
                        let at = bodyStart + 8 + i * 4
                        guard at + 4 <= cursor + size else { break }
                        let new = shifted(UInt64(be32(at)))
                        guard new <= UInt64(UInt32.max) else {
                            throw TagWriteError.chunkOffsetOverflow
                        }
                        write32(at, UInt32(new))
                    }
                case "co64":
                    let count = Int(be32(bodyStart + 4))
                    for i in 0..<count {
                        let at = bodyStart + 8 + i * 8
                        guard at + 8 <= cursor + size else { break }
                        write64(at, shifted(be64(at)))
                    }
                default:
                    if sampleTablePath.contains(type) {
                        try walk(bodyStart, cursor + size)
                    }
                }
                cursor += size
            }
        }

        try walk(0, bytes.count)
        return Data(bytes)
    }

    // MARK: - Top level

    static func write(
        translations: [Translation],
        to url: URL,
        overwrite: Bool
    ) throws -> WriteOutcome {
        let data = try Data(contentsOf: url, options: .mappedIfSafe)
        let top = try atoms(in: data, from: 0, to: data.count)

        guard let moovAtom = top.first(where: { $0.typeString == "moov" }) else {
            throw TagWriteError.malformed("no moov atom")
        }

        let moovNode = try node(from: data, atom: moovAtom)
        let text = LyricsPayload.combined(translations)
        guard let updated = applyLyrics(to: moovNode, text: text, overwrite: overwrite) else {
            return .skippedExisting
        }

        var newMoov = updated.serialized
        let delta = newMoov.count - moovAtom.range.count

        // Media offsets are absolute file positions. Only the ones that live past `moov` move.
        newMoov = try adjustChunkOffsets(
            in: newMoov,
            delta: delta,
            threshold: UInt64(moovAtom.range.lowerBound)
        )

        let head = data.slice(0, moovAtom.range.lowerBound) ?? Data()
        let tail = data.slice(moovAtom.range.upperBound, data.count - moovAtom.range.upperBound) ?? Data()

        try AtomicFile.replace(at: url) { handle in
            try handle.write(contentsOf: head)
            try handle.write(contentsOf: newMoov)
            try handle.write(contentsOf: tail)
        }
        return .written(translations.count)
    }
}
