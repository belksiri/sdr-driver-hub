import Foundation

/// DEVELOPMENT ONLY — excluded from App Store builds (`generate_project.rb`).
///
/// The public rtl_tcp protocol: a 12-byte greeting ("RTL0", tuner type,
/// gain count, big-endian), then the client sends 5-byte commands (one
/// command byte, a big-endian 32-bit parameter) and the server streams
/// unsigned 8-bit interleaved I/Q.
public enum RtlTcpProtocol {
    public static func greeting(tunerType: UInt32, gainCount: UInt32) -> Data {
        var d = Data("RTL0".utf8)
        withUnsafeBytes(of: tunerType.bigEndian) { d.append(contentsOf: $0) }
        withUnsafeBytes(of: gainCount.bigEndian) { d.append(contentsOf: $0) }
        return d
    }
}

/// Decoding of one 5-byte rtl_tcp command (development server only).
extension RtlCommand {
    public init(code: UInt8, value v: UInt32) {
        switch code {
        case 0x01: self = .frequency(v)
        case 0x02: self = .sampleRate(v)
        case 0x03: self = .gainMode(manual: v != 0)
        case 0x04: self = .gain(tenthDb: Int32(bitPattern: v))
        case 0x05: self = .frequencyCorrection(Int32(bitPattern: v))
        case 0x06: self = .ifGain(v)
        case 0x07: self = .testMode(v != 0)
        case 0x08: self = .rtlAgc(v != 0)
        case 0x09: self = .directSampling(v)
        case 0x0A: self = .offsetTuning(v != 0)
        case 0x0B: self = .rtlXtal(v)
        case 0x0C: self = .tunerXtal(v)
        case 0x0D: self = .gainByIndex(v)
        case 0x0E: self = .biasTee(v != 0)
        default: self = .unknown(code: code, value: v)
        }
    }
}

/// Reassembles 5-byte commands across TCP segment boundaries.
public struct RtlTcpCommandParser {
    private var pending = [UInt8]()
    public init() {}

    public mutating func feed(_ bytes: Data) -> [RtlCommand] {
        pending.append(contentsOf: bytes)
        var out = [RtlCommand]()
        var i = 0
        while pending.count - i >= 5 {
            let v = UInt32(pending[i + 1]) << 24 | UInt32(pending[i + 2]) << 16
                  | UInt32(pending[i + 3]) << 8 | UInt32(pending[i + 4])
            out.append(RtlCommand(code: pending[i], value: v))
            i += 5
        }
        pending.removeFirst(i)
        return out
    }
}
