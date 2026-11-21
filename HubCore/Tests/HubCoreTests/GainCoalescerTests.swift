import XCTest
@testable import HubCore

/// Simulates the server: the device takes `applyCost` seconds per request.
private func simulateDrag(_ cmds: [(TimeInterval, RtlCommand)], applyCost: TimeInterval,
                          coalescer c: GainCoalescer) -> [(TimeInterval, GainRequest)] {
    var timers: [TimeInterval] = []           // due times of scheduled `take`
    var completions: [TimeInterval] = []      // due times of `complete`
    var appliedAt: [(TimeInterval, GainRequest)] = []
    var i = 0
    var now: TimeInterval = 0
    while i < cmds.count || !timers.isEmpty || !completions.isEmpty {
        let nextCmd = i < cmds.count ? cmds[i].0 : .infinity
        let nextTimer = timers.min() ?? .infinity
        let nextDone = completions.min() ?? .infinity
        now = min(nextCmd, nextTimer, nextDone)
        if now == nextDone {
            completions.remove(at: completions.firstIndex(of: nextDone)!)
            if let d = c.complete(now: now) { timers.append(now + d) }
        } else if now == nextTimer {
            timers.remove(at: timers.firstIndex(of: nextTimer)!)
            if let r = c.take() { appliedAt.append((now, r)); completions.append(now + applyCost) }
        } else {
            if let d = c.push(cmds[i].1, now: now) { timers.append(now + d) }
            i += 1
        }
    }
    return appliedAt
}

final class GainCoalescerTests: XCTestCase {

    /// A full-range slider drag: 600 commands in 3 s (mode + gain, as Spectrum sends).
    private func drag() -> [(TimeInterval, RtlCommand)] {
        var out: [(TimeInterval, RtlCommand)] = []
        for k in 0..<300 {
            let t = Double(k) * 0.01
            out.append((t, .gainMode(manual: true)))
            out.append((t, .gain(tenthDb: Int32(k * 496 / 299))))
        }
        return out
    }

    func testBurstIsCoalescedAndRateLimited() {
        let c = GainCoalescer(minInterval: 1.0 / 15)
        let applied = simulateDrag(drag(), applyCost: 0.1, coalescer: c)
        XCTAssertEqual(c.received, 600)
        XCTAssertLessThanOrEqual(applied.count, 31, "≈ 1 apply per 100 ms of I²C, never 600")
        XCTAssertGreaterThan(applied.count, 5, "the slider still follows during the drag")
        for (a, b) in zip(applied, applied.dropFirst()) {
            XCTAssertGreaterThanOrEqual(b.0 - a.0, 1.0 / 15 - 1e-9, "never faster than 15/s")
        }
    }

    func testFinalValueIsAlwaysApplied() {
        let c = GainCoalescer()
        let applied = simulateDrag(drag(), applyCost: 0.1, coalescer: c)
        XCTAssertEqual(applied.last?.1.tenthDb, 496)
        XCTAssertEqual(applied.last?.1.manual, true)
        XCTAssertNil(c.pending)
        XCTAssertFalse(c.busy)
        XCTAssertFalse(c.scheduled)
        // No backlog after release: the last apply starts at most one apply
        // (100 ms) + one interval after the last command (2.99 s).
        XCTAssertLessThanOrEqual(applied.last!.0, 2.99 + 0.1 + 1.0 / 15 + 1e-9)
    }

    func testAtMostOnePendingRequest() {
        let c = GainCoalescer()
        XCTAssertEqual(c.push(.gain(tenthDb: 100), now: 0), 0)
        XCTAssertNil(c.push(.gain(tenthDb: 200), now: 0))
        XCTAssertNil(c.push(.gainMode(manual: true), now: 0))
        XCTAssertEqual(c.pending, GainRequest(manual: true, tenthDb: 200))
        XCTAssertEqual(c.take(), GainRequest(manual: true, tenthDb: 200))
        XCTAssertNil(c.take(), "nothing twice")
        XCTAssertNil(c.push(.gain(tenthDb: 300), now: 0.01), "busy: completion schedules")
        XCTAssertNil(c.push(.gainByIndex(5), now: 0.02))
        XCTAssertEqual(c.pending, GainRequest(index: 5), "index replaces the dB value")
        XCTAssertEqual(c.complete(now: 0.1), 1.0 / 15)
        XCTAssertEqual(c.take(), GainRequest(index: 5))
        XCTAssertNil(c.complete(now: 0.2))
    }

    func testIdleSingleCommandIsAppliedWithoutDelay() {
        let c = GainCoalescer()
        XCTAssertEqual(c.push(.gainMode(manual: false), now: 10), 0)
        XCTAssertEqual(c.take(), GainRequest(manual: false))
    }

    func testNonGainCommandsAreNotCoalesced() {
        let c = GainCoalescer()
        XCTAssertNil(c.push(.frequency(88_800_000), now: 0))
        XCTAssertNil(c.pending)
        XCTAssertEqual(c.received, 0)
        XCTAssertFalse(GainCoalescer.isGain(.biasTee(true)))
    }

    func testStopOrDisconnectDropsThePendingRequest() {
        let c = GainCoalescer()
        _ = c.push(.gain(tenthDb: 100), now: 0)
        _ = c.take()
        _ = c.push(.gain(tenthDb: 400), now: 0.01)
        c.cancel()                           // client gone / Stop
        XCTAssertNil(c.pending)
        XCTAssertNil(c.complete(now: 0.1), "nothing rescheduled after a stop")
        XCTAssertNil(c.take())
        XCTAssertNil(c.push(.gain(tenthDb: 50), now: 0.2), "a dead client's commands are ignored")
        XCTAssertEqual(c.applied, 1)
    }

    func testNewClientStartsClean() {
        let old = GainCoalescer()
        _ = old.push(.gain(tenthDb: 100), now: 0)
        old.cancel()
        let fresh = GainCoalescer()          // the server builds one per client
        XCTAssertEqual(fresh.push(.gain(tenthDb: 297), now: 0), 0)
        XCTAssertEqual(fresh.take(), GainRequest(tenthDb: 297))
    }
}
