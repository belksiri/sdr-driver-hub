//
//  BHRFTypes.swift — Ponts Swift au-dessus du contrat C.
//
//  Rien ici ne redéfinit une constante : tout passe par bhrf_shared.h, importé
//  via le bridging header. Une constante recopiée en Swift finirait par diverger
//  de celle du pilote, et une divergence sur une valeur de bRequest est
//  exactement le genre de bug qui ouvre un trou dans la liste blanche.
//

import Foundation

enum BHRF {
    static let protocolVersion = UInt32(BHRF_PROTOCOL_VERSION)
    static let vendorID = UInt16(BHRF_USB_VENDOR_ID)
    static let productID = UInt16(BHRF_USB_PRODUCT_ID)

    // SDR Driver Hub : la table des débits HackRF (hackrf_ios_control.c) est
    // retirée de cette copie — sans objet pour le RTL-SDR.

    /// Les quatre paliers de la campagne go/no-go.
    static let campaignRates: [UInt32] = [2_000_000, 10_000_000, 16_000_000, 20_000_000]
}

/// Le jeu canonique des six requêtes interdites.
///
/// Une seule définition, utilisée par le bouton « Tester les refus », par les
/// tests automatisés et par le script de la vidéo destinée à Apple. Les trois
/// doivent porter sur exactement les mêmes requêtes, sinon la démonstration ne
/// démontre pas ce que la preuve prouve.
struct ForbiddenProbe: Identifiable, Sendable {
    let id: Int
    let label: String
    let request: BHRFControlRequest
    let expected: BHRFDenyReason

    static let canonicalSix: [ForbiddenProbe] = [
        ForbiddenProbe(id: 1, label: "SET_TRANSCEIVER_MODE = TRANSMIT",
                       request: .make(direction: .out,
                                      request: UInt8(BHRF_VR_SET_TRANSCEIVER_MODE),
                                      value: UInt16(BHRF_TRANSCEIVER_MODE_TRANSMIT)),
                       expected: kBHRFDenyTransmitMode),
        ForbiddenProbe(id: 2, label: "SET_TRANSCEIVER_MODE = CPLD_UPDATE",
                       request: .make(direction: .out,
                                      request: UInt8(BHRF_VR_SET_TRANSCEIVER_MODE),
                                      value: UInt16(BHRF_TRANSCEIVER_MODE_CPLD_UPDATE)),
                       expected: kBHRFDenyCpldUpdateMode),
        ForbiddenProbe(id: 3, label: "SPIFLASH_ERASE",
                       request: .make(direction: .out,
                                      request: UInt8(BHRF_VR_SPIFLASH_ERASE)),
                       expected: kBHRFDenyFlashAccess),
        ForbiddenProbe(id: 4, label: "SPIFLASH_WRITE",
                       request: .make(direction: .out,
                                      request: UInt8(BHRF_VR_SPIFLASH_WRITE)),
                       expected: kBHRFDenyFlashAccess),
        ForbiddenProbe(id: 5, label: "SET_TXVGA_GAIN",
                       request: .make(direction: .in,
                                      request: UInt8(BHRF_VR_SET_TXVGA_GAIN),
                                      length: 1),
                       expected: kBHRFDenyTxGain),
        ForbiddenProbe(id: 6, label: "bRequest inconnu (99)",
                       request: .make(direction: .out, request: 99),
                       expected: kBHRFDenyUnknownRequest)
    ]
}

/// Résultat d'une tentative interdite, tel qu'affiché et journalisé.
struct ForbiddenProbeResult: Identifiable, Sendable {
    let id: Int
    let label: String
    let denied: Bool
    let reason: String
    /// L'ioReturn renvoyé par le pilote. Doit valoir 0 sur un refus : c'est la
    /// preuve qu'aucun transfert USB n'a été tenté.
    let ioReturn: Int32
    let expectedReason: String

    var isCorrect: Bool { denied && ioReturn == 0 && reason == expectedReason }
}
