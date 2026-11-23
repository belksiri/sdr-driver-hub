//
//  HubApp.swift — SDR Driver Hub : pilotes SDR et service IQ local pour
//  Spectrum Analyzer One. Aucune fonction d'analyse ici.
//

import SwiftUI

@main
struct HubApp: App {
    @StateObject private var model = HubModel()
    var body: some Scene {
        WindowGroup {
            HubView(model: model)
                .onOpenURL { _ in model.tick() }    // sdrdriverhub://open
        }
    }
}

struct HubView: View {
    @ObservedObject var model: HubModel

    var body: some View {
        let b = model.backend
        let stats = b.statistics()
        NavigationStack {
            List {
                Section { ReadinessHeader(readiness: model.readiness) }

                Section("Hardware") {
                    LabeledContent("Receiver", value: b.displayName)
                    LabeledContent("Device", value: b.deviceInfo?.product
                                   ?? (b.availability.deviceDetected ? String(localized: "Connected")
                                                                     : String(localized: "Not detected")))
                    if let tuner = b.deviceInfo?.detail, !tuner.isEmpty {
                        LabeledContent("Tuner", value: tuner)
                    }
                    LabeledContent("Driver") {
                        Text(Self.driverState(b.availability)).foregroundStyle(Self.tint(b.availability))
                    }
                }

                Section {
                    LabeledContent("Spectrum Analyzer One") {
                        Text(stats.clientConnected ? "Receiving" : "Not receiving")
                            .foregroundStyle(stats.clientConnected ? .green : .secondary)
                    }
                } footer: {
                    Text("Spectrum Analyzer One opens the driver directly: SDR Driver Hub does not need to stay open while you listen.")
                }

                #if HUB_DEV_RTLTCP
                // Textes dans la table « Development », embarquée seulement dans
                // les builds de développement (jamais dans un build App Store).
                Section {
                    Toggle(isOn: Binding(get: { model.serviceOn },
                                         set: { model.serviceOn = $0 })) {
                        Text("rtl_tcp service", tableName: "Development")
                    }
                    LabeledContent {
                        Text(b.isServiceRunning ? "Running" : "Stopped", tableName: "Development")
                            .foregroundStyle(b.isServiceRunning ? .green : .secondary)
                    } label: {
                        Text("Status", tableName: "Development")
                    }
                    if let e = b.lastError {
                        Text(LocalizedStringKey(e.message), tableName: "Development")
                            .foregroundStyle(.red)
                    }
                } header: {
                    Text("Development", tableName: "Development")
                } footer: {
                    Text("Development builds only: rtl_tcp on 127.0.0.1:1234, kept alive in the background. Never part of an App Store build.",
                         tableName: "Development")
                }
                #endif

                Section("Reception") {
                    // Réglages réels seulement une fois l'appareil ouvert
                    // (à la connexion d'un client ou par la vérification).
                    if b.availability.deviceDetected, b.deviceInfo != nil {
                        LabeledContent("Frequency", value: Self.mhz(b.configuration.frequencyHz))
                        LabeledContent("Sample rate", value: Self.msps(b.configuration.sampleRateHz))
                        LabeledContent("Gain", value: Self.gain(b.configuration.gain))
                    }
                    LabeledContent("Actual throughput", value: Self.throughput(model.bytesPerSecond))
                    LabeledContent("USB errors", value: "\(stats.usbErrors)")
                    LabeledContent("Lost blocks", value: "\(stats.totalLostBlocks)")
                    LabeledContent("IQ content") {
                        switch stats.iqContentLive {
                        case .some(true): Text("Verified").foregroundStyle(.green)
                        case .some(false): Text("Suspect").foregroundStyle(.red)
                        case .none: Text("—").foregroundStyle(.secondary)
                        }
                    }
                }

                Section {
                    Button(model.testing ? "Checking…" : "Check reception") { model.runSelfTest() }
                        .disabled(model.testing || stats.clientConnected || !b.availability.deviceDetected)
                    if let r = model.selfTest {
                        Label(r.summary, systemImage: r.passed ? "checkmark.circle.fill" : "xmark.octagon.fill")
                            .foregroundStyle(r.passed ? .green : .red)
                            .font(.footnote)
                    }
                } footer: {
                    Text("Receives for 1.5 s at 88.8 MHz and checks the real IQ content. Available when Spectrum Analyzer One is not receiving.")
                }

                Section {
                    NavigationLink("Open-source licenses") { LicensesView() }
                    NavigationLink("About") { AboutView(model: model) }
                }
            }
            .navigationTitle("SDR Driver Hub")
        }
    }

    // MARK: Formatage

    static func driverState(_ a: BackendAvailability) -> LocalizedStringKey {
        switch a {
        case .driverNotEnabled: return "Not enabled"
        case .waitingForDevice: return "Waiting for device"
        case .ready: return "Ready"
        case .streaming: return "Receiving"
        case .failed: return "Device removed"
        }
    }

    static func tint(_ a: BackendAvailability) -> Color {
        switch a {
        case .ready, .streaming: return .green
        case .waitingForDevice: return .secondary
        case .driverNotEnabled, .failed: return .orange
        }
    }

    static func mhz(_ hz: UInt32) -> String { String(format: "%.3f MHz", Double(hz) / 1e6) }
    static func msps(_ hz: UInt32) -> String { String(format: "%.3f MS/s", Double(hz) / 1e6) }
    static func throughput(_ bps: Double) -> String { String(format: "%.2f MB/s", bps / 1e6) }
    static func gain(_ g: GainControl) -> String {
        switch g {
        case .automatic: return String(localized: "Automatic")
        case .manual(let t): return String(format: "%.1f dB", Double(t) / 10)
        }
    }
}

/// En tête : Spectrum Analyzer One peut-il se connecter, et sinon quoi faire.
struct ReadinessHeader: View {
    let readiness: SpectrumReadiness

    var body: some View {
        switch readiness {
        case .connected:
            Label("Spectrum Analyzer One is receiving.", systemImage: "checkmark.circle.fill")
                .foregroundStyle(.green).font(.headline)
        case .canConnect:
            Label("Ready: Spectrum Analyzer One can connect.", systemImage: "checkmark.circle")
                .foregroundStyle(.green).font(.headline)
        case .notReady(let e):
            VStack(alignment: .leading, spacing: 6) {
                Label(LocalizedStringKey(e.message), systemImage: "exclamationmark.triangle.fill")
                    .foregroundStyle(.orange).font(.headline)
                if let r = e.recovery {
                    Text(LocalizedStringKey(r)).font(.subheadline).foregroundStyle(.secondary)
                }
            }
        }
    }
}

struct AboutView: View {
    @ObservedObject var model: HubModel

    var body: some View {
        List {
            Section {
                LabeledContent("Version", value: HubModel.appVersion)
                if let d = model.backend.deviceInfo?.driverVersion {
                    LabeledContent("Loaded driver build", value: d)
                }
            }
            Section("Supported hardware") {
                ForEach(model.registry.backends, id: \.id) { b in
                    LabeledContent(b.displayName, value: b.backendVersion)
                }
            }
            Section {
                Text("SDR Driver Hub installs the USB drivers of your SDR receivers. Spectrum Analyzer One opens them directly on this iPad, so SDR Driver Hub does not need to stay open while you listen.")
                    .font(.footnote).foregroundStyle(.secondary)
            }
        }
        .navigationTitle("About")
    }
}
