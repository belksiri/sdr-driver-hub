import Foundation

// MARK: - Le contrat d'un backend matériel
//
// SDR Driver Hub possède les pilotes et contrôle les équipements SDR ; Spectrum
// Analyzer One reste le logiciel d'analyse et ne voit qu'un flux IQ local. Chaque
// famille d'équipements est un BACKEND : il sait détecter son matériel, dire
// s'il est prêt, servir les IQ sur un transport local et rendre compte de ses
// pertes. L'application (écran, service, journal) ne connaît que ce contrat :
// ajouter un équipement, c'est ajouter un backend, pas réécrire l'application.
//
// Aujourd'hui : RTL-SDR (DriverKit + librtlsdr, transport rtl_tcp).
// Emplacement prévu : ADALM-Pluto (réseau/IIO) — voir docs/ADDING_A_BACKEND.md.

/// Identifiant STABLE d'un backend : il sert de clé de préférence et de
/// journal, il ne change jamais d'une version à l'autre.
public struct SdrBackendID: Hashable, Sendable, CustomStringConvertible {
    public let rawValue: String
    public init(_ rawValue: String) { self.rawValue = rawValue }
    public var description: String { rawValue }

    public static let rtlSdr = SdrBackendID("rtlsdr")
}

/// Une erreur que l'utilisateur peut comprendre ET corriger : jamais un code.
public struct BackendUserError: Equatable, Sendable {
    public let message: String
    public let recovery: String?
    public init(_ message: String, recovery: String? = nil) {
        self.message = message
        self.recovery = recovery
    }
}

/// Où en est le backend. Ordonné du plus bloquant au plus avancé.
public enum BackendAvailability: Equatable, Sendable {
    /// Le pilote n'est pas encore activé par l'utilisateur (Réglages).
    case driverNotEnabled
    /// Pilote prêt, aucun équipement de cette famille n'est branché.
    case waitingForDevice
    /// Équipement détecté, ouvert ou ouvrable, pas de réception en cours.
    case ready
    /// Réception en cours.
    case streaming
    /// Équipement retiré ou en erreur ; `error` dit quoi faire.
    case failed(BackendUserError)

    public var deviceDetected: Bool {
        switch self {
        case .ready, .streaming: return true
        default: return false
        }
    }
}

/// Le transport par lequel les IQ quittent le compagnon.
public enum IqTransportKind: Equatable, Sendable {
    /// Le client ouvre LUI-MÊME le pilote (user client DriverKit, protocole
    /// client v2) : le compagnon n'a pas besoin de tourner pendant l'écoute.
    case directDriver
    #if HUB_DEV_RTLTCP
    /// Protocole rtl_tcp sur la boucle locale (127.0.0.1:port). Builds de
    /// développement seulement : ce cas n'existe pas dans un build App Store.
    case rtlTcp(port: UInt16)
    #endif
}

public enum GainControl: Equatable, Sendable {
    case automatic
    /// Gain manuel, en dixièmes de dB.
    case manual(tenthDb: Int32)
}

/// Ce que l'équipement sait faire. Statique pour une famille, précisé une fois
/// l'équipement ouvert (paliers de gain réels du tuner détecté).
public struct BackendCapabilities: Equatable, Sendable {
    public var frequencyRangeHz: ClosedRange<UInt64>
    public var sampleRateRangesHz: [ClosedRange<UInt32>]
    public var maxLosslessSampleRateHz: UInt32
    public var gainStepsTenthDb: [Int32]
    public var supportsAutomaticGain: Bool
    public var supportsBiasTee: Bool
    public var transports: [IqTransportKind]

    public init(frequencyRangeHz: ClosedRange<UInt64>, sampleRateRangesHz: [ClosedRange<UInt32>],
                maxLosslessSampleRateHz: UInt32, gainStepsTenthDb: [Int32],
                supportsAutomaticGain: Bool, supportsBiasTee: Bool, transports: [IqTransportKind]) {
        self.frequencyRangeHz = frequencyRangeHz
        self.sampleRateRangesHz = sampleRateRangesHz
        self.maxLosslessSampleRateHz = maxLosslessSampleRateHz
        self.gainStepsTenthDb = gainStepsTenthDb
        self.supportsAutomaticGain = supportsAutomaticGain
        self.supportsBiasTee = supportsBiasTee
        self.transports = transports
    }

    public func supportsSampleRate(_ hz: UInt32) -> Bool {
        sampleRateRangesHz.contains { $0.contains(hz) }
    }
}

/// Réglages radio. Pour un backend rtl_tcp, c'est le CLIENT (Spectrum) qui les
/// impose ; le compagnon les reflète et peut les fixer pour sa vérification.
public struct BackendConfiguration: Equatable, Sendable {
    public var frequencyHz: UInt32
    public var sampleRateHz: UInt32
    public var gain: GainControl
    public var biasTee: Bool

    public init(frequencyHz: UInt32, sampleRateHz: UInt32, gain: GainControl, biasTee: Bool = false) {
        self.frequencyHz = frequencyHz
        self.sampleRateHz = sampleRateHz
        self.gain = gain
        self.biasTee = biasTee
    }
}

/// L'équipement détecté et les versions en jeu.
public struct BackendDeviceInfo: Equatable, Sendable {
    public var product: String
    public var detail: String?          // tuner, numéro de révision…
    public var driverVersion: String?   // version du pilote CHARGÉ
    public init(product: String, detail: String? = nil, driverVersion: String? = nil) {
        self.product = product
        self.detail = detail
        self.driverVersion = driverVersion
    }
}

/// Compteurs cumulés depuis l'ouverture. Chaque perte appartient à UN étage
/// et n'est jamais additionnée à une autre.
public struct BackendStatistics: Equatable, Sendable {
    public var deliveredBytes: UInt64 = 0
    /// Erreurs du bus USB (contrôle + transferts).
    public var usbErrors: UInt64 = 0
    /// Blocs perdus entre le pilote et l'application.
    public var ringLostBlocks: UInt64 = 0
    /// Blocs abandonnés faute d'un client assez rapide.
    public var sendLostBlocks: UInt64 = 0
    /// Contrôle de contenu : nil tant qu'aucun bloc n'a été vu.
    public var iqContentLive: Bool?
    /// Un client (Spectrum Analyzer One en accès direct, ou un client rtl_tcp
    /// de développement) possède le récepteur.
    public var clientConnected = false
    public init() {}

    public var totalLostBlocks: UInt64 { ringLostBlocks + sendLostBlocks }
}

/// Résultat d'une vérification locale de réception (sans client).
public struct SelfTestResult: Equatable, Sendable {
    public let passed: Bool
    public let summary: String
    public init(passed: Bool, summary: String) { self.passed = passed; self.summary = summary }
}

/// Le contrat qu'implémente chaque backend. Appelé depuis la file principale ;
/// le backend protège lui-même son matériel (files série internes).
public protocol SdrBackend: AnyObject {
    var id: SdrBackendID { get }
    /// Nom public (« RTL-SDR »).
    var displayName: String { get }
    /// Version du backend (code applicatif), indépendante du pilote.
    var backendVersion: String { get }

    var availability: BackendAvailability { get }
    var deviceInfo: BackendDeviceInfo? { get }
    var capabilities: BackendCapabilities { get }
    var configuration: BackendConfiguration { get }

    /// Démarre / arrête le service qui sert les IQ aux clients locaux.
    var isServiceRunning: Bool { get }
    func startService()
    func stopService()

    /// Applique des réglages hors client (vérification locale). Un client
    /// connecté reste maître de ses réglages : refusé dans ce cas.
    func apply(_ configuration: BackendConfiguration) throws

    func statistics() -> BackendStatistics
    var lastError: BackendUserError? { get }

    /// Relit l'état du matériel ; `onChange` signale la fin.
    func refresh()

    /// Courte réception locale et contrôle du CONTENU (jamais du seul débit).
    /// Indisponible pendant qu'un client est connecté.
    func runSelfTest(_ done: @escaping (SelfTestResult) -> Void)

    /// Appelé (file principale) quand un état visible change.
    var onChange: (() -> Void)? { get set }
}

/// Les backends présents dans cette version de l'application.
public final class BackendRegistry {
    public private(set) var backends: [SdrBackend] = []
    public init() {}

    public func register(_ backend: SdrBackend) {
        precondition(!backends.contains { $0.id == backend.id },
                     "backend \(backend.id) déjà enregistré")
        backends.append(backend)
    }

    public func backend(_ id: SdrBackendID) -> SdrBackend? {
        backends.first { $0.id == id }
    }

    /// Le backend à mettre en avant : celui qui reçoit, sinon celui dont le
    /// matériel est branché, sinon le premier enregistré.
    public var primary: SdrBackend? {
        backends.first { $0.availability == .streaming }
            ?? backends.first { $0.availability.deviceDetected }
            ?? backends.first
    }
}

// MARK: - Spectrum peut-il se connecter ?

/// Réponse affichée en tête de l'écran : l'utilisateur doit savoir d'un coup
/// d'œil si Spectrum Analyzer One trouvera le RTL-SDR, et sinon quoi faire.
public enum SpectrumReadiness: Equatable, Sendable {
    case connected
    case canConnect
    case notReady(BackendUserError)

    /// Spectrum Analyzer One ouvre le pilote lui-même (protocole client v2) :
    /// seul l'état du pilote et du matériel compte, jamais un service du
    /// compagnon (qui n'existe que dans les builds de développement).
    public static func evaluate(availability: BackendAvailability, clientConnected: Bool,
                                deviceName: String) -> SpectrumReadiness {
        if clientConnected { return .connected }
        switch availability {
        case .driverNotEnabled:
            return .notReady(BackendUserError(
                "The \(deviceName) driver is not enabled.",
                recovery: "Open Settings › Apps › SDR Driver Hub › Drivers and turn it on."))
        case .waitingForDevice:
            return .notReady(BackendUserError(
                "No \(deviceName) detected.",
                recovery: "Plug the \(deviceName) into the iPad's USB-C port. If it is already plugged in, "
                    + "turn the driver on in Settings › Apps › SDR Driver Hub › Drivers."))
        case .failed(let e):
            return .notReady(e)
        case .ready, .streaming:
            return .canConnect
        }
    }
}

// MARK: - Débit réel

/// Débit réel à partir de compteurs cumulés, sur la dernière seconde.
public struct ThroughputSampler: Sendable {
    private var lastBytes: UInt64?
    private var lastTime: TimeInterval = 0
    public private(set) var bytesPerSecond: Double = 0
    public init() {}

    public mutating func record(totalBytes: UInt64, at time: TimeInterval) {
        defer { lastBytes = totalBytes; lastTime = time }
        guard let prev = lastBytes, time > lastTime else { bytesPerSecond = 0; return }
        // Compteur remis à zéro (nouvelle ouverture) : pas de débit négatif.
        bytesPerSecond = totalBytes >= prev ? Double(totalBytes - prev) / (time - lastTime) : 0
    }
}
