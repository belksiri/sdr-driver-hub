//
//  DriverKitUsbTransport.swift — Le transport réel, via IOKit.
//
//  Ce fichier est le SEUL de l'application qui touche à IOKit, et il est identique
//  sur macOS et iPadOS : `IOServiceOpen`, `IOConnectCallStructMethod` et
//  `IOServiceAddMatchingNotification` existent des deux côtés. Ce qui diffère
//  entre les deux plateformes n'est pas ici, c'est l'INSTALLATION du pilote —
//  `OSSystemExtensionRequest` sur macOS, l'activation dans Réglages sur iPadOS.
//  D'où la séparation : ce transport est partagé, les installateurs ne le sont
//  pas.
//

import Foundation

// Pas d'`import IOKit` : sur iOS, IOKit.framework fournit bien IOKitLib.h et
// IOKit.tbd, mais AUCUN modulemap — `import IOKit` y échoue avec
// « unable to resolve module dependency ». Les symboles arrivent donc par le
// bridging header de chaque cible (#import <IOKit/IOKitLib.h>), et le lien se
// fait par -framework IOKit. C'est exactement le genre de divergence de
// plateforme qu'il valait mieux découvrir aujourd'hui que demain sur l'iPad.

final class DriverKitUsbTransport: UsbDeviceTransport {

    private(set) var state: ProbeState = .driverNotInstalled {
        didSet {
            guard state != oldValue else { return }
            let s = state
            DispatchQueue.main.async { [weak self] in self?.onStateChange?(s) }
        }
    }

    var onStateChange: ((ProbeState) -> Void)?

    /// La valeur de `IOUserClass` du dictionnaire IOKitPersonalities du dext.
    ///
    /// ATTENTION — ce n'est PAS le nom d'une classe IOKit au sens du registre.
    /// Un service DriverKit déclaré `IOClass = IOUserService` apparaît dans le
    /// registre avec :
    ///
    ///     classe de l'entrée .... IOUserService        ← jamais BelksiriRtlSdrDriver
    ///     nom de l'entrée ....... BelksiriRtlSdrDriver
    ///     propriété IOUserClass . BelksiriRtlSdrDriver
    ///
    /// C'est pourquoi la recherche se fait par NOM (`IOServiceNameMatching`) et
    /// jamais par classe (`IOServiceMatching`) : cette dernière construit
    /// `{ IOProviderClass: … }`, comparé à la classe de l'entrée, et ne peut
    /// donc jamais correspondre. Vérifié sur machine réelle — voir
    /// `evidence/09-liaison-user-client.md`.
    /// Nom d'entrée du service recherché (« BelksiriRtlSdrDriver » pour le
    /// backend RTL-SDR).
    private let serviceUserClass: String

    /// Journal technique de la découverte. Il ne remplace pas les journaux
    /// système : il vit DANS l'application, donc il part avec l'export et reste
    /// lisible quand Console.app n'est pas branché.
    private(set) var discoveryLog: [String] = []
    var onDiscoveryLog: ((String) -> Void)?

    private func trace(_ message: String) {
        discoveryLog.append(message)
        if discoveryLog.count > 200 { discoveryLog.removeFirst(discoveryLog.count - 200) }
        let m = message
        DispatchQueue.main.async { [weak self] in self?.onDiscoveryLog?(m) }
    }

    /// Ce que la découverte a réellement établi. Sert à ne plus afficher
    /// « Pilote non installé » quand le pilote tourne et que c'est l'ouverture
    /// du user client qui échoue.
    enum Diagnosis: Equatable {
        case notSearchedYet
        case noServicePublished          // aucun IOUserService à ce nom
        case servicePublishedNotOpened   // service trouvé, IOServiceOpen pas encore tenté
        case openRefused(kern_return_t)  // IOServiceOpen a échoué
        case open                        // user client ouvert

        var detail: String {
            switch self {
            case .notSearchedYet:
                return "Recherche du service non encore effectuée."
            case .noServicePublished:
                return "Aucun service du pilote publié dans IOKit. "
                     + "Le pilote est-il activé dans Réglages, et le périphérique est-il branché ?"
            case .servicePublishedNotOpened:
                return "Service publié. User client pas encore ouvert."
            case .openRefused(let code):
                return String(format: "Service publié, mais IOServiceOpen a échoué (0x%08x). "
                              + "Entitlement communicates-with-drivers, ou UserClientProperties.",
                              UInt32(bitPattern: code))
            case .open:
                return "User client ouvert."
            }
        }
    }

    private(set) var diagnosis: Diagnosis = .notSearchedYet

    var linkDiagnostic: String { diagnosis.detail }

    private var connection: io_connect_t = IO_OBJECT_NULL
    private var service: io_service_t = IO_OBJECT_NULL
    private var notifyPort: IONotificationPortRef?
    private var matchedIterator: io_iterator_t = IO_OBJECT_NULL
    private var terminatedIterator: io_iterator_t = IO_OBJECT_NULL

    /// Toutes les opérations IOKit passent par cette file série. Le drain du
    /// ring et les requêtes de contrôle ne doivent jamais s'exécuter sur le
    /// thread principal : à 40 Mo/s, une seule opération bloquante sur le thread
    /// d'interface suffit à produire les saccades qu'on cherche à mesurer.
    private let queue = DispatchQueue(label: "com.belksiri.sdrdriverhub.iokit")

    private(set) var lastProtocolInfo: BHRFProtocolInfo?

    // ── Étape 5 : le ring mappé et son drainage ──────────────────────────
    /// Le drain partagé (voir `RingDrain`) : copie chaque bloc hors du ring,
    /// VALIDE la copie, la livre au consommateur et compte les pertes. Le
    /// pilote ne peut pas connaître ces pertes : le ring ne contient
    /// volontairement aucun curseur de lecture.
    private let drain = RingDrain()
    private var mappedAddress: mach_vm_address_t = 0
    private var mappedSize: mach_vm_size_t = 0
    private var drainTimer: DispatchSourceTimer?

    /// Ce que l'application a demandé au pilote au dernier démarrage. Le drain
    /// en déduit la fenêtre de lecture sûre avec la même borne que le pilote.
    private var requestedInFlight: UInt32 = 0

    var blockConsumer: RingBlockConsumer? {
        get { queue.sync { drain.consumer } }
        set { queue.sync { drain.consumer = newValue } }
    }

    init(serviceUserClass: String) {
        self.serviceUserClass = serviceUserClass
        startMatchingNotifications()
    }

    deinit {
        stopDrainLocked()
        unmapRingLocked()
        disconnect()
        if matchedIterator != IO_OBJECT_NULL { IOObjectRelease(matchedIterator) }
        if terminatedIterator != IO_OBJECT_NULL { IOObjectRelease(terminatedIterator) }
        if let port = notifyPort { IONotificationPortDestroy(port) }
    }

    // MARK: - Découverte

    /// Sur iPadOS l'application ne peut PAS énumérer le bus USB : c'est le
    /// matching IOKit du pilote qui découvre l'appareil, et l'application ne
    /// peut qu'observer l'apparition puis la disparition du service. C'est une
    /// inversion complète du modèle desktop, où l'application interroge.
    /// Construit un dictionnaire de correspondance NEUF à chaque appel.
    ///
    /// `IOServiceAddMatchingNotification` CONSOMME une référence sur le
    /// dictionnaire qu'on lui passe. La version précédente en gardait un
    /// second usage après la première inscription, ce qui revenait à copier un
    /// objet déjà relâché. Chaque inscription reçoit désormais le sien.
    // `internal` et non `private` : ce dictionnaire est le siège exact du
    // défaut qui a bloqué la première session sur iPad, et un test le vérifie.
    func makeMatchingDictionary() -> CFMutableDictionary? {
        IOServiceNameMatching(serviceUserClass)
    }

    private func startMatchingNotifications() {
        trace("Recherche par NOM d'entrée « \(serviceUserClass) » "
              + "(et non par classe : un dext s'enregistre comme IOUserService).")

        notifyPort = IONotificationPortCreate(kIOMainPortDefault)
        guard let port = notifyPort else {
            trace("ÉCHEC : IONotificationPortCreate a renvoyé nil.")
            state = .driverNotInstalled
            return
        }
        IONotificationPortSetDispatchQueue(port, queue)

        let selfPtr = Unmanaged.passUnretained(self).toOpaque()

        if let appearing = makeMatchingDictionary() {
            let r = IOServiceAddMatchingNotification(
                port, kIOFirstMatchNotification, appearing, { ctx, iterator in
                    guard let ctx else { return }
                    let me = Unmanaged<DriverKitUsbTransport>.fromOpaque(ctx).takeUnretainedValue()
                    me.drainAppeared(iterator)
                }, selfPtr, &matchedIterator)
            trace(r == kIOReturnSuccess
                  ? "Notification d'apparition inscrite."
                  : String(format: "Notification d'apparition refusée (0x%08x).", UInt32(bitPattern: r)))
            drainAppeared(matchedIterator)
        }

        if let terminating = makeMatchingDictionary() {
            let r = IOServiceAddMatchingNotification(
                port, kIOTerminatedNotification, terminating, { ctx, iterator in
                    guard let ctx else { return }
                    let me = Unmanaged<DriverKitUsbTransport>.fromOpaque(ctx).takeUnretainedValue()
                    me.drainTerminated(iterator)
                }, selfPtr, &terminatedIterator)
            trace(r == kIOReturnSuccess
                  ? "Notification de terminaison inscrite."
                  : String(format: "Notification de terminaison refusée (0x%08x).", UInt32(bitPattern: r)))
            drainTerminated(terminatedIterator)
        }

        if service == IO_OBJECT_NULL {
            locateServiceIfNeeded()
        }
    }

    private func drainAppeared(_ iterator: io_iterator_t) {
        var count = 0
        while case let obj = IOIteratorNext(iterator), obj != IO_OBJECT_NULL {
            count += 1
            if service == IO_OBJECT_NULL {
                adopt(obj, via: "notification d'apparition")
            } else {
                IOObjectRelease(obj)
            }
        }
        if count > 0 {
            trace("Apparition : \(count) service(s) correspondant(s).")
        }
    }

    private func drainTerminated(_ iterator: io_iterator_t) {
        var terminated = false
        while case let obj = IOIteratorNext(iterator), obj != IO_OBJECT_NULL {
            IOObjectRelease(obj)
            terminated = true
        }
        guard terminated else { return }
        trace("Terminaison : le service a disparu du registre.")
        diagnosis = .noServicePublished

        // Ordre strict : couper le dialogue AVANT de relâcher le service, et ne
        // jamais relire quoi que ce soit après. C'est exactement le défaut
        // corrigé côté Android en phase 3.4 — une session morte réutilisée
        // oblige à redémarrer l'application.
        closeConnection()
        if service != IO_OBJECT_NULL {
            IOObjectRelease(service)
            service = IO_OBJECT_NULL
        }
        state = .terminated
    }

    /// Recherche synchrone, pour le cas où le service existait déjà au
    /// lancement et où aucune notification ne viendra.
    ///
    /// Deux stratégies, essayées dans l'ordre, et la trace dit laquelle a
    /// abouti. La seconde n'est pas un doublon superstitieux : elle protège
    /// d'un changement de convention de nommage côté IOKit, et surtout elle
    /// DIAGNOSTIQUE — si elle trouve le service que la première a manqué, on
    /// sait immédiatement où regarder.
    private func locateServiceIfNeeded() {
        guard service == IO_OBJECT_NULL else { return }

        // 1. Par nom d'entrée. C'est la voie normale.
        if let matching = makeMatchingDictionary() {
            let found = IOServiceGetMatchingService(kIOMainPortDefault, matching)
            if found != IO_OBJECT_NULL {
                adopt(found, via: "nom d'entrée")
                return
            }
        }

        // 2. Par balayage des IOUserService, en filtrant sur la propriété
        //    IOUserClass. Plus coûteux, mais indépendant du nommage.
        if let byProperty = findByUserClassProperty() {
            adopt(byProperty, via: "propriété IOUserClass")
            return
        }

        diagnosis = .noServicePublished
        trace("Aucun service « \(serviceUserClass) » dans le registre IOKit.")
        if state != .terminated { state = .driverNotInstalled }
    }

    private func adopt(_ found: io_service_t, via strategy: String) {
        service = found
        diagnosis = .servicePublishedNotOpened
        var cls = [CChar](repeating: 0, count: 128)
        IOObjectGetClass(found, &cls)
        let className = String(cString: cls)
        trace("Service TROUVÉ par \(strategy) — classe de l'entrée : \(className).")
        state = .deviceAttached
    }

    /// Balaie les `IOUserService` et retient celui dont la propriété
    /// `IOUserClass` vaut le nom attendu.
    private func findByUserClassProperty() -> io_service_t? {
        guard let matching = IOServiceMatching("IOUserService") else { return nil }
        var iterator: io_iterator_t = IO_OBJECT_NULL
        guard IOServiceGetMatchingServices(kIOMainPortDefault, matching, &iterator)
                == kIOReturnSuccess else { return nil }
        defer { IOObjectRelease(iterator) }

        var result: io_service_t?
        while case let obj = IOIteratorNext(iterator), obj != IO_OBJECT_NULL {
            if result == nil,
               let raw = IORegistryEntryCreateCFProperty(obj, "IOUserClass" as CFString,
                                                         kCFAllocatorDefault, 0),
               let name = raw.takeRetainedValue() as? String,
               name == serviceUserClass {
                result = obj
                continue   // conservé : ne pas le relâcher
            }
            IOObjectRelease(obj)
        }
        return result
    }

    // MARK: - Connexion

    func connect() throws -> BHRFProtocolInfo {
        try queue.sync {
            locateServiceIfNeeded()
            guard service != IO_OBJECT_NULL else {
                // Le service n'est pas publié. Cela peut vouloir dire « pilote
                // non activé » ou « pilote actif mais aucun périphérique branché » :
                // le diagnostic le dit, l'état ne le distingue pas encore.
                trace("connect() : abandon, aucun service.")
                throw TransportError.serviceNotFound
            }

            if connection == IO_OBJECT_NULL {
                // type 0 : le dext ignore le type et crée toujours le user
                // client décrit par UserClientProperties (voir NewUserClient).
                let ret = IOServiceOpen(service, mach_task_self_, 0, &connection)
                guard ret == kIOReturnSuccess else {
                    connection = IO_OBJECT_NULL
                    diagnosis = .openRefused(ret)
                    trace(String(format: "IOServiceOpen a ÉCHOUÉ : kern_return_t = 0x%08x.",
                                 UInt32(bitPattern: ret)))
                    trace("  → \(Diagnosis.openRefused(ret).detail)")
                    throw TransportError.openFailed(ret)
                }
                diagnosis = .open
                trace("IOServiceOpen a RÉUSSI — user client ouvert.")
            }

            // PREMIER appel obligatoire : sans lui, on ne sait pas si les
            // structures qu'on s'apprête à échanger ont la même disposition des
            // deux côtés.
            let info: BHRFProtocolInfo = try callStruct(selector: kBHRFSelProtocolVersion,
                                                        input: Optional<Never>.none)
            guard info.protocolVersion == DriverProtocol.version else {
                closeConnectionLocked()
                throw TransportError.protocolMismatch(driver: info.protocolVersion,
                                                      app: DriverProtocol.version)
            }
            lastProtocolInfo = info
            return info
        }
    }

    func disconnect() {
        queue.sync { closeConnectionLocked() }
    }

    private func closeConnection() { queue.async { self.closeConnectionLocked() } }

    private func closeConnectionLocked() {
        guard connection != IO_OBJECT_NULL else { return }
        if diagnosis == .open { diagnosis = .servicePublishedNotOpened }
        // Démonter le ring AVANT de fermer le user client : un mappage qui
        // survivrait à la connexion pointerait sur de la mémoire libérée.
        stopDrainLocked()
        unmapRingLocked()
        IOServiceClose(connection)
        connection = IO_OBJECT_NULL
        lastProtocolInfo = nil
    }

    // MARK: - Appels

    /// Appel générique struct→struct. La taille de la réponse est vérifiée à
    /// l'octet près : une réponse plus courte ou plus longue signale un
    /// désaccord de contrat et doit lever, jamais être complétée de zéros.
    private func callStruct<Out, In>(selector: Int, input: In?) throws -> Out {
        guard connection != IO_OBJECT_NULL else { throw TransportError.notConnected }

        var out = [UInt8](repeating: 0, count: MemoryLayout<Out>.size)
        var outSize = MemoryLayout<Out>.size

        let ret: kern_return_t
        if var inValue = input {
            ret = withUnsafeBytes(of: &inValue) { inRaw in
                out.withUnsafeMutableBytes { outRaw in
                    IOConnectCallStructMethod(connection, UInt32(selector),
                                              inRaw.baseAddress, inRaw.count,
                                              outRaw.baseAddress, &outSize)
                }
            }
        } else {
            ret = out.withUnsafeMutableBytes { outRaw in
                IOConnectCallStructMethod(connection, UInt32(selector),
                                          nil, 0,
                                          outRaw.baseAddress, &outSize)
            }
        }

        guard ret == kIOReturnSuccess else {
            throw TransportError.callFailed(selector: UInt32(selector), code: ret)
        }
        guard outSize == MemoryLayout<Out>.size else {
            throw TransportError.unexpectedSize(expected: MemoryLayout<Out>.size, got: outSize)
        }
        return out.withUnsafeBytes { $0.loadUnaligned(as: Out.self) }
    }

    func controlRequest(_ request: BHRFControlRequest) throws -> BHRFControlResponse {
        try queue.sync {
            try callStruct(selector: kBHRFSelControlRequest, input: request)
        }
    }

    /// Sélecteur du protocole client v2 sans entrée (Acquire, Release,
    /// DeviceInfo).
    func call<Out>(_ selector: Int) throws -> Out {
        try queue.sync { try callStruct(selector: selector, input: Optional<Never>.none) }
    }

    /// Sélecteur du protocole client v2 avec entrée (Command).
    func call<Out, In>(_ selector: Int, _ input: In) throws -> Out {
        try queue.sync { try callStruct(selector: selector, input: input) }
    }

    func ringStats() throws -> BHRFRingStats {
        try queue.sync {
            var stats: BHRFRingStats =
                try callStruct(selector: kBHRFSelRingStats, input: Optional<Never>.none)
            // Le pilote laisse ce champ à zéro faute de pouvoir le connaître :
            // sans curseur de lecture dans le ring, lui seul ignore de combien
            // le consommateur a été distancé. On le renseigne ici, au seul
            // endroit où l'information existe — TOUTES causes confondues :
            // blocs dépassés, copies invalidées, blocs publiés vides.
            stats.overflowBlocks = drain.lostBlocks
            return stats
        }
    }

    // MARK: - Étape 5 : réception continue

    /// Séquence : démarrer les transferts côté pilote, PUIS mapper le ring.
    /// Cet ordre est imposé par le pilote, qui n'alloue le ring qu'au premier
    /// démarrage — mapper avant reviendrait à demander une mémoire qui n'existe
    /// pas encore.
    func startRx(buffersInFlight: UInt32, bufferBytes: UInt32) throws {
        let req = BHRFStartRxRequest(buffersInFlight: buffersInFlight,
                                     bufferBytes: bufferBytes,
                                     reserved0: 0, reserved1: 0)
        try queue.sync {
            let st: BHRFSimpleStatus = try callStruct(selector: kBHRFSelStartRx, input: req)
            if st.statusValue == kBHRFStatusUnsupportedSelector {
                throw TransportError.notSupportedYet("La réception continue")
            }
            if st.status == Int32(BRTL_BHRF_STATUS_NOT_OWNER) {
                throw TransportError.notOwner
            }
            guard st.statusValue == kBHRFStatusOK else {
                throw TransportError.usbError(kern_return_t(st.ioReturn))
            }
            requestedInFlight = buffersInFlight
            try mapRingLocked()
            startDrainLocked()
        }
        state = .receiving
    }

    func stopRx() throws {
        queue.sync {
            stopDrainLocked()
            let st: BHRFSimpleStatus? =
                try? callStruct(selector: kBHRFSelStopRx, input: Optional<Never>.none)
            _ = st
            // Un dernier tour de drainage APRÈS l'arrêt : le pilote a pu
            // publier des blocs entre le dernier tic et l'abandon des
            // transferts. Les abandonner fausserait le total d'octets.
            drainOnceLocked()
            unmapRingLocked()
        }
        state = .deviceAttached
    }

    // MARK: - Mappage

    private func mapRingLocked() throws {
        guard connection != IO_OBJECT_NULL else { throw TransportError.notConnected }
        guard mappedAddress == 0 else { return }

        var address: mach_vm_address_t = 0
        var size: mach_vm_size_t = 0
        // kIOMapReadOnly : le ring est en lecture seule côté application, et le
        // pilote l'impose de son côté (kIOUserClientMemoryReadOnly). Demander
        // l'écriture ici échouerait — c'est voulu, l'application n'a rien à
        // écrire dans une structure dont le pilote est seul propriétaire.
        let ret = IOConnectMapMemory64(connection,
                                       UInt32(kBHRFMemoryTypeRing),
                                       mach_task_self_,
                                       &address, &size,
                                       IOOptionBits(kIOMapAnywhere | kIOMapReadOnly))
        guard ret == kIOReturnSuccess, address != 0, size > 0 else {
            throw TransportError.callFailed(selector: UInt32(kBHRFSelMapRing), code: ret)
        }
        mappedAddress = address
        mappedSize = size

        do {
            try drain.attach(base: UnsafeRawPointer(bitPattern: UInt(address))!,
                             mappedBytes: Int(size),
                             requestedInFlight: requestedInFlight)
        } catch {
            unmapRingLocked()
            throw error
        }
    }

    private func unmapRingLocked() {
        drain.detach()
        if mappedAddress != 0 {
            IOConnectUnmapMemory64(connection, UInt32(kBHRFMemoryTypeRing),
                                   mach_task_self_, mappedAddress)
            mappedAddress = 0
            mappedSize = 0
        }
    }

    // MARK: - Drainage

    /// 2 ms. Même au débit maximal, le pilote publie un bloc toutes les quelques ms :
    /// ce rythme laisse plusieurs occasions de rattraper un retard avant que le
    /// ring, qui absorbe 0,4 s, ne soit distancé.
    private func startDrainLocked() {
        stopDrainLocked()
        let timer = DispatchSource.makeTimerSource(queue: queue)
        timer.schedule(deadline: .now(), repeating: .milliseconds(2), leeway: .milliseconds(1))
        timer.setEventHandler { [weak self] in self?.drainOnceLocked() }
        drainTimer = timer
        timer.resume()
    }

    private func stopDrainLocked() {
        drainTimer?.cancel()
        drainTimer = nil
    }

    /// Un tour de drainage. Appelé UNIQUEMENT depuis `queue`.
    private func drainOnceLocked() {
        guard drain.isAttached else { return }
        if drain.drainOnce(maxBlocks: 64) {
            stopDrainLocked()
            state = .terminated
        }
    }
}

extension BHRFSimpleStatus {
    var statusValue: BHRFStatus { BHRFStatus(UInt32(bitPattern: status)) }
}
