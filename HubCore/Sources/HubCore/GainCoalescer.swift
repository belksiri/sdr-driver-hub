import Foundation

/// The gain a client asked for, merged over a burst of commands.
public struct GainRequest: Equatable {
    /// `true` manual, `false` automatic, `nil` unchanged.
    public var manual: Bool?
    /// Manual gain in tenths of dB (0x04), or `nil`.
    public var tenthDb: Int32?
    /// Gain step index (0x0D), or `nil`. Exclusive with [tenthDb].
    public var index: UInt32?

    public init(manual: Bool? = nil, tenthDb: Int32? = nil, index: UInt32? = nil) {
        self.manual = manual
        self.tenthDb = tenthDb
        self.index = index
    }

    mutating func merge(_ cmd: RtlCommand) {
        switch cmd {
        case .gainMode(let m): manual = m
        case .gain(let t): tenthDb = t; index = nil
        case .gainByIndex(let i): index = i; tenthDb = nil
        default: break
        }
    }
}

/// Coalesces the gain commands of one client: latest value wins.
///
/// A dragged gain slider sends hundreds of commands; each one costs the tuner
/// about a hundred milliseconds of I²C. Instead of queuing them all, the
/// coalescer keeps ONE pending request (every new command is merged into it),
/// applies at most one request at a time, and no more often than
/// `minInterval`. When a request completes and another is pending, the next
/// one is scheduled, so the final value is always applied. Nothing here
/// touches the connection or the stream.
///
/// Not thread-safe: the server drives it from its own serial queue. Time is
/// passed in so the behaviour is testable without waiting.
public final class GainCoalescer {

    public static func isGain(_ cmd: RtlCommand) -> Bool {
        switch cmd {
        case .gainMode, .gain, .gainByIndex: return true
        default: return false
        }
    }

    public let minInterval: TimeInterval
    public private(set) var pending: GainRequest?
    public private(set) var busy = false
    public private(set) var scheduled = false
    public private(set) var cancelled = false
    /// Gain commands received, and requests actually handed to the device.
    public private(set) var received = 0
    public private(set) var applied = 0
    private var lastApply = -Double.infinity

    /// About 15 updates per second at most.
    public init(minInterval: TimeInterval = 1.0 / 15) {
        self.minInterval = minInterval
    }

    /// Merges `cmd`. Returns the delay after which `take` must be called, or
    /// `nil` when a flush is already scheduled or a request is being applied
    /// (its completion schedules the next one).
    public func push(_ cmd: RtlCommand, now: TimeInterval) -> TimeInterval? {
        guard !cancelled, Self.isGain(cmd) else { return nil }
        received += 1
        var p = pending ?? GainRequest()
        p.merge(cmd)
        pending = p
        if scheduled || busy { return nil }
        scheduled = true
        return max(0, lastApply + minInterval - now)
    }

    /// The request to apply now, or `nil` (nothing pending, or cancelled).
    public func take() -> GainRequest? {
        scheduled = false
        guard !cancelled, !busy, let p = pending else { return nil }
        pending = nil
        busy = true
        applied += 1
        return p
    }

    /// The device has applied the request handed out by `take`. Returns the
    /// delay before the next `take` when another request is pending.
    public func complete(now: TimeInterval) -> TimeInterval? {
        busy = false
        lastApply = now
        guard !cancelled, pending != nil, !scheduled else { return nil }
        scheduled = true
        return minInterval
    }

    /// Stop or disconnection: the pending request is dropped, nothing more is
    /// handed out.
    public func cancel() {
        cancelled = true
        pending = nil
    }
}
