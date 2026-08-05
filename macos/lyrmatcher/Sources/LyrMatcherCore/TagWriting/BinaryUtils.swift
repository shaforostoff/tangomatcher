import Foundation

/// Big-endian reads/writes and the ID3 "synchsafe" integer encoding, used by all three taggers.
extension Data {

    func byte(at offset: Int) -> UInt8? {
        guard offset >= 0, offset < count else { return nil }
        return self[index(startIndex, offsetBy: offset)]
    }

    func slice(_ offset: Int, _ length: Int) -> Data? {
        guard offset >= 0, length >= 0, offset + length <= count else { return nil }
        let start = index(startIndex, offsetBy: offset)
        let end = index(start, offsetBy: length)
        return self[start..<end]
    }

    func beUInt16(at offset: Int) -> UInt16? {
        guard let d = slice(offset, 2) else { return nil }
        return d.reduce(UInt16(0)) { ($0 << 8) | UInt16($1) }
    }

    func beUInt24(at offset: Int) -> UInt32? {
        guard let d = slice(offset, 3) else { return nil }
        return d.reduce(UInt32(0)) { ($0 << 8) | UInt32($1) }
    }

    func beUInt32(at offset: Int) -> UInt32? {
        guard let d = slice(offset, 4) else { return nil }
        return d.reduce(UInt32(0)) { ($0 << 8) | UInt32($1) }
    }

    func beUInt64(at offset: Int) -> UInt64? {
        guard let d = slice(offset, 8) else { return nil }
        return d.reduce(UInt64(0)) { ($0 << 8) | UInt64($1) }
    }

    func leUInt32(at offset: Int) -> UInt32? {
        guard let d = slice(offset, 4) else { return nil }
        return d.reversed().reduce(UInt32(0)) { ($0 << 8) | UInt32($1) }
    }

    /// Four ASCII characters starting at `offset` — atom types and frame IDs.
    func fourCC(at offset: Int) -> String? {
        guard let d = slice(offset, 4) else { return nil }
        return String(decoding: d, as: UTF8.self)
    }

    /// ID3 synchsafe integer: 4 bytes, 7 significant bits each.
    func synchsafeUInt32(at offset: Int) -> UInt32? {
        guard let d = slice(offset, 4) else { return nil }
        var value: UInt32 = 0
        for byte in d {
            guard byte & 0x80 == 0 else { return nil }
            value = (value << 7) | UInt32(byte & 0x7F)
        }
        return value
    }

    static func be(_ value: UInt16) -> Data {
        Data([UInt8(truncatingIfNeeded: value >> 8), UInt8(truncatingIfNeeded: value)])
    }

    static func be(_ value: UInt32) -> Data {
        Data([
            UInt8(truncatingIfNeeded: value >> 24),
            UInt8(truncatingIfNeeded: value >> 16),
            UInt8(truncatingIfNeeded: value >> 8),
            UInt8(truncatingIfNeeded: value),
        ])
    }

    static func be(_ value: UInt64) -> Data {
        Data((0..<8).reversed().map { UInt8(truncatingIfNeeded: value >> ($0 * 8)) })
    }

    static func le(_ value: UInt32) -> Data {
        Data((0..<4).map { UInt8(truncatingIfNeeded: value >> ($0 * 8)) })
    }

    static func be24(_ value: UInt32) -> Data {
        Data([
            UInt8(truncatingIfNeeded: value >> 16),
            UInt8(truncatingIfNeeded: value >> 8),
            UInt8(truncatingIfNeeded: value),
        ])
    }

    static func synchsafe(_ value: UInt32) -> Data {
        Data([
            UInt8((value >> 21) & 0x7F),
            UInt8((value >> 14) & 0x7F),
            UInt8((value >> 7) & 0x7F),
            UInt8(value & 0x7F),
        ])
    }

    static func fourCC(_ s: String) -> Data {
        var d = Data(s.utf8)
        while d.count < 4 { d.append(0x20) }
        return d.prefix(4)
    }

    /// Undoes ID3 unsynchronisation: every `FF 00` becomes `FF`.
    func deunsynchronised() -> Data {
        var out = Data()
        out.reserveCapacity(count)
        var previousWasFF = false
        for byte in self {
            if previousWasFF && byte == 0x00 {
                previousWasFF = false
                continue
            }
            out.append(byte)
            previousWasFF = (byte == 0xFF)
        }
        return out
    }
}

/// UTF-16LE with a byte-order mark — the only Unicode encoding ID3v2.3 permits.
enum UTF16Text {
    static func encode(_ s: String) -> Data {
        var d = Data([0xFF, 0xFE])
        for unit in Array(s.utf16) {
            d.append(UInt8(truncatingIfNeeded: unit))
            d.append(UInt8(truncatingIfNeeded: unit >> 8))
        }
        return d
    }
}
