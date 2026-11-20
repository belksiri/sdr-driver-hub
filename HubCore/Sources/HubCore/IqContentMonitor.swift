import Foundation

/// Checks that what the driver delivers is LIVE I/Q, not just bytes at the
/// right rate: a diagnostic that only measures throughput turns green on a
/// stale buffer sent over and over (seen with another app: a fixed 3 760-byte
/// pattern, repeated forever). Fed with raw unsigned 8-bit I/Q blocks.
public final class IqContentMonitor {
    public struct Snapshot: Equatable {
        public var blocks: UInt64 = 0
        public var bytes: UInt64 = 0
        /// Blocks whose content equals one of the recent blocks.
        public var repeatedBlocks: UInt64 = 0
        /// Blocks made of a short pattern repeated (period ≤ maxPeriod).
        public var periodicBlocks: UInt64 = 0
        public var lastPeriod: Int? = nil
        public var meanI: Double = 0, meanQ: Double = 0
        /// Standard deviation of I and Q around their means, in LSB.
        public var stdI: Double = 0, stdQ: Double = 0
        /// ADC end-stops (0 / 255), in % of components.
        public var clippingPercent: Double = 0
        public var lastHash: UInt64 = 0

        /// Live and plausible: content varies, never repeats, and the level
        /// is neither dead (all equal) nor saturated.
        public var looksLive: Bool {
            blocks >= 4 && repeatedBlocks == 0 && periodicBlocks == 0
                && stdI > 0.5 && stdQ > 0.5 && clippingPercent < 50
        }
    }

    public let historySize: Int
    public let maxPeriod: Int
    private var recent = [UInt64]()
    private var snap = Snapshot()
    private var sumI = 0.0, sumQ = 0.0, sumI2 = 0.0, sumQ2 = 0.0, n = 0.0
    private var clipped = 0.0, components = 0.0
    private let lock = NSLock()

    public init(historySize: Int = 64, maxPeriod: Int = 16_384) {
        self.historySize = historySize
        self.maxPeriod = maxPeriod
    }

    public func reset() {
        lock.lock(); defer { lock.unlock() }
        recent.removeAll(); snap = Snapshot()
        sumI = 0; sumQ = 0; sumI2 = 0; sumQ2 = 0; n = 0; clipped = 0; components = 0
    }

    public var snapshot: Snapshot { lock.lock(); defer { lock.unlock() }; return snap }

    /// FNV-1a 64 over the whole block.
    public static func hash(_ b: UnsafeRawBufferPointer) -> UInt64 {
        var h: UInt64 = 0xcbf2_9ce4_8422_2325
        for x in b { h = (h ^ UInt64(x)) &* 0x0000_0100_0000_01B3 }
        return h
    }

    /// The smallest period p ≤ maxPeriod such that the block is made of its
    /// first p bytes repeated, or nil. A 64-byte probe locates candidates.
    public static func period(_ b: UnsafeRawBufferPointer, maxPeriod: Int) -> Int? {
        let count = b.count
        guard count >= 256, let base = b.baseAddress else { return nil }
        let probe = 64
        var p = 1
        while p <= min(maxPeriod, count - probe) {
            if memcmp(base, base + p, probe) == 0,
               memcmp(base, base + p, count - p) == 0 {
                return p
            }
            p += 1
        }
        return nil
    }

    public func feed(_ b: UnsafeRawBufferPointer) {
        let h = Self.hash(b)
        // Periodicity is costly to test exhaustively: sample one block in 16.
        let testPeriod = snap.blocks % 16 == 0
        let per = testPeriod ? Self.period(b, maxPeriod: maxPeriod) : nil
        var si = 0.0, sq = 0.0, si2 = 0.0, sq2 = 0.0, cl = 0.0
        var i = 0
        let c = b.count & ~1
        while i < c {
            let iv = Double(b[i]) - 127.5, qv = Double(b[i + 1]) - 127.5
            si += iv; sq += qv; si2 += iv * iv; sq2 += qv * qv
            if b[i] == 0 || b[i] == 255 { cl += 1 }
            if b[i + 1] == 0 || b[i + 1] == 255 { cl += 1 }
            i += 2
        }
        lock.lock(); defer { lock.unlock() }
        snap.blocks += 1
        snap.bytes += UInt64(b.count)
        if recent.contains(h) { snap.repeatedBlocks += 1 }
        recent.append(h)
        if recent.count > historySize { recent.removeFirst(recent.count - historySize) }
        snap.lastHash = h
        if let p = per { snap.periodicBlocks += 1; snap.lastPeriod = p }
        let pairs = Double(c / 2)
        sumI += si; sumQ += sq; sumI2 += si2; sumQ2 += sq2; n += pairs
        clipped += cl; components += Double(c)
        if n > 0 {
            let mi = sumI / n, mq = sumQ / n
            snap.meanI = mi + 127.5; snap.meanQ = mq + 127.5
            snap.stdI = (max(0, sumI2 / n - mi * mi)).squareRoot()
            snap.stdQ = (max(0, sumQ2 / n - mq * mq)).squareRoot()
        }
        snap.clippingPercent = components > 0 ? clipped * 100 / components : 0
    }
}
