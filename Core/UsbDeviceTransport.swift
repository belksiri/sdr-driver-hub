//
//  UsbDeviceTransport.swift — Le contrat entre l'application et un pilote
//  DriverKit de SDR Driver Hub (protocole commun bhrf_shared.h).
//
//  Indépendant de l'équipement : le backend RTL-SDR l'utilise avec le pilote
//  BelksiriRtlSdrDriver ; un futur pilote USB parlerait le même protocole.
//

import Foundation

/// Ce que l'application sait dire de l'état du couple pilote + appareil.
/// Six états, volontairement distincts : confondre « pilote non activé » et
/// « aucun périphérique » envoie l'utilisateur chercher une panne matérielle
/// qui n'existe pas.
enum ProbeState: String, CaseIterable, Sendable {
    case driverNotInstalled  = "Pilote non installé"
    case driverNotEnabled    = "Pilote non activé"
    case waitingForDevice    = "En attente du périphérique"
    case deviceAttached      = "Périphérique détecté"
    case receiving           = "Réception en cours"
    case terminated          = "Périphérique retiré"

    /// Vrai quand l'utilisateur doit agir lui-même (et non attendre).
    var needsUserAction: Bool {
        self == .driverNotInstalled || self == .driverNotEnabled
    }
}

/// Erreurs du transport. Chacune doit pouvoir se traduire en une phrase que
/// l'utilisateur peut agir — pas un code.
enum TransportError: Error, LocalizedError {
    case notConnected
    case serviceNotFound
    case openFailed(kern_return_t)
    case callFailed(selector: UInt32, code: kern_return_t)
    case unexpectedSize(expected: Int, got: Int)
    case protocolMismatch(driver: UInt32, app: UInt32)
    case denied(reason: UInt32, request: UInt8)
    case usbError(kern_return_t)
    case notSupportedYet(String)
    /// Protocole client v2 : la radio appartient à un autre client.
    case busy
    /// Protocole client v2 : opération réservée au propriétaire de la radio.
    case notOwner
    /// Protocole client v2 : refus du pilote, avec son statut (BRTLClientStatus).
    case client(status: Int32, controller: Int32)

    var errorDescription: String? {
        switch self {
        case .notConnected:
            return "Aucune connexion au pilote."
        case .serviceNotFound:
            return "Le service du pilote est introuvable. Le pilote est-il activé ?"
        case .openFailed(let k):
            return String(format: "Ouverture du user client impossible (0x%08x).", UInt32(bitPattern: k))
        case .callFailed(let sel, let code):
            return String(format: "Appel %u refusé par le pilote (0x%08x).", sel, UInt32(bitPattern: code))
        case .unexpectedSize(let expected, let got):
            return "Structure de \(got) octets au lieu de \(expected) : le pilote et l'application ne s'accordent pas."
        case .protocolMismatch(let driver, let app):
            return "Pilote en protocole v\(driver), application en v\(app). Débranchez puis rebranchez le périphérique pour recharger le pilote."
        case .denied(let reason, let request):
            return "Requête \(request) refusée par le pilote (motif \(reason))."
        case .usbError(let k):
            return String(format: "Erreur USB (0x%08x).", UInt32(bitPattern: k))
        case .notSupportedYet(let what):
            return "\(what) n'est pas encore implémenté dans ce pilote."
        case .busy:
            return "Le récepteur est utilisé par une autre application (Spectrum Analyzer One)."
        case .notOwner:
            return "Le récepteur n'est pas attribué à SDR Driver Hub."
        case .client(let status, let controller):
            let name = String(cString: brtl_client_status_name(BRTLClientStatus(UInt32(bitPattern: status))))
            return "Refus du pilote : \(name) (contrôleur \(controller))."
        }
    }
}

/// Le contrat. Volontairement étroit : tout ce qui peut vivre au-dessus (calcul
/// de fréquence, cadence, journal) vit au-dessus, dans du code partagé et testé.
protocol UsbDeviceTransport: AnyObject {
    var state: ProbeState { get }

    /// Ouvre le user client. Le premier appel effectué DOIT être la lecture de
    /// la version de protocole : le pilote ne se recharge qu'au débranchement,
    /// donc l'application peut parfaitement tomber sur un pilote plus ancien
    /// qu'elle, et doit le refuser plutôt qu'interpréter de travers.
    func connect() throws -> BHRFProtocolInfo

    func disconnect()

    /// Émet une requête de contrôle. Une réponse de statut `denied` n'est PAS
    /// une erreur de transport : c'est le filtre qui a fonctionné. L'appelant
    /// décide quoi en faire.
    func controlRequest(_ request: BHRFControlRequest) throws -> BHRFControlResponse

    func ringStats() throws -> BHRFRingStats

    /// Démarre la réception continue. Côté réel : le pilote amorce les
    /// transferts bulk IN, puis l'application mappe le ring en lecture seule et
    /// le draine. Un pilote antérieur à l'étape 5 répond
    /// `unsupportedSelector`, et le transport lève alors `.notSupportedYet`
    /// plutôt que d'afficher un débit nul qu'on prendrait pour une panne.
    func startRx(buffersInFlight: UInt32, bufferBytes: UInt32) throws
    func stopRx() throws

    /// Notifie l'appelant d'un changement d'état venu du système
    /// (apparition, terminaison). Appelé sur la file principale.
    var onStateChange: ((ProbeState) -> Void)? { get set }

    /// Ce que la liaison au pilote a réellement établi, en une phrase.
    ///
    /// Existe parce qu'un seul état — « Pilote non installé » — recouvrait cinq
    /// situations distinctes : pilote absent, pilote désactivé, pilote actif
    /// sans périphérique, périphérique attaché mais user client introuvable,
    /// ouverture refusée. Afficher la mauvaise envoie chercher une panne qui
    /// n'existe pas, ce qui est exactement ce qui s'est produit lors de la
    /// première session sur iPad.
    var linkDiagnostic: String { get }

    /// Destinataire des blocs drainés du ring (affichage, enregistrement).
    /// Appelé sur la file de drainage du transport. Nil : les blocs sont
    /// drainés, comptés et abandonnés — le comportement du prototype seul.
    var blockConsumer: RingBlockConsumer? { get set }
}

extension UsbDeviceTransport {
    var linkDiagnostic: String { "" }
}

// MARK: - Confort de construction des requêtes

extension BHRFControlRequest {
    /// Construit une requête de contrôle en recopiant au plus
    /// `BHRF_CTRL_MAX_DATA` octets de charge utile.
    static func make(direction: BHRFDirectionSwift,
                     request: UInt8,
                     value: UInt16 = 0,
                     index: UInt16 = 0,
                     length: UInt16 = 0,
                     payload: [UInt8] = []) -> BHRFControlRequest {
        var r = BHRFControlRequest()
        r.bmRequestType = direction.bmRequestType
        r.bRequest = request
        r.wValue = value
        r.wIndex = index
        r.wLength = length
        if !payload.isEmpty {
            withUnsafeMutableBytes(of: &r.data) { raw in
                let n = min(payload.count, Int(BHRF_CTRL_MAX_DATA))
                raw.copyBytes(from: payload.prefix(n))
            }
        }
        return r
    }
}

/// Miroir Swift de `BHRFDirection`, pour éviter de manipuler des `bmRequestType`
/// bruts dans le code applicatif.
enum BHRFDirectionSwift {
    case out, `in`

    var bmRequestType: UInt8 {
        switch self {
        case .out: return UInt8(BRTL_BMREQ_OUT_VENDOR)
        case .in:  return UInt8(BRTL_BMREQ_IN_VENDOR)
        }
    }
}

extension BHRFControlResponse {
    var statusValue: BHRFStatus { BHRFStatus(UInt32(bitPattern: status)) }

    /// Les octets réellement renvoyés par l'appareil.
    var payload: [UInt8] {
        let n = min(Int(actualLength), Int(BHRF_CTRL_MAX_DATA))
        guard n > 0 else { return [] }
        return withUnsafeBytes(of: data) { Array($0.prefix(n)) }
    }

    var payloadString: String {
        let bytes = payload.prefix { $0 != 0 }
        return String(decoding: bytes, as: UTF8.self)
    }
}

extension BHRFStatus {
    var label: String { String(cString: bhrf_status_name(Int32(bitPattern: rawValue))) }
}
