//
//  RtlDevice.swift — Le RTL-SDR, par le pilote DriverKit embarqué (SDR Driver Hub).
//
//  Même chemin que le prototype validé sur l'iPad (Belksiri Spectrum, branche
//  experiment/ipados-rtlsdr-v3-gpl) :
//    contrôle : librtlsdr (brtl_rtl_controller) ── brtl_transport ── user client
//    données  : dext ── ring partagé ── RingDrain ── ringDelivered (u8 bruts)
//  Les blocs NE sont PAS convertis : rtl_tcp transporte l'IQ non signé tel que
//  le RTL2832U le livre. Chaque bloc est aussi passé au contrôle de contenu.
//
//  Toutes les opérations passent par `queue` (librtlsdr n'est pas réentrante).
//

import Foundation

struct HubError: Error, CustomStringConvertible {
    let description: String
    init(_ d: String) { description = d }
}

final class RtlDevice: RingBlockConsumer {

    static let serviceName = "BelksiriRtlSdrDriver"
    static let blockBytes: UInt32 = 65_536
    static let inFlight: UInt32 = 16

    let transport = HackRFDriverKitTransport(serviceUserClass: RtlDevice.serviceName)
    let bridge: RtlControlBridge
    let queue = DispatchQueue(label: "hub.rtl.device")
    let monitor = IqContentMonitor()

    /// Destinataire des blocs bruts (le serveur). Appelé sur la file du drain.
    var onBlock: ((UnsafeRawBufferPointer) -> Void)?
    /// Le périphérique a disparu (retrait USB). Appelé sur la file principale.
    var onRemoved: (() -> Void)?

    private var ctrl: OpaquePointer?
    private(set) var info = brtl_device_info()
    private(set) var driverBuildId: UInt32 = 0
    private(set) var isOpen = false
    private(set) var isStreaming = false

    // Réglages demandés (valeurs par défaut de rtl_tcp) et appliqués.
    private(set) var frequencyHz: UInt32 = 100_000_000
    private(set) var sampleRateHz: UInt32 = 2_048_000
    private(set) var manualGain = false
    private(set) var gainTenthDb: Int32 = 0
    private(set) var appliedGainTenthDb: Int32 = -1
    private(set) var ppm: Int32 = 0
    private(set) var rtlAgc = false
    private(set) var directSampling: UInt32 = 0
    private(set) var biasTee = false
    private(set) var pllLocked: Int32 = -1
    private(set) var lastCommandStatus = "—"

    // Compteurs.
    private let countLock = NSLock()
    private var _bytes: UInt64 = 0
    private var _lostBlocks: UInt64 = 0
    var deliveredBytes: UInt64 { countLock.lock(); defer { countLock.unlock() }; return _bytes }
    var lostBlocks: UInt64 { countLock.lock(); defer { countLock.unlock() }; return _lostBlocks }

    init() {
        bridge = RtlControlBridge(transport: transport)
        transport.blockConsumer = self
        transport.onStateChange = { [weak self] s in self?.stateChanged(s) }
    }

    var tunerName: String { cString(info.tuner_name) }
    var product: String { "\(cString(info.manufacturer)) \(cString(info.product))" }

    private func cString<T>(_ tuple: T) -> String {
        withUnsafeBytes(of: tuple) { String(cString: $0.bindMemory(to: CChar.self).baseAddress!) }
    }

    // MARK: - RingBlockConsumer (file du drain)

    func ringDelivered(_ bytes: UnsafeRawBufferPointer, index: UInt64, captureNs: UInt64) {
        countLock.lock(); _bytes &+= UInt64(bytes.count); countLock.unlock()
        monitor.feed(bytes)
        onBlock?(bytes)
    }

    func ringLost(blocks: UInt64, bytes: UInt64) {
        countLock.lock(); _lostBlocks &+= blocks; countLock.unlock()
    }

    // MARK: - Ouverture (sur `queue`)

    /// Ouvre le user client et initialise le RTL2832U + tuner (librtlsdr).
    func open() throws {
        dispatchPrecondition(condition: .onQueue(queue))
        if isOpen { return }
        let pi = try transport.connect()
        driverBuildId = pi.driverBuildId
        var t = brtl_transport()
        t.ctx = Unmanaged.passUnretained(bridge).toOpaque()
        t.control_transfer = brtlControlEntry
        var out: OpaquePointer?
        var devInfo = brtl_device_info()
        bridge.beginProbe()
        let st = brtl_ctrl_open(&t, &out, &devInfo)
        bridge.finishProbe(tunerAddress: st == BRTL_OK ? devInfo.tuner_i2c_addr : nil)
        info = devInfo
        guard st == BRTL_OK, let c = out else {
            transport.disconnect()
            throw HubError("Ouverture refusée : \(String(cString: brtl_status_name(st))) "
                           + "(tuner « \(cString(devInfo.tuner_name)) »)")
        }
        ctrl = c
        isOpen = true
        biasTee = false
        countLock.lock(); _bytes = 0; _lostBlocks = 0; countLock.unlock()
        // Réglages rtl_tcp par défaut, jusqu'à ce que le client les change.
        _ = apply(.sampleRate(sampleRateHz))
        _ = apply(.frequency(frequencyHz))
        _ = apply(.gainMode(manual: manualGain))
        HubLog.shared.log("open — tuner \(tunerName) « \(product) » driver build \(driverBuildId)")
    }

    var tunerType: UInt32 { ctrl.map { UInt32(brtl_ctrl_tuner_type($0)) } ?? 0 }

    func gainSteps() -> [Int32] {
        guard let c = ctrl else { return [] }
        var list = [Int32](repeating: 0, count: 64)
        let n = brtl_ctrl_gains(c, &list, 64)
        return Array(list.prefix(Int(max(0, n))))
    }

    // MARK: - Commandes rtl_tcp (sur `queue`)

    @discardableResult
    func apply(_ cmd: RtlTcpCommand) -> String {
        dispatchPrecondition(condition: .onQueue(queue))
        guard let c = ctrl else { return "notOpen" }
        var st = BRTL_OK
        switch cmd {
        case .frequency(let f):
            var applied: UInt32 = 0
            st = brtl_ctrl_set_center_frequency(c, f, &applied)
            if st == BRTL_OK || st == BRTL_ERR_PLL_UNLOCKED { frequencyHz = applied }
            pllLocked = brtl_ctrl_pll_locked(c)
        case .sampleRate(let r):
            var applied: UInt32 = 0
            st = brtl_ctrl_set_sample_rate(c, r, &applied)
            if st == BRTL_OK { sampleRateHz = applied }
        case .gainMode(let manual):
            manualGain = manual
            var g: Int32 = 0
            st = brtl_ctrl_set_gain(c, manual ? gainTenthDb : -1, &g)
            if st == BRTL_OK { appliedGainTenthDb = manual ? g : -1 }
        case .gain(let tenth):
            gainTenthDb = tenth
            if manualGain {
                var g: Int32 = 0
                st = brtl_ctrl_set_gain(c, tenth, &g)
                if st == BRTL_OK { appliedGainTenthDb = g }
            }
        case .gainByIndex(let idx):
            let steps = gainSteps()
            if Int(idx) < steps.count { return apply(.gain(tenthDb: steps[Int(idx)])) }
            st = BRTL_ERR_ARGUMENT
        case .frequencyCorrection(let p):
            st = brtl_ctrl_set_freq_correction(c, p)
            if st == BRTL_OK { ppm = p }
        case .rtlAgc(let on):
            st = brtl_ctrl_set_rtl_agc(c, on ? 1 : 0)
            if st == BRTL_OK { rtlAgc = on }
        case .directSampling(let mode):
            st = brtl_ctrl_set_direct_sampling(c, Int32(mode))
            if st == BRTL_OK { directSampling = mode }
        case .offsetTuning(let on):
            st = brtl_ctrl_set_offset_tuning(c, on ? 1 : 0)
        case .biasTee(let on):
            st = brtl_ctrl_set_bias_tee(c, on ? 1 : 0)
            if st == BRTL_OK { biasTee = on }
        case .testMode, .ifGain, .rtlXtal, .tunerXtal, .unknown:
            lastCommandStatus = "\(cmd): ignored"
            return "ignored"
        }
        let s = String(cString: brtl_status_name(st))
        lastCommandStatus = "\(cmd): \(s)"
        HubLog.shared.log(lastCommandStatus)
        return s
    }

    /// Applies a coalesced gain request in ONE tuner call (or none when it
    /// changes nothing): a slider drag never queues hundreds of I²C
    /// sequences behind the stream.
    @discardableResult
    func applyGain(_ r: GainRequest) -> String {
        dispatchPrecondition(condition: .onQueue(queue))
        guard let c = ctrl else { return "notOpen" }
        var newValue = r.tenthDb != nil
        if let i = r.index {
            let steps = gainSteps()
            if Int(i) < steps.count { gainTenthDb = steps[Int(i)]; newValue = true }
        }
        if let t = r.tenthDb { gainTenthDb = t }
        let target = r.manual ?? manualGain
        let needsIo = target != manualGain || (target && newValue && gainTenthDb != appliedGainTenthDb)
        manualGain = target
        guard needsIo else { return "unchanged" }
        var g: Int32 = 0
        let st = brtl_ctrl_set_gain(c, target ? gainTenthDb : -1, &g)
        if st == BRTL_OK { appliedGainTenthDb = target ? g : -1 }
        let s = String(cString: brtl_status_name(st))
        lastCommandStatus = "gain \(target ? "\(gainTenthDb)" : "auto"): \(s)"
        HubLog.shared.log(lastCommandStatus)
        return s
    }

    // MARK: - Flux (sur `queue`)

    func startRx() throws {
        dispatchPrecondition(condition: .onQueue(queue))
        guard isOpen, let c = ctrl else { throw HubError("RTL-SDR non ouvert") }
        if isStreaming { return }
        guard brtl_ctrl_reset_stream(c) == BRTL_OK else { throw HubError("Vidage du FIFO impossible") }
        monitor.reset()
        // Juste après un Stop, le pilote encaisse encore le retour des
        // transferts annulés et refuse un nouveau départ : on réessaie
        // brièvement (1,5 s au plus) au lieu d'échouer.
        var attempt = 0
        while true {
            do {
                try transport.startRx(buffersInFlight: Self.inFlight, bufferBytes: Self.blockBytes)
                break
            } catch {
                attempt += 1
                if attempt >= 30 { HubLog.shared.log("startRx failed after \(attempt) tries: \(error)"); throw error }
                if attempt == 1 {
                    // Une instance précédente de l'app, tuée en pleine
                    // réception, a pu laisser la session du pilote ouverte :
                    // on la ferme avant de réessayer.
                    HubLog.shared.log("startRx refused (\(error)) — stopping a stale session, retrying")
                    try? transport.stopRx()
                }
                Thread.sleep(forTimeInterval: 0.05)
            }
        }
        if attempt > 0 { HubLog.shared.log("startRx ok after \(attempt) retries") }
        isStreaming = true
        HubLog.shared.log("RX started")
    }

    func stopRx() {
        dispatchPrecondition(condition: .onQueue(queue))
        guard isStreaming else { return }
        do { try transport.stopRx() } catch { HubLog.shared.log("stopRx error: \(error)") }
        isStreaming = false
        HubLog.shared.log("RX stopped")
        if biasTee, let c = ctrl { _ = brtl_ctrl_set_bias_tee(c, 0); biasTee = false }
    }

    func close() {
        dispatchPrecondition(condition: .onQueue(queue))
        stopRx()
        if let c = ctrl { brtl_ctrl_close(c); ctrl = nil }
        transport.disconnect()
        isOpen = false
    }

    // MARK: - Retrait (file principale)

    private func stateChanged(_ s: ProbeState) {
        guard s == .terminated else { return }
        queue.async { [self] in
            isStreaming = false
            if let c = ctrl {
                brtl_ctrl_mark_lost(c)    // plus rien ne part sur le bus
                brtl_ctrl_close(c)
                ctrl = nil
            }
            transport.disconnect()
            isOpen = false
            biasTee = false
            HubLog.shared.log("device removed")
            DispatchQueue.main.async { self.onRemoved?() }
        }
    }
}
