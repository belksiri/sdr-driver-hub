//
//  ThroughputMeter.swift — Débit instantané et moyen.
//
//  Fenêtre glissante de 5 s pour l'instantané, cumul depuis le départ pour le
//  moyen. Les deux sont affichés : un débit moyen flatteur peut masquer des
//  décrochages que seul l'instantané révèle, et c'est précisément ce qu'on
//  cherche à voir à 20 Msps.
//

import Foundation

struct ThroughputMeter {
    private struct Sample { let at: TimeInterval; let bytes: UInt64 }

    private var window: [Sample] = []
    private let windowSeconds: TimeInterval
    private var startedAt: TimeInterval?
    private(set) var totalBytes: UInt64 = 0

    /// Le plus bas débit instantané observé depuis le démarrage. C'est lui qui
    /// trahit un décrochage, pas la moyenne.
    private(set) var minimumInstantMBps: Double = .greatestFiniteMagnitude

    init(windowSeconds: TimeInterval = 5.0) {
        self.windowSeconds = windowSeconds
    }

    mutating func reset() {
        window.removeAll(keepingCapacity: true)
        startedAt = nil
        totalBytes = 0
        minimumInstantMBps = .greatestFiniteMagnitude
    }

    mutating func record(bytes: UInt64, at now: TimeInterval = Date().timeIntervalSince1970) {
        if startedAt == nil { startedAt = now }
        totalBytes &+= bytes
        window.append(Sample(at: now, bytes: bytes))
        let cutoff = now - windowSeconds
        while let first = window.first, first.at < cutoff { window.removeFirst() }

        // On n'enregistre un minimum qu'une fois la fenêtre réellement remplie :
        // les toutes premières secondes ne sont pas représentatives.
        if let started = startedAt, now - started > windowSeconds {
            let inst = instantMBps(at: now)
            if inst > 0 { minimumInstantMBps = min(minimumInstantMBps, inst) }
        }
    }

    func instantMBps(at now: TimeInterval = Date().timeIntervalSince1970) -> Double {
        guard let first = window.first, window.count > 1 else { return 0 }
        let span = max(now - first.at, 0.001)
        let bytes = window.reduce(UInt64(0)) { $0 &+ $1.bytes }
        return Double(bytes) / span / 1_000_000.0
    }

    func averageMBps(at now: TimeInterval = Date().timeIntervalSince1970) -> Double {
        guard let started = startedAt else { return 0 }
        let span = max(now - started, 0.001)
        return Double(totalBytes) / span / 1_000_000.0
    }

    var elapsed: TimeInterval {
        guard let started = startedAt else { return 0 }
        return Date().timeIntervalSince1970 - started
    }

    /// Écart au débit théorique, en pourcentage. Le débit théorique d'un HackRF
    /// en IQ 8 bits est de 2 octets par échantillon complexe.
    func deviationPercent(sampleRateHz: UInt32,
                          at now: TimeInterval = Date().timeIntervalSince1970) -> Double {
        let theoretical = Double(sampleRateHz) * 2.0 / 1_000_000.0
        guard theoretical > 0 else { return 0 }
        return (theoretical - averageMBps(at: now)) / theoretical * 100.0
    }
}
