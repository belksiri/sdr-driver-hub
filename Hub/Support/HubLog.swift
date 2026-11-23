//
//  HubLog.swift — Journal d'événements du compagnon.
//
//  Par défaut : en MÉMOIRE seulement (les dernières lignes, pour le diagnostic
//  à l'écran). Aucun fichier, aucune sortie système en Release.
//
//  Builds de développement SEULEMENT (HUB_DEV_RTLTCP, jamais dans un build
//  App Store, où il ne serait de toute façon pas atteignable : déposer un
//  fichier dans le conteneur demande un build signé en développement) —
//  diagnostic opt-in : un fichier marqueur `Documents/hub_debug.enable`
//  (déposé par le développeur, `xcrun devicectl device copy to …`) active au
//  lancement suivant l'écriture de `Documents/hub_events.log`, borné à 1 Mo.
//  Le retirer désactive l'écriture ; le fichier journal est alors supprimé.
//  `Documents/hub_debug.disable` (déposé de la même façon, puisque devicectl
//  ne sait pas supprimer) l'emporte : marqueurs et journal sont effacés.
//

import Foundation

final class HubLog {
    static let shared = HubLog()
    #if HUB_DEV_RTLTCP
    static let markerName = "hub_debug.enable"
    static let disableMarkerName = "hub_debug.disable"
    static let fileName = "hub_events.log"
    #endif

    private let lock = NSLock()
    private var handle: FileHandle?
    private var size: UInt64 = 0
    private var recent: [String] = []
    private(set) var writesFile = false

    private init() {
        #if HUB_DEV_RTLTCP
        guard let docs = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first else { return }
        let marker = docs.appendingPathComponent(Self.markerName)
        let url = docs.appendingPathComponent(Self.fileName)
        let disable = docs.appendingPathComponent(Self.disableMarkerName)
        if FileManager.default.fileExists(atPath: disable.path) {
            for f in [marker, url, disable] { try? FileManager.default.removeItem(at: f) }
            return
        }
        guard FileManager.default.fileExists(atPath: marker.path) else {
            // Diagnostic désactivé : ne rien laisser derrière soi.
            try? FileManager.default.removeItem(at: url)
            return
        }
        if !FileManager.default.fileExists(atPath: url.path) {
            FileManager.default.createFile(atPath: url.path, contents: nil)
        }
        handle = try? FileHandle(forWritingTo: url)
        size = (try? handle?.seekToEnd()) ?? 0
        writesFile = handle != nil
        #endif
    }

    func log(_ text: String) {
        #if DEBUG
        NSLog("HUB: %@", text)
        #endif
        let f = ISO8601DateFormatter.string(from: Date(), timeZone: .current,
                                            formatOptions: [.withFullTime, .withFractionalSeconds])
        lock.lock(); defer { lock.unlock() }
        recent.append("\(f.prefix(12)) \(text)")
        if recent.count > 20 { recent.removeFirst(recent.count - 20) }
        guard let h = handle, let d = "\(f) \(text)\n".data(using: .utf8),
              size + UInt64(d.count) < 1 << 20 else { return }
        h.write(d)
        size += UInt64(d.count)
    }

    var lines: [String] { lock.lock(); defer { lock.unlock() }; return recent }
}
