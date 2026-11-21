import XCTest
@testable import HubCore

final class RtlTcpProtocolTests: XCTestCase {
    func testGreetingIsRtl0TunerAndGainCountBigEndian() {
        let g = RtlTcpProtocol.greeting(tunerType: 5, gainCount: 29)
        XCTAssertEqual(Array(g), [0x52, 0x54, 0x4C, 0x30, 0, 0, 0, 5, 0, 0, 0, 29])
    }

    func testCommandsAreReassembledAcrossSegments() {
        var p = RtlTcpCommandParser()
        // freq 88.8 MHz split 2 + 3, then rate + manual gain + gain 297 in one go
        XCTAssertEqual(p.feed(Data([0x01, 0x05])), [])
        XCTAssertEqual(p.feed(Data([0x4A, 0xFB, 0x00])), [.frequency(88_800_000)])
        let more = p.feed(Data([0x02, 0x00, 0x27, 0x10, 0x00,
                                0x03, 0, 0, 0, 1,
                                0x04, 0, 0, 0x01, 0x29,
                                0x0E, 0, 0, 0]))   // trailing partial bias-tee
        XCTAssertEqual(more, [.sampleRate(2_560_000), .gainMode(manual: true), .gain(tenthDb: 297)])
        XCTAssertEqual(p.feed(Data([0x01])), [.biasTee(true)])
    }

    func testEveryCommandCodeMaps() {
        XCTAssertEqual(RtlCommand(code: 0x05, value: UInt32(bitPattern: -3)), .frequencyCorrection(-3))
        XCTAssertEqual(RtlCommand(code: 0x08, value: 1), .rtlAgc(true))
        XCTAssertEqual(RtlCommand(code: 0x09, value: 2), .directSampling(2))
        XCTAssertEqual(RtlCommand(code: 0x0D, value: 7), .gainByIndex(7))
        XCTAssertEqual(RtlCommand(code: 0x3F, value: 9), .unknown(code: 0x3F, value: 9))
    }
}

final class IqContentMonitorTests: XCTestCase {
    /// Gaussian-ish live samples: a tone plus noise, never the same twice.
    private func liveBlock(_ k: Int, bytes: Int = 65_536) -> [UInt8] {
        var rng = UInt64(0x9E37_79B9_7F4A_7C15) &+ UInt64(k)
        var out = [UInt8](repeating: 0, count: bytes)
        for i in stride(from: 0, to: bytes, by: 2) {
            rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17
            let noise = Double(rng % 21) - 10
            let ph = Double(k * bytes / 2 + i / 2) * 0.05
            out[i] = UInt8(clamping: Int(127.5 + 40 * cos(ph) + noise))
            out[i + 1] = UInt8(clamping: Int(127.5 + 40 * sin(ph) + noise))
        }
        return out
    }

    func testLiveBlocksLookLive() {
        let m = IqContentMonitor()
        for k in 0..<20 { liveBlock(k).withUnsafeBytes { m.feed($0) } }
        let s = m.snapshot
        XCTAssertEqual(s.blocks, 20)
        XCTAssertEqual(s.repeatedBlocks, 0)
        XCTAssertEqual(s.periodicBlocks, 0)
        XCTAssertTrue(s.looksLive, "\(s)")
        XCTAssertEqual(s.meanI, 127.5, accuracy: 3)
        XCTAssertGreaterThan(s.stdI, 20)
    }

    func testAStaleBlockSentAgainIsDetected() {
        let m = IqContentMonitor()
        let b = liveBlock(1)
        for k in 0..<6 { liveBlock(k + 10).withUnsafeBytes { m.feed($0) } }
        b.withUnsafeBytes { m.feed($0) }
        b.withUnsafeBytes { m.feed($0) }      // the same block again
        XCTAssertEqual(m.snapshot.repeatedBlocks, 1)
        XCTAssertFalse(m.snapshot.looksLive)
    }

    /// The failure seen in the field: a fixed 3 760-byte pattern, forever.
    func testThe3760BytePatternIsDetected() {
        var seed = UInt64(42)
        let pattern: [UInt8] = (0..<3760).map { _ in
            seed ^= seed << 13; seed ^= seed >> 7; seed ^= seed << 17
            return UInt8(truncatingIfNeeded: seed)
        }
        let m = IqContentMonitor()
        var stream = [UInt8]()
        while stream.count < 65_536 * 8 { stream += pattern }
        for k in 0..<8 {
            stream[(k * 65_536)..<((k + 1) * 65_536)].withUnsafeBytes { m.feed($0) }
        }
        let s = m.snapshot
        XCTAssertGreaterThan(s.periodicBlocks, 0)
        XCTAssertEqual(s.lastPeriod, 3760)
        XCTAssertFalse(s.looksLive, "random-looking but repeated: not live")
    }

    func testDeadSignalIsNotLive() {
        let m = IqContentMonitor()
        for _ in 0..<6 { [UInt8](repeating: 127, count: 4096).withUnsafeBytes { m.feed($0) } }
        XCTAssertFalse(m.snapshot.looksLive)
    }
}

final class IqSendQueueTests: XCTestCase {
    func testOrderIsKeptAndPartialPopsReturnWholeBlocks() {
        let q = IqSendQueue(maxBytes: 1000)
        q.push(Data([1, 2, 3])); q.push(Data([4, 5])); q.push(Data([6]))
        XCTAssertEqual(q.pop(upTo: 5), Data([1, 2, 3, 4, 5]))
        XCTAssertEqual(q.pop(upTo: 5), Data([6]))
        XCTAssertNil(q.pop(upTo: 5))
    }

    func testASlowClientDropsAndCountsInsteadOfBlocking() {
        let q = IqSendQueue(maxBytes: 10)
        XCTAssertTrue(q.push(Data(count: 6)))
        XCTAssertFalse(q.push(Data(count: 6)), "over the bound: dropped")
        XCTAssertEqual(q.droppedBlocks, 1)
        XCTAssertEqual(q.droppedBytes, 6)
        XCTAssertEqual(q.queuedBytes, 6)
        _ = q.pop(upTo: 100)
        XCTAssertTrue(q.push(Data(count: 6)), "room again once drained")
    }
}
