//
//  HubApp.swift — SDR Driver Hub : pilote RTL-SDR + serveur rtl_tcp local.
//  Aucune fonction de Spectrum ici : diagnostic seulement.
//

import SwiftUI

@main
struct HubApp: App {
    @StateObject private var model = HubModel()
    var body: some Scene {
        WindowGroup {
            HubView(model: model)
                .onOpenURL { _ in model.refresh() }    // sdrdriverhub://open
        }
    }
}

struct HubView: View {
    @ObservedObject var model: HubModel

    var body: some View {
        NavigationStack {
            Form {
                Section("Pilote et périphérique") {
                    row("État du pilote", model.driverState)
                    row("Périphérique", model.deviceLine)
                    row("Tuner", model.tuner)
                }
                Section("Réglages appliqués") {
                    row("Fréquence", model.frequency)
                    row("Débit", model.sampleRate)
                    row("Gain", model.gain)
                    row("Commandes de gain", model.gainCommands)
                }
                Section("Flux IQ") {
                    row("Débit IQ", model.iqRate)
                    row("Octets reçus", model.totalBytes)
                    row("Contenu", model.content, tint: model.content == "—" ? nil : (model.contentLive ? .green : .red))
                    row("Erreurs USB", model.usbErrors)
                    row("Pertes du ring", model.ringLoss)
                    row("Pertes d'envoi (client lent)", model.sendLoss)
                }
                Section("Service rtl_tcp — 127.0.0.1:\(RtlTcpServer.port)") {
                    row("Serveur", model.serverOn ? "Actif" : (model.serverError.map { "Arrêté — \($0)" } ?? "Arrêté"),
                        tint: model.serverOn ? .green : .orange)
                    row("Client connecté", model.client ?? "aucun")
                    HStack {
                        Button(model.serverOn ? "Stop Server" : "Start Server") {
                            model.serverOn ? model.stopServer() : model.startServer()
                        }
                        Spacer()
                        Button(model.testing ? "Test…" : "Test") { model.runTest() }
                            .disabled(model.testing)
                    }
                    if let t = model.testResult {
                        Text(t).font(.footnote)
                            .foregroundStyle(t.hasPrefix("VERT") ? .green : .red)
                    }
                }
                Section("Derniers événements") {
                    ForEach(Array(model.events.enumerated()), id: \.offset) { _, e in
                        Text(e).font(.caption.monospaced())
                    }
                }
                Section {
                    Text("EXPERIMENTAL — DEVELOPMENT ONLY. Pilote RTL-SDR (librtlsdr, GPL-2.0-or-later) pour un usage local de développement.")
                        .font(.footnote).foregroundStyle(.secondary)
                }
            }
            .navigationTitle("SDR Driver Hub")
        }
    }

    private func row(_ k: String, _ v: String, tint: Color? = nil) -> some View {
        HStack(alignment: .top) {
            Text(k)
            Spacer()
            Text(v).multilineTextAlignment(.trailing).foregroundStyle(tint ?? .secondary)
                .textSelection(.enabled)
        }
    }
}
