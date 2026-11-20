//
//  HubModel.swift — État affiché par SDR Driver Hub.
//

import Foundation
import SwiftUI

@MainActor
final class HubModel: ObservableObject {
    let device = RtlDevice()
    lazy var server = RtlTcpServer(device: device)
    private let keepAlive = BackgroundKeepAlive()
    private var timer: Timer?
    private var lastBytes: UInt64 = 0
    private var lastTick = Date()

    @Published var driverState = "—"
    @Published var deviceLine = "—"
    @Published var tuner = "—"
    @Published var frequency = "—"
    @Published var sampleRate = "—"
    @Published var gain = "—"
    @Published var gainCommands = "—"
    @Published var iqRate = "—"
    @Published var totalBytes = "—"
    @Published var usbErrors = "—"
    @Published var ringLoss = "—"
    @Published var sendLoss = "—"
    @Published var content = "—"
    @Published var contentLive = false
    @Published var client: String? = nil
    @Published var serverOn = false
    @Published var serverError: String? = nil
    @Published var testResult: String? = nil
    @Published var testing = false
    @Published var events: [String] = []

    init() {
        server.onClientChanged = { [weak self] c in self?.client = c }
        server.onListening = { [weak self] on, err in
            self?.serverOn = on
            self?.serverError = err
        }
        device.onRemoved = { [weak self] in self?.server.deviceRemoved() }
        startServer()               // le service démarre avec l'app
        timer = Timer.scheduledTimer(withTimeInterval: 1, repeats: true) { [weak self] _ in
            Task { @MainActor in self?.refresh() }
        }
    }

    func startServer() {
        keepAlive.start()
        server.start()
    }

    func stopServer() {
        server.stop()
        keepAlive.stop()
    }

    private static func label(_ s: ProbeState) -> String {
        switch s {
        case .driverNotInstalled: return "Pilote non installé"
        case .driverNotEnabled: return "Pilote non activé (Réglages › Apps › SDR Driver Hub)"
        case .waitingForDevice: return "Pilote actif — en attente du RTL-SDR"
        case .deviceAttached: return "RTL-SDR détecté"
        case .receiving: return "Réception en cours"
        case .terminated: return "RTL-SDR retiré"
        }
    }

    func refresh() {
        let d = device
        driverState = Self.label(d.transport.state)
        events = HubLog.shared.lines
        let now = Date()
        let bytes = d.deliveredBytes
        let dt = now.timeIntervalSince(lastTick)
        let rate = dt > 0 && bytes >= lastBytes ? Double(bytes - lastBytes) / dt : 0
        lastBytes = bytes; lastTick = now
        d.queue.async { [weak self] in
            let open = d.isOpen
            let line = open ? "\(d.product) · build pilote \(d.driverBuildId)" : "—"
            let tun = open ? d.tunerName : "—"
            let f = String(format: "%.3f MHz", Double(d.frequencyHz) / 1e6)
            let r = String(format: "%.3f MS/s", Double(d.sampleRateHz) / 1e6)
            let g = d.appliedGainTenthDb < 0 ? "Automatique" : String(format: "%.1f dB", Double(d.appliedGainTenthDb) / 10)
            let usb = d.bridge.usbErrors + d.bridge.tunerCommandFailures
            let ring = (try? d.transport.ringStats())
            let lost = d.lostBlocks + (ring?.overflowBlocks ?? 0)
            let ringUsb = ring?.usbErrors ?? 0
            let s = d.monitor.snapshot
            Task { @MainActor [weak self] in
                guard let self else { return }
                self.deviceLine = line
                self.tuner = tun
                self.frequency = open ? f : "—"
                self.sampleRate = open ? r : "—"
                self.gain = open ? g : "—"
                self.iqRate = String(format: "%.3f Mo/s (attendu %.3f)", rate / 1e6, Double(d.sampleRateHz) * 2 / 1e6)
                self.totalBytes = "\(bytes)"
                self.usbErrors = "contrôle \(usb) · ring \(ringUsb) · sondes tuner absentes \(d.bridge.tunerProbeMisses) (normal)"
                self.ringLoss = "\(lost) blocs"
                self.sendLoss = "\(self.server.sendQueue.droppedBlocks) blocs"
                self.gainCommands = "\(self.server.gainCommandsReceived) reçues · \(self.server.gainCommandsApplied) appliquées"
                self.content = s.blocks == 0 ? "—" : String(
                    format: "%@ · I %.1f±%.1f Q %.1f±%.1f · répétés %llu · périodiques %llu · écrêtage %.2f %%",
                    s.looksLive ? "IQ vivants" : "IQ SUSPECTS", s.meanI, s.stdI, s.meanQ, s.stdQ,
                    s.repeatedBlocks, s.periodicBlocks, s.clippingPercent)
                self.contentLive = s.looksLive
            }
        }
    }

    /// Test local : 88,8 MHz, 2,56 MS/s, 29,7 dB, 1,5 s. Vert seulement si le
    /// débit est le bon ET le contenu vivant (jamais sur le débit seul).
    func runTest() {
        guard client == nil else { testResult = "Un client est connecté : test impossible pendant le service."; return }
        testing = true
        testResult = nil
        let d = device
        d.queue.async {
            var result: String
            do {
                try d.open()
                d.apply(.sampleRate(2_560_000))
                d.apply(.frequency(88_800_000))
                d.apply(.gainMode(manual: true))
                d.apply(.gain(tenthDb: 297))
                let b0 = d.deliveredBytes
                let t0 = Date()
                try d.startRx()
                Thread.sleep(forTimeInterval: 1.5)
                let bytes = d.deliveredBytes - b0
                let secs = Date().timeIntervalSince(t0)
                d.stopRx()
                let s = d.monitor.snapshot
                let mbps = Double(bytes) / secs / 1e6
                let rateOk = abs(mbps - 5.12) / 5.12 < 0.10
                let ok = rateOk && s.looksLive && d.lostBlocks == 0
                result = String(format: "%@ — %.3f Mo/s, %llu blocs, répétés %llu, périodiques %llu, I %.1f±%.1f, Q %.1f±%.1f, pertes %llu, PLL %d",
                                ok ? "VERT" : "ROUGE", mbps, s.blocks, s.repeatedBlocks, s.periodicBlocks,
                                s.meanI, s.stdI, s.meanQ, s.stdQ, d.lostBlocks, d.pllLocked)
            } catch {
                result = "ROUGE — \(error)"
            }
            DispatchQueue.main.async {
                self.testResult = result
                self.testing = false
            }
        }
    }
}
