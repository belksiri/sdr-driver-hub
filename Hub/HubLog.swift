//
//  HubLog.swift — Journal d'événements du compagnon (Documents/hub_events.log,
//  borné à 1 Mo), lisible depuis le Mac par devicectl, et ses dernières lignes
//  à l'écran.
//

import Foundation

final class HubLog {
    static let shared = HubLog()
    private let lock = NSLock()
    private var handle: FileHandle?
    private var size: UInt64 = 0
    private(set) var recent: [String] = []

    private init() {
        guard let docs = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first else { return }
        let url = docs.appendingPathComponent("hub_events.log")
        if !FileManager.default.fileExists(atPath: url.path) {
            FileManager.default.createFile(atPath: url.path, contents: nil)
        }
        handle = try? FileHandle(forWritingTo: url)
        size = (try? handle?.seekToEnd()) ?? 0
    }

    func log(_ text: String) {
        NSLog("HUB: %@", text)
        let f = ISO8601DateFormatter.string(from: Date(), timeZone: .current,
                                            formatOptions: [.withFullTime, .withFractionalSeconds])
        let line = "\(f) \(text)\n"
        lock.lock(); defer { lock.unlock() }
        recent.append("\(f.prefix(12)) \(text)")
        if recent.count > 8 { recent.removeFirst(recent.count - 8) }
        guard let h = handle, let d = line.data(using: .utf8), size + UInt64(d.count) < 1 << 20 else { return }
        h.write(d)
        size += UInt64(d.count)
    }

    var lines: [String] { lock.lock(); defer { lock.unlock() }; return recent }
}
