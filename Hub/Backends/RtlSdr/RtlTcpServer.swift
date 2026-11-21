//
//  RtlTcpServer.swift — Serveur rtl_tcp local (SDR Driver Hub).
//
//  Un client à la fois (une nouvelle connexion remplace la précédente).
//  À la connexion : ouverture du RTL-SDR si besoin, en-tête « RTL0 » (vrai
//  tuner, vrai nombre de paliers), réglages par défaut rtl_tcp, démarrage du
//  flux. Les blocs bruts du ring partent dans l'ordre par une file bornée : un
//  client lent ne bloque jamais le drain, la perte est comptée. À la
//  déconnexion : arrêt du flux et RESTITUTION du récepteur, pour que
//  Spectrum Analyzer One puisse l'ouvrir directement.
//
//  DÉVELOPPEMENT SEULEMENT : compilé sous HUB_DEV_RTLTCP, jamais dans un
//  build App Store. Le chemin normal est l'accès direct au pilote.
//

#if HUB_DEV_RTLTCP
import Foundation
import Network

final class RtlTcpServer {

    static let port: UInt16 = 1234

    private let device: RtlDevice
    private let queue = DispatchQueue(label: "hub.rtl.server")
    private var listener: NWListener?
    private var client: NWConnection?
    private var clientId = 0
    private var parser = RtlTcpCommandParser()
    /// One per client: a gain slider drag is coalesced (latest value wins).
    private var gains = GainCoalescer()
    private let clock = Date()
    private var sending = false
    let sendQueue = IqSendQueue(maxBytes: 16 << 20)

    /// Pour l'interface (appelés sur la file principale).
    var onClientChanged: ((String?) -> Void)?
    var onListening: ((Bool, String?) -> Void)?

    private(set) var sentBytes: UInt64 = 0
    /// Un client est connecté et servi (lu depuis n'importe quelle file).
    var hasClient: Bool { queue.sync { client != nil } }
    private(set) var isListening = false
    private(set) var clientCount = 0
    /// Gain commands received and actually applied (all clients).
    private(set) var gainCommandsReceived = 0
    private(set) var gainCommandsApplied = 0

    init(device: RtlDevice) {
        self.device = device
        device.onBlock = { [weak self] bytes in
            guard let self else { return }
            self.sendQueue.push(Data(bytes))
            self.queue.async { self.pump() }
        }
    }

    // MARK: - Écoute

    func start() {
        queue.async { [self] in
            guard listener == nil else { return }
            do {
                let params = NWParameters.tcp
                params.allowLocalEndpointReuse = true
                // Boucle locale UNIQUEMENT : le service n'est joignable que
                // depuis l'iPad lui-même (Spectrum Analyzer One), jamais depuis
                // le réseau. Pas d'autorisation « réseau local » nécessaire.
                params.requiredLocalEndpoint = .hostPort(host: .ipv4(.loopback),
                                                         port: NWEndpoint.Port(rawValue: Self.port)!)
                let l = try NWListener(using: params)
                l.newConnectionHandler = { [weak self] c in self?.accept(c) }
                l.stateUpdateHandler = { [weak self] st in
                    guard let self else { return }
                    switch st {
                    case .ready:
                        self.isListening = true
                        DispatchQueue.main.async { self.onListening?(true, nil) }
                    case .failed(let e):
                        self.isListening = false
                        self.listener = nil
                        DispatchQueue.main.async { self.onListening?(false, "\(e)") }
                    case .cancelled:
                        self.isListening = false
                        DispatchQueue.main.async { self.onListening?(false, nil) }
                    default: break
                    }
                }
                listener = l
                l.start(queue: queue)
            } catch {
                DispatchQueue.main.async { self.onListening?(false, "\(error)") }
            }
        }
    }

    func stop() {
        queue.async { [self] in
            dropClient(reason: "server stopped")
            listener?.cancel()
            listener = nil
        }
    }

    /// Le RTL-SDR a disparu : le client est coupé, il le verra.
    func deviceRemoved() {
        queue.async { [self] in dropClient(reason: "device removed") }
    }

    // MARK: - Client (file du serveur)

    private func accept(_ c: NWConnection) {
        dropClient(reason: "replaced by a new client")
        clientId += 1
        let id = clientId
        client = c
        parser = RtlTcpCommandParser()
        gains.cancel()
        gains = GainCoalescer()
        sendQueue.clear()
        sending = false
        c.stateUpdateHandler = { [weak self] st in
            guard let self else { return }
            switch st {
            case .ready: self.clientReady(c, id: id)
            case .failed, .cancelled: if self.clientId == id { self.dropClient(reason: "client gone") }
            default: break
            }
        }
        c.start(queue: queue)
    }

    private func clientReady(_ c: NWConnection, id: Int) {
        let endpoint = "\(c.endpoint)"
        // Ouverture + en-tête + flux, sur la file du périphérique.
        device.queue.async { [self] in
            do {
                try device.open()
                let greeting = RtlTcpProtocol.greeting(tunerType: device.tunerType,
                                                       gainCount: UInt32(device.gainSteps().count))
                try device.startRx()
                queue.async {
                    guard self.clientId == id else { return }
                    self.clientCount += 1
                    HubLog.shared.log("client \(id) ready \(endpoint)")
                    c.send(content: greeting, completion: .contentProcessed { _ in })
                    self.receive(c, id: id)
                    DispatchQueue.main.async { self.onClientChanged?(endpoint) }
                }
            } catch {
                HubLog.shared.log("client refused — \(error)")
                queue.async { if self.clientId == id { self.dropClient(reason: "\(error)") } }
            }
        }
    }

    private func receive(_ c: NWConnection, id: Int) {
        c.receive(minimumIncompleteLength: 1, maximumLength: 4096) { [weak self] data, _, done, error in
            guard let self, self.clientId == id else { return }
            if let data, !data.isEmpty {
                var direct: [RtlCommand] = []
                for cmd in self.parser.feed(data) {
                    if GainCoalescer.isGain(cmd) {
                        self.gainCommandsReceived += 1
                        if let delay = self.gains.push(cmd, now: self.now) { self.scheduleGain(self.gains, after: delay) }
                    } else {
                        direct.append(cmd)
                    }
                }
                if !direct.isEmpty {
                    self.device.queue.async { direct.forEach { self.device.apply($0) } }
                }
            }
            if done || error != nil {
                self.dropClient(reason: done ? "client closed" : "receive error \(error!)")
                return
            }
            self.receive(c, id: id)
        }
    }

    private var now: TimeInterval { Date().timeIntervalSince(clock) }

    /// Applies the pending gain of `g` after `delay`, then the next one if the
    /// slider moved meanwhile. At most one request is on the device queue.
    private func scheduleGain(_ g: GainCoalescer, after delay: TimeInterval) {
        queue.asyncAfter(deadline: .now() + delay) { [weak self] in
            guard let self, let req = g.take() else { return }
            self.gainCommandsApplied += 1
            self.device.queue.async {
                self.device.applyGain(req)
                self.queue.async {
                    if let next = g.complete(now: self.now) { self.scheduleGain(g, after: next) }
                }
            }
        }
    }

    private func pump() {
        guard !sending, let c = client, let chunk = sendQueue.pop(upTo: 1 << 20) else { return }
        sending = true
        let id = clientId
        c.send(content: chunk, completion: .contentProcessed { [weak self] err in
            guard let self else { return }
            self.queue.async {
                guard self.clientId == id else { return }
                self.sending = false
                if let err {
                    self.dropClient(reason: "send error \(err)")
                    return
                }
                self.sentBytes &+= UInt64(chunk.count)
                self.pump()
            }
        })
    }

    private func dropClient(reason: String) {
        guard let c = client else { return }
        HubLog.shared.log("client dropped — \(reason) · gain : \(gains.received) commandes reçues, \(gains.applied) appliquées")
        gains.cancel()
        client = nil
        clientId += 1
        sending = false
        sendQueue.clear()
        c.cancel()
        // Rendre le récepteur : Spectrum Analyzer One doit pouvoir l'ouvrir
        // directement dès que le client rtl_tcp de développement est parti.
        device.queue.async { self.device.close() }
        DispatchQueue.main.async { self.onClientChanged?(nil) }
    }
}
#endif
