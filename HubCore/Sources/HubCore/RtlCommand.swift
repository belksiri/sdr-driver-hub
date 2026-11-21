import Foundation

/// The RTL-SDR settings vocabulary the Hub applies through the driver's
/// client protocol (one `BRTLCommand` per case). It mirrors the rtl_tcp
/// command set one to one, so the development rtl_tcp server decodes into it
/// (`RtlTcpProtocol.swift`) — but this type itself carries no wire format and
/// is the only part compiled into App Store builds.
public enum RtlCommand: Equatable {
    case frequency(UInt32)            // 0x01, Hz
    case sampleRate(UInt32)           // 0x02, Hz
    case gainMode(manual: Bool)       // 0x03, 0 = auto
    case gain(tenthDb: Int32)         // 0x04
    case frequencyCorrection(Int32)   // 0x05, ppm
    case ifGain(UInt32)               // 0x06 (accepted, not mapped)
    case testMode(Bool)               // 0x07
    case rtlAgc(Bool)                 // 0x08
    case directSampling(UInt32)       // 0x09
    case offsetTuning(Bool)           // 0x0A
    case rtlXtal(UInt32)              // 0x0B (accepted, not mapped)
    case tunerXtal(UInt32)            // 0x0C (accepted, not mapped)
    case gainByIndex(UInt32)          // 0x0D
    case biasTee(Bool)                // 0x0E
    case unknown(code: UInt8, value: UInt32)
}
