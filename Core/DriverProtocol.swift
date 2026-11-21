//
//  DriverProtocol.swift — Ponts Swift au-dessus du contrat C (bhrf_shared.h).
//
//  Rien ici ne redéfinit une constante : tout passe par bhrf_shared.h, importé
//  via le bridging header. Une constante recopiée en Swift finirait par diverger
//  de celle du pilote.
//

import Foundation

enum DriverProtocol {
    /// Version du protocole client que cette application sait parler : v2,
    /// commandes de haut niveau (brtl_client_protocol.h).
    static let version = UInt32(BRTL_CLIENT_PROTOCOL_VERSION)
    /// Disposition du ring partagé, inchangée depuis la v1.
    static let ringLayoutVersion = UInt32(BRTL_RING_LAYOUT_VERSION)
}
