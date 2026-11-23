//
//  HubModel.swift — État affiché par SDR Driver Hub.
//
//  Ne connaît que le contrat `SdrBackend` : les backends présents sont
//  enregistrés ici, le reste de l'application en est indépendant.
//

import Foundation
import SwiftUI

@MainActor
final class HubModel: ObservableObject {
    let registry = BackendRegistry()
    #if HUB_DEV_RTLTCP
    private let keepAlive = BackgroundKeepAlive()
    #endif
    private var timer: Timer?
    private var sampler = ThroughputSampler()

    /// Service rtl_tcp de développement : arrêté par défaut. Le chemin normal
    /// est l'accès direct de Spectrum Analyzer One au pilote.
    @Published private(set) var serviceRequested = false
    @Published private(set) var bytesPerSecond: Double = 0
    @Published var selfTest: SelfTestResult?
    @Published var testing = false

    static let appVersion: String = {
        let info = Bundle.main.infoDictionary ?? [:]
        let v = info["CFBundleShortVersionString"] as? String ?? "?"
        let b = info["CFBundleVersion"] as? String ?? "?"
        return "\(v) (\(b))"
    }()

    init() {
        _ = HubLog.shared      // applique tout de suite le choix journal / pas de journal
        registry.register(RtlSdrBackend(backendVersion: Self.appVersion))
        for b in registry.backends {
            b.onChange = { [weak self] in self?.objectWillChange.send() }
        }
        timer = Timer.scheduledTimer(withTimeInterval: 1, repeats: true) { [weak self] _ in
            Task { @MainActor in self?.tick() }
        }
        tick()
    }

    /// Le backend mis en avant (celui qui reçoit, sinon celui qui est branché).
    var backend: SdrBackend { registry.primary! }

    var readiness: SpectrumReadiness {
        let b = backend
        return .evaluate(availability: b.availability,
                         clientConnected: b.statistics().clientConnected, deviceName: b.displayName)
    }

    var serviceOn: Bool {
        get { serviceRequested }
        set { newValue ? startService() : stopService() }
    }

    func startService() {
        #if HUB_DEV_RTLTCP
        serviceRequested = true
        keepAlive.start()
        registry.backends.forEach { $0.startService() }
        #endif
    }

    func stopService() {
        #if HUB_DEV_RTLTCP
        serviceRequested = false
        registry.backends.forEach { $0.stopService() }
        keepAlive.stop()
        #endif
    }

    func tick() {
        registry.backends.forEach { $0.refresh() }
        sampler.record(totalBytes: backend.statistics().deliveredBytes,
                       at: Date().timeIntervalSinceReferenceDate)
        bytesPerSecond = sampler.bytesPerSecond
    }

    func runSelfTest() {
        testing = true
        selfTest = nil
        backend.runSelfTest { [weak self] r in
            self?.selfTest = r
            self?.testing = false
        }
    }
}
