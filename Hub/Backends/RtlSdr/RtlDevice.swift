//
//  RtlDevice.swift — Le RTL-SDR, par le pilote DriverKit embarqué (SDR Driver Hub).
//
//  Protocole client v2 (Driver/brtl_client_protocol.h) :
//    contrôle : commandes de haut niveau ── user client ── pilote (librtlsdr
//               compilée DANS le dext, derrière la liste blanche)
//    données  : dext ── ring partagé ── RingDrain ── ringDelivered (u8 bruts)
//  L'application n'embarque plus librtlsdr et n'émet plus aucune requête USB.
//
//  Le récepteur a UN propriétaire à la fois. Spectrum Analyzer One l'ouvre
//  directement ; SDR Driver Hub ne le prend (`open`) que pour une
//  vérification ou un client rtl_tcp de développement, et le rend (`close`)
//  aussitôt. Les lectures (`readInfo`, compteurs du ring) restent possibles
//  pendant que Spectrum Analyzer One reçoit.
//
//  Toutes les opérations passent par `queue`.
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

    let transport = DriverKitUsbTransport(serviceUserClass: RtlDevice.serviceName)
    let queue = DispatchQueue(label: "hub.rtl.device")
    let monitor = IqContentMonitor()

    /// Destinataire des blocs bruts (le serveur). Appelé sur la file du drain.
    var onBlock: ((UnsafeRawBufferPointer) -> Void)?
    /// Le périphérique a disparu (retrait USB). Appelé sur la file principale.
    var onRemoved: (() -> Void)?
    /// Tout changement d'état de la liaison au pilote. File principale.
    var onLinkStateChange: ((ProbeState) -> Void)?

    private(set) var info = BRTLDeviceInfo()
    private(set) var hasInfo = false
    private(set) var driverBuildId: UInt32 = 0
    private(set) var connected = false
    /// SDR Driver Hub possède le récepteur.
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
    private(set) var lastCommandStatus = "—"

    // Compteurs.
    private let countLock = NSLock()
    private var _bytes: UInt64 = 0
    private var _lostBlocks: UInt64 = 0
    var deliveredBytes: UInt64 { countLock.lock(); defer { countLock.unlock() }; return _bytes }
    var lostBlocks: UInt64 { countLock.lock(); defer { countLock.unlock() }; return _lostBlocks }

    init() {
        transport.blockConsumer = self
        transport.onStateChange = { [weak self] s in self?.stateChanged(s) }
    }

    var tunerName: String { cString(info.tunerName) }
    var product: String { "\(cString(info.manufacturer)) \(cString(info.product))" }
    /// Un AUTRE client (Spectrum Analyzer One) possède le récepteur.
    var ownedByAnotherClient: Bool { hasInfo && info.ownerPresent != 0 && info.callerIsOwner == 0 }

    private func cString<T>(_ tuple: T) -> String {
        withUnsafeBytes(of: tuple) { raw in
            let bytes = raw.bindMemory(to: UInt8.self)
            let end = bytes.firstIndex(of: 0) ?? bytes.count
            return String(decoding: bytes[..<end], as: UTF8.self)
        }
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

    // MARK: - Lecture libre (sur `queue`)

    /// Ouvre le user client (sans prendre le récepteur) et lit l'identité.
    func connectIfNeeded() throws {
        dispatchPrecondition(condition: .onQueue(queue))
        if connected { return }
        let pi = try transport.connect()
        driverBuildId = pi.driverBuildId
        connected = true
    }

    /// Identité, réglages et compteurs du pilote. Possible pendant que
    /// Spectrum Analyzer One reçoit : aucune action sur la radio.
    @discardableResult
    func readInfo() -> BRTLDeviceInfo? {
        dispatchPrecondition(condition: .onQueue(queue))
        do {
            try connectIfNeeded()
            let i: BRTLDeviceInfo = try transport.call(Int(kBRTLSelDeviceInfo))
            info = i
            hasInfo = true
            return i
        } catch {
            return nil
        }
    }

    // MARK: - Prise du récepteur (sur `queue`)

    /// Prend le récepteur. Refusé (`TransportError.busy`) tant que Spectrum
    /// Analyzer One le possède : SDR Driver Hub ne le lui retire jamais.
    func open() throws {
        dispatchPrecondition(condition: .onQueue(queue))
        if isOpen { return }
        try connectIfNeeded()
        let r: BRTLClientResult = try transport.call(Int(kBRTLSelAcquire))
        switch BRTLClientStatus(UInt32(bitPattern: r.status)) {
        case kBRTLClientOK:
            break
        case kBRTLClientBusy:
            throw TransportError.busy
        default:
            throw TransportError.client(status: r.status, controller: r.controllerStatus)
        }
        isOpen = true
        biasTee = false
        countLock.lock(); _bytes = 0; _lostBlocks = 0; countLock.unlock()
        readInfo()
        // Réglages rtl_tcp par défaut, jusqu'à ce que le client les change.
        _ = apply(.sampleRate(sampleRateHz))
        _ = apply(.frequency(frequencyHz))
        _ = apply(.gainMode(manual: manualGain))
        HubLog.shared.log("open — tuner \(tunerName) « \(product) » driver build \(driverBuildId)")
    }

    var tunerType: UInt32 { hasInfo ? info.tunerType : 0 }

    func gainSteps() -> [Int32] {
        guard hasInfo else { return [] }
        let n = Int(min(info.gainCount, BRTL_MAX_GAINS))
        return withUnsafeBytes(of: info.gainsTenthDb) { Array($0.bindMemory(to: Int32.self).prefix(n)) }
    }

    // MARK: - Commandes (sur `queue`)

    /// Une commande au pilote. Renvoie le résultat, ou nil si l'appel a échoué.
    private func command(_ op: BRTLOpcode, _ value: Int64) -> BRTLClientResult? {
        var c = BRTLCommand()
        c.opcode = op.rawValue
        c.value = value
        return try? transport.call(Int(kBRTLSelCommand), c)
    }

    private static func name(_ r: BRTLClientResult?) -> String {
        guard let r else { return "callFailed" }
        return String(cString: brtl_client_status_name(BRTLClientStatus(UInt32(bitPattern: r.status))))
    }

    private static func ok(_ r: BRTLClientResult?) -> Bool { r?.status == Int32(kBRTLClientOK.rawValue) }

    @discardableResult
    func apply(_ cmd: RtlCommand) -> String {
        dispatchPrecondition(condition: .onQueue(queue))
        guard isOpen else { return "notOpen" }
        let r: BRTLClientResult?
        switch cmd {
        case .frequency(let f):
            r = command(kBRTLOpSetFrequency, Int64(f))
            if Self.ok(r), let a = r?.applied { frequencyHz = UInt32(truncatingIfNeeded: a) }
        case .sampleRate(let rate):
            r = command(kBRTLOpSetSampleRate, Int64(rate))
            if Self.ok(r), let a = r?.applied { sampleRateHz = UInt32(truncatingIfNeeded: a) }
        case .gainMode(let manual):
            manualGain = manual
            if manual {
                r = command(kBRTLOpSetGain, Int64(gainTenthDb))
                if Self.ok(r), let a = r?.applied { appliedGainTenthDb = Int32(truncatingIfNeeded: a) }
            } else {
                r = command(kBRTLOpSetGainMode, 0)
                if Self.ok(r) { appliedGainTenthDb = -1 }
            }
        case .gain(let tenth):
            gainTenthDb = tenth
            guard manualGain else { lastCommandStatus = "\(cmd): stored"; return "stored" }
            r = command(kBRTLOpSetGain, Int64(tenth))
            if Self.ok(r), let a = r?.applied { appliedGainTenthDb = Int32(truncatingIfNeeded: a) }
        case .gainByIndex(let idx):
            let steps = gainSteps()
            guard Int(idx) < steps.count else { lastCommandStatus = "\(cmd): badArgument"; return "badArgument" }
            return apply(.gain(tenthDb: steps[Int(idx)]))
        case .frequencyCorrection(let p):
            r = command(kBRTLOpSetFreqCorrection, Int64(p))
            if Self.ok(r) { ppm = p }
        case .rtlAgc(let on):
            r = command(kBRTLOpSetRtlAgc, on ? 1 : 0)
            if Self.ok(r) { rtlAgc = on }
        case .directSampling(let mode):
            r = command(kBRTLOpSetDirectSampling, Int64(mode))
            if Self.ok(r) { directSampling = mode }
        case .offsetTuning(let on):
            r = command(kBRTLOpSetOffsetTuning, on ? 1 : 0)
        case .biasTee(let on):
            r = command(kBRTLOpSetBiasTee, on ? 1 : 0)
            if Self.ok(r) { biasTee = on }
        case .testMode, .ifGain, .rtlXtal, .tunerXtal, .unknown:
            lastCommandStatus = "\(cmd): ignored"
            return "ignored"
        }
        let s = Self.name(r)
        lastCommandStatus = "\(cmd): \(s)"
        HubLog.shared.log(lastCommandStatus)
        return s
    }

    /// Applies a coalesced gain request in ONE driver command (or none when it
    /// changes nothing): a slider drag never queues hundreds of I²C
    /// sequences behind the stream.
    @discardableResult
    func applyGain(_ req: GainRequest) -> String {
        dispatchPrecondition(condition: .onQueue(queue))
        guard isOpen else { return "notOpen" }
        var newValue = req.tenthDb != nil
        if let i = req.index {
            let steps = gainSteps()
            if Int(i) < steps.count { gainTenthDb = steps[Int(i)]; newValue = true }
        }
        if let t = req.tenthDb { gainTenthDb = t }
        let target = req.manual ?? manualGain
        let needsIo = target != manualGain || (target && newValue && gainTenthDb != appliedGainTenthDb)
        manualGain = target
        guard needsIo else { return "unchanged" }
        let r = target ? command(kBRTLOpSetGain, Int64(gainTenthDb)) : command(kBRTLOpSetGainMode, 0)
        if Self.ok(r) {
            appliedGainTenthDb = target ? Int32(truncatingIfNeeded: r?.applied ?? 0) : -1
        }
        let s = Self.name(r)
        lastCommandStatus = "gain \(target ? "\(gainTenthDb)" : "auto"): \(s)"
        HubLog.shared.log(lastCommandStatus)
        return s
    }

    // MARK: - Flux (sur `queue`)

    func startRx() throws {
        dispatchPrecondition(condition: .onQueue(queue))
        guard isOpen else { throw HubError("RTL-SDR non ouvert") }
        if isStreaming { return }
        monitor.reset()
        // Le pilote vide lui-même le FIFO avant les premiers transferts.
        // Juste après un Stop, il encaisse encore le retour des transferts
        // annulés : on réessaie brièvement (1,5 s au plus).
        var attempt = 0
        while true {
            do {
                try transport.startRx(buffersInFlight: Self.inFlight, bufferBytes: Self.blockBytes)
                break
            } catch TransportError.notOwner {
                throw TransportError.notOwner
            } catch {
                attempt += 1
                if attempt >= 30 { HubLog.shared.log("startRx failed after \(attempt) tries: \(error)"); throw error }
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
        if biasTee { _ = apply(.biasTee(false)) }
    }

    /// Rend le récepteur (le pilote coupe le bias-tee) et ferme le user client.
    func close() {
        dispatchPrecondition(condition: .onQueue(queue))
        stopRx()
        if isOpen {
            let r: BRTLClientResult? = try? transport.call(Int(kBRTLSelRelease))
            HubLog.shared.log("release: \(Self.name(r))")
        }
        isOpen = false
        biasTee = false
        transport.disconnect()
        connected = false
    }

    // MARK: - Retrait (file principale)

    private func stateChanged(_ s: ProbeState) {
        onLinkStateChange?(s)
        guard s == .terminated else { return }
        queue.async { [self] in
            isStreaming = false
            transport.disconnect()
            connected = false
            isOpen = false
            hasInfo = false
            biasTee = false
            HubLog.shared.log("device removed")
            DispatchQueue.main.async { self.onRemoved?() }
        }
    }
}
