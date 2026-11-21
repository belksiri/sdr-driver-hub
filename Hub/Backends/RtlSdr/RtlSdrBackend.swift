//
//  RtlSdrBackend.swift — Le backend RTL-SDR de SDR Driver Hub.
//
//  Assemble les pièces validées sur l'iPad (pilote DriverKit avec librtlsdr
//  intégrée, ring partagé) derrière le contrat `SdrBackend`. L'écran ne
//  connaît que ce contrat.
//
//  Chemin normal : Spectrum Analyzer One ouvre LUI-MÊME le pilote (protocole
//  client v2) ; ce backend n'en est alors que l'observateur (identité,
//  compteurs du pilote, occupation du récepteur). Le serveur rtl_tcp et le
//  maintien en arrière-plan n'existent que dans les builds de développement
//  (condition de compilation HUB_DEV_RTLTCP, absente des builds App Store).
//
//  Fil d'exécution : appelé depuis la file principale. Le matériel n'est
//  touché que sur `device.queue` (librtlsdr n'est pas réentrante) ; l'état
//  affiché est un instantané recopié sur la file principale par `refresh()`.
//

import Foundation

final class RtlSdrBackend: SdrBackend {

    let id = SdrBackendID.rtlSdr
    let displayName = "RTL-SDR"
    let backendVersion: String

    private let device = RtlDevice()
    #if HUB_DEV_RTLTCP
    private lazy var server = RtlTcpServer(device: device)
    #endif

    var onChange: (() -> Void)?

    // Instantané, file principale uniquement.
    private(set) var availability: BackendAvailability = .waitingForDevice
    private(set) var deviceInfo: BackendDeviceInfo?
    private(set) var configuration = BackendConfiguration(frequencyHz: 100_000_000,
                                                          sampleRateHz: 2_048_000, gain: .automatic)
    private(set) var capabilities = RtlSdrBackend.familyCapabilities
    private(set) var isServiceRunning = false
    private(set) var lastError: BackendUserError?
    private var stats = BackendStatistics()

    /// Ce que tout RTL2832U + R820T/R828D sait faire (librtlsdr). Les paliers de
    /// gain réels sont ceux du tuner détecté, lus à l'ouverture.
    static let familyCapabilities = BackendCapabilities(
        frequencyRangeHz: 24_000_000...1_766_000_000,
        sampleRateRangesHz: [225_001...300_000, 900_001...3_200_000],
        maxLosslessSampleRateHz: 2_560_000,
        gainStepsTenthDb: [],
        supportsAutomaticGain: true,
        supportsBiasTee: true,
        transports: RtlSdrBackend.transports)

    #if HUB_DEV_RTLTCP
    static let transports: [IqTransportKind] = [.directDriver, .rtlTcp(port: RtlTcpServer.port)]
    #else
    static let transports: [IqTransportKind] = [.directDriver]
    #endif

    init(backendVersion: String) {
        self.backendVersion = backendVersion
        #if HUB_DEV_RTLTCP
        server.onClientChanged = { [weak self] _ in self?.refresh() }
        server.onListening = { [weak self] on, error in
            guard let self else { return }
            self.isServiceRunning = on
            self.lastError = error.map {
                BackendUserError("The local service could not start.", recovery: $0)
            }
            self.onChange?()
        }
        #endif
        device.onRemoved = { [weak self] in
            #if HUB_DEV_RTLTCP
            self?.server.deviceRemoved()
            #endif
            self?.refresh()
        }
        device.onLinkStateChange = { [weak self] _ in self?.refresh() }
    }

    // MARK: Service (développement seulement)

    #if HUB_DEV_RTLTCP
    func startService() { server.start() }
    func stopService() { server.stop() }
    private var devClientConnected: Bool { server.hasClient }
    private var devSendLost: UInt64 { server.sendQueue.droppedBlocks }
    #else
    func startService() {}
    func stopService() {}
    private var devClientConnected: Bool { false }
    private var devSendLost: UInt64 { 0 }
    #endif

    // MARK: Réglages hors client

    func apply(_ c: BackendConfiguration) throws {
        guard !stats.clientConnected else {
            throw BackendUserErrorBox(BackendUserError(
                "Spectrum Analyzer One is connected and controls the receiver."))
        }
        let d = device
        d.queue.async {
            do {
                try d.open()
                d.apply(.sampleRate(c.sampleRateHz))
                d.apply(.frequency(c.frequencyHz))
                switch c.gain {
                case .automatic: d.apply(.gainMode(manual: false))
                case .manual(let t): d.applyGain(GainRequest(manual: true, tenthDb: t))
                }
                d.apply(.biasTee(c.biasTee))
                // Rendu aussitôt : le récepteur reste libre pour Spectrum.
                if !self.devClientConnected { d.close() }
            } catch {
                HubLog.shared.log("apply refused — \(error)")
            }
            DispatchQueue.main.async { self.refresh() }
        }
    }

    // MARK: Instantané

    func statistics() -> BackendStatistics { stats }

    /// Recopie l'état du matériel (lu sur sa file) vers la file principale.
    func refresh() {
        let d = device
        let linkState = d.transport.state
        let devClient = devClientConnected, devLost = devSendLost
        d.queue.async { [weak self] in
            // Lecture passive : aucune prise du récepteur. Possible pendant que
            // Spectrum Analyzer One reçoit en accès direct.
            let attached = linkState == .deviceAttached || linkState == .receiving
            let di = attached ? d.readInfo() : nil
            let ring = di != nil ? (try? d.transport.ringStats()) : nil
            let tunerOk = di.map { $0.tunerStatus == 0 } ?? false
            let otherOwner = d.ownedByAnotherClient
            var st = BackendStatistics()
            // Octets produits par le PILOTE : le débit réel, quel que soit le
            // client qui reçoit (Spectrum Analyzer One ou SDR Driver Hub).
            st.deliveredBytes = ring?.bytesProduced ?? d.deliveredBytes
            st.usbErrors = (ring?.usbErrors ?? 0) + (di?.commandsFailed ?? 0)
            // Les pertes ne sont connues que du consommateur : celles de SDR
            // Driver Hub quand il reçoit, jamais devinées pour un autre client.
            st.ringLostBlocks = d.isOpen ? d.lostBlocks + (ring?.overflowBlocks ?? 0) : 0
            st.sendLostBlocks = devLost
            st.clientConnected = devClient || otherOwner
            let snap = d.monitor.snapshot
            st.iqContentLive = snap.blocks == 0 ? nil : snap.looksLive
            let info: BackendDeviceInfo? = tunerOk
                ? BackendDeviceInfo(product: d.product, detail: d.tunerName,
                                    driverVersion: d.driverBuildId == 0 ? nil : "\(d.driverBuildId)")
                : nil
            let gain = di.map { $0.gainTenthDb } ?? d.appliedGainTenthDb
            let config = BackendConfiguration(
                frequencyHz: di?.frequencyHz ?? d.frequencyHz,
                sampleRateHz: di?.sampleRateHz ?? d.sampleRateHz,
                gain: gain < 0 ? .automatic : .manual(tenthDb: gain),
                biasTee: di.map { ($0.flags & BRTL_FLAG_BIAS_TEE) != 0 } ?? d.biasTee)
            let steps = tunerOk ? d.gainSteps() : []
            let streaming = d.isStreaming || (di.map { ($0.flags & BRTL_FLAG_STREAMING) != 0 } ?? false)
            let open = tunerOk
            DispatchQueue.main.async {
                guard let self else { return }
                self.stats = st
                if let info { self.deviceInfo = info }
                if open { self.configuration = config }
                if !steps.isEmpty { self.capabilities.gainStepsTenthDb = steps }
                self.availability = Self.availability(linkState, streaming: streaming)
                if case .waitingForDevice = self.availability { self.deviceInfo = nil }
                self.onChange?()
            }
        }
    }

    static func availability(_ s: ProbeState, streaming: Bool) -> BackendAvailability {
        switch s {
        case .driverNotEnabled:
            return .driverNotEnabled
        // Sur iPadOS, le service du pilote n'est publié qu'avec le RTL-SDR
        // branché ET le pilote activé : son absence ne permet pas de savoir
        // lequel des deux manque. Le message de reprise cite les deux.
        case .driverNotInstalled, .waitingForDevice:
            return .waitingForDevice
        case .deviceAttached:
            return streaming ? .streaming : .ready
        case .receiving:
            return .streaming
        case .terminated:
            return .failed(BackendUserError("The RTL-SDR was unplugged.",
                                            recovery: "Plug it back in; the service resumes by itself."))
        }
    }

    // MARK: Vérification locale

    /// 88,8 MHz, 2,56 MS/s, 29,7 dB, 1,5 s : réussie seulement si le débit est
    /// le bon ET le contenu vivant (jamais sur le débit seul).
    func runSelfTest(_ done: @escaping (SelfTestResult) -> Void) {
        guard !stats.clientConnected else {
            done(SelfTestResult(passed: false,
                                summary: "Spectrum Analyzer One is using the receiver: the check runs when it is free."))
            return
        }
        let d = device
        d.queue.async {
            var result: SelfTestResult
            do {
                try d.open()
                d.apply(.sampleRate(2_560_000))
                d.apply(.frequency(88_800_000))
                d.applyGain(GainRequest(manual: true, tenthDb: 297))
                let lost0 = d.lostBlocks
                let b0 = d.deliveredBytes
                let t0 = Date()
                try d.startRx()
                Thread.sleep(forTimeInterval: 1.5)
                let bytes = d.deliveredBytes - b0
                let secs = Date().timeIntervalSince(t0)
                d.close()      // rend le récepteur
                let s = d.monitor.snapshot
                let mbps = Double(bytes) / secs / 1e6
                let lost = d.lostBlocks - lost0
                let rateOk = abs(mbps - 5.12) / 5.12 < 0.10
                let passed = rateOk && s.looksLive && lost == 0
                result = SelfTestResult(passed: passed, summary: String(
                    format: "%.2f MB/s (expected 5.12) · IQ content %@ · %llu lost blocks",
                    mbps, s.looksLive ? "live" : "suspect", lost))
            } catch TransportError.busy {
                result = SelfTestResult(passed: false, summary:
                    "Spectrum Analyzer One is using the receiver: the check runs when it is free.")
            } catch {
                d.close()
                result = SelfTestResult(passed: false, summary: "\(error)")
            }
            DispatchQueue.main.async {
                done(result)
                self.refresh()
            }
        }
    }
}

/// `BackendUserError` levée comme une `Error`.
struct BackendUserErrorBox: Error, LocalizedError {
    let error: BackendUserError
    init(_ e: BackendUserError) { error = e }
    var errorDescription: String? { error.message }
}
