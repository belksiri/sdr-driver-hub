import XCTest
@testable import HubCore

/// Un backend factice : ce que devra satisfaire tout futur backend (Pluto…).
private final class FakeBackend: SdrBackend {
    let id: SdrBackendID
    let displayName: String
    let backendVersion = "1.0.0"
    var availability: BackendAvailability
    var deviceInfo: BackendDeviceInfo?
    let capabilities = BackendCapabilities(
        frequencyRangeHz: 70_000_000...6_000_000_000, sampleRateRangesHz: [65_105...61_440_000],
        maxLosslessSampleRateHz: 4_000_000, gainStepsTenthDb: [0, 10, 20],
        supportsAutomaticGain: true, supportsBiasTee: false, transports: [.rtlTcp(port: 1235)])
    var configuration = BackendConfiguration(frequencyHz: 100_000_000, sampleRateHz: 2_048_000, gain: .automatic)
    var isServiceRunning = false
    var stats = BackendStatistics()
    var lastError: BackendUserError?
    var onChange: (() -> Void)?

    init(_ id: String, _ name: String, _ availability: BackendAvailability) {
        self.id = SdrBackendID(id); displayName = name; self.availability = availability
    }
    func startService() { isServiceRunning = true; onChange?() }
    func stopService() { isServiceRunning = false; onChange?() }
    func apply(_ c: BackendConfiguration) throws {
        if stats.clientConnected { throw NSError(domain: "client", code: 1) }
        configuration = c
    }
    func statistics() -> BackendStatistics { stats }
    func refresh() { onChange?() }
    func runSelfTest(_ done: @escaping (SelfTestResult) -> Void) {
        done(SelfTestResult(passed: availability.deviceDetected, summary: "fake"))
    }
}

final class SdrBackendTests: XCTestCase {

    func testRegistryKeepsStableIdsAndRefusesDuplicates() {
        let r = BackendRegistry()
        r.register(FakeBackend("rtlsdr", "RTL-SDR", .waitingForDevice))
        r.register(FakeBackend("pluto", "ADALM-Pluto", .waitingForDevice))
        XCTAssertEqual(r.backends.map(\.id), [.rtlSdr, SdrBackendID("pluto")])
        XCTAssertEqual(r.backend(.rtlSdr)?.displayName, "RTL-SDR")
        XCTAssertNil(r.backend(SdrBackendID("airspy")))
    }

    func testPrimaryPrefersStreamingThenDetectedThenFirst() {
        let r = BackendRegistry()
        let a = FakeBackend("rtlsdr", "RTL-SDR", .waitingForDevice)
        let b = FakeBackend("pluto", "ADALM-Pluto", .waitingForDevice)
        r.register(a); r.register(b)
        XCTAssertTrue(r.primary === a)
        b.availability = .ready
        XCTAssertTrue(r.primary === b)
        a.availability = .streaming
        XCTAssertTrue(r.primary === a)
    }

    func testSpectrumReadinessTellsWhatToDo() {
        func eval(_ a: BackendAvailability, running: Bool = true, client: Bool = false) -> SpectrumReadiness {
            .evaluate(availability: a, serviceRequired: true, serviceRunning: running,
                      clientConnected: client, deviceName: "RTL-SDR")
        }
        XCTAssertEqual(eval(.ready), .canConnect)
        XCTAssertEqual(eval(.streaming, client: true), .connected)
        guard case .notReady(let e1) = eval(.driverNotEnabled) else { return XCTFail() }
        XCTAssertTrue(e1.recovery!.contains("Settings"))
        guard case .notReady(let e2) = eval(.waitingForDevice) else { return XCTFail() }
        XCTAssertTrue(e2.message.contains("RTL-SDR"))
        XCTAssertTrue(e2.recovery!.contains("Drivers"), "the driver may simply be off")
        guard case .notReady(let e3) = eval(.ready, running: false) else { return XCTFail() }
        XCTAssertTrue(e3.message.contains("stopped"))
        let removed = BackendUserError("The RTL-SDR was unplugged.", recovery: "Plug it back in.")
        XCTAssertEqual(eval(.failed(removed)), .notReady(removed))
    }

    /// Accès direct (protocole client v2) : Spectrum Analyzer One ouvre le
    /// pilote lui-même. Le compagnon arrêté, ou sans service, ne l'en empêche
    /// pas : seul l'état du pilote et du matériel compte.
    func testDirectDriverAccessDoesNotDependOnTheCompanionService() {
        func eval(_ a: BackendAvailability, running: Bool, client: Bool = false) -> SpectrumReadiness {
            .evaluate(availability: a, serviceRequired: false, serviceRunning: running,
                      clientConnected: client, deviceName: "RTL-SDR")
        }
        XCTAssertEqual(eval(.ready, running: false), .canConnect)
        XCTAssertEqual(eval(.streaming, running: false), .canConnect)
        XCTAssertEqual(eval(.streaming, running: false, client: true), .connected)
        guard case .notReady = eval(.driverNotEnabled, running: false) else { return XCTFail() }
        guard case .notReady = eval(.waitingForDevice, running: false) else { return XCTFail() }
        // Par défaut, le service n'est PAS requis : c'est le chemin normal.
        XCTAssertEqual(SpectrumReadiness.evaluate(availability: .ready, serviceRunning: false,
                                                  clientConnected: false, deviceName: "RTL-SDR"),
                       .canConnect)
        XCTAssertNotEqual(IqTransportKind.directDriver, IqTransportKind.rtlTcp(port: 1234))
    }

    func testAFutureBackendFitsTheContractWithoutTouchingTheApp() throws {
        let pluto = FakeBackend("pluto", "ADALM-Pluto", .ready)
        let backend: SdrBackend = pluto
        backend.startService()
        XCTAssertTrue(backend.isServiceRunning)
        XCTAssertTrue(backend.capabilities.supportsSampleRate(2_048_000))
        XCTAssertFalse(backend.capabilities.supportsSampleRate(10_000))
        try backend.apply(BackendConfiguration(frequencyHz: 433_920_000, sampleRateHz: 1_000_000,
                                               gain: .manual(tenthDb: 200)))
        XCTAssertEqual(backend.configuration.frequencyHz, 433_920_000)
        pluto.stats.clientConnected = true
        XCTAssertThrowsError(try backend.apply(backend.configuration), "a connected client keeps control")
    }

    func testThroughputSamplerUsesTheLastIntervalAndSurvivesACounterReset() {
        var t = ThroughputSampler()
        t.record(totalBytes: 0, at: 10)
        XCTAssertEqual(t.bytesPerSecond, 0)
        t.record(totalBytes: 5_120_000, at: 11)
        XCTAssertEqual(t.bytesPerSecond, 5_120_000, accuracy: 1)
        t.record(totalBytes: 7_680_000, at: 11.5)
        XCTAssertEqual(t.bytesPerSecond, 5_120_000, accuracy: 1)
        t.record(totalBytes: 1_000, at: 12)       // nouvelle ouverture
        XCTAssertEqual(t.bytesPerSecond, 0)
    }

    func testStatisticsNeverMixStages() {
        var s = BackendStatistics()
        s.ringLostBlocks = 2; s.sendLostBlocks = 3; s.usbErrors = 7
        XCTAssertEqual(s.totalLostBlocks, 5, "USB errors are not lost blocks")
    }
}
