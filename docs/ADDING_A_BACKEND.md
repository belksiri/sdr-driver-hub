# Ajouter un backend matériel à SDR Driver Hub

SDR Driver Hub possède les pilotes et contrôle les équipements SDR ; Spectrum
Analyzer One reste le logiciel d'analyse et ne reçoit qu'un flux IQ local. Chaque
famille d'équipements est un **backend** qui implémente le contrat
`SdrBackend` (`HubCore/Sources/HubCore/SdrBackend.swift`). L'écran, le
service, le maintien en arrière-plan et le journal ne connaissent que ce
contrat.

## Le contrat

| Élément | Rôle |
|---|---|
| `id: SdrBackendID` | identifiant **stable** (`"rtlsdr"`, futur `"pluto"`) : clé de préférence et de journal, ne change jamais |
| `displayName` | nom public (« RTL-SDR ») |
| `backendVersion` | version du code du backend |
| `availability` | `driverNotEnabled`, `waitingForDevice`, `ready`, `streaming`, `failed(BackendUserError)` |
| `deviceInfo` | produit détecté, détail (tuner…), version du pilote **chargé** |
| `capabilities` | plages de fréquence et de débit, débit sans perte, paliers de gain, AGC, bias-tee, transports IQ |
| `configuration` / `apply(_:)` | réglages radio ; un client connecté reste maître de ses réglages |
| `startService()` / `stopService()` / `isServiceRunning` | le service qui sert les IQ aux clients locaux |
| `statistics()` | octets livrés, erreurs USB, pertes du ring, pertes d'envoi (jamais additionnées entre étages), contenu IQ vérifié, client connecté |
| `lastError: BackendUserError?` | message que l'utilisateur comprend et sait corriger, jamais un code |
| `refresh()` / `onChange` | relecture de l'état matériel, notification de l'écran |
| `runSelfTest(_:)` | courte réception locale contrôlée sur le **contenu** des IQ |

`BackendRegistry` liste les backends ; `HubModel` les enregistre au
démarrage (une ligne par backend). `SpectrumReadiness` dit à l'utilisateur si
Spectrum Analyzer One peut se connecter, et sinon quoi faire.
`HubCore/Tests/HubCoreTests/SdrBackendTests.swift` montre qu'un backend factice
s'insère sans toucher à l'application.

## Étapes pour un nouveau backend

1. Créer `Hub/Backends/<Nom>/` avec une classe qui implémente `SdrBackend`.
2. Choisir le transport IQ vers Spectrum (voir plus bas) et l'ajouter à
   `IqTransportKind` si c'est un nouveau type.
3. L'enregistrer dans `HubModel.init` : `registry.register(...)`.
4. Ajouter ses composants tiers à `THIRD_PARTY_NOTICES.md` et à
   `docs/LICENSE_INVENTORY.md` **avant** tout build distribué.
5. Ajouter des tests HubCore pour sa logique pure (protocole, conversions).
6. Côté Spectrum : une source qui parle le transport choisi, derrière
   `SdrStreamSource` (comme `RtlTcpStreamSource`).

`generate_project.rb` compile automatiquement toutes les sources Swift de
`Hub/`, `Core/` et `HubCore/Sources/HubCore/`.

## Cas d'étude : ADALM-Pluto (non implémenté)

### Connexion : réseau / IIO en priorité

Le Pluto expose ses données par **libiio** :

- en USB, il apparaît comme une **interface réseau RNDIS/ECM**
  (`192.168.2.1` par défaut) **et** comme périphérique USB libiio ;
- il est aussi joignable en Ethernet ou par adaptateur réseau.

La voie prioritaire est **IIO sur réseau** (`ip:<adresse>`, port 30431,
protocole iiod) : elle ne demande **aucun pilote DriverKit** et aucun
entitlement USB. Un backend Pluto serait donc un client iiod en Swift ou en C,
sans dext. Reste à vérifier sur iPadOS qu'une interface RNDIS/ECM d'un
périphérique USB est bien montée par le système (à tester sur matériel).

### Détection sur le réseau local

- Adresse configurée par l'utilisateur, avec `192.168.2.1` proposé par défaut.
- Découverte : iiod s'annonce en **DNS-SD/Bonjour** (`_iio._tcp`) quand avahi
  tourne sur le Pluto ; `NWBrowser` le trouve. Prévoir aussi la saisie manuelle.
- Vérification : lire le contexte IIO, exiger les périphériques
  `ad9361-phy` et `cf-ad9361-lpc`.

### Paramètres

| Réglage | Attribut IIO (ad9361-phy / cf-ad9361-lpc) |
|---|---|
| Fréquence RX | `out_altvoltage0_RX_LO_frequency` (70 MHz – 6 GHz, plage étendue selon firmware) |
| Débit | `in_voltage_sampling_frequency` (≈ 0,52 – 61,44 MS/s ; débit soutenu réel limité par l'USB 2.0, quelques MS/s) |
| Bande passante | `in_voltage_rf_bandwidth` |
| Gain | `in_voltage0_gain_control_mode` (`manual`, `slow_attack`, `fast_attack`, `hybrid`) et `in_voltage0_hardwaregain` |

`BackendCapabilities` porte déjà ces notions (plages, `maxLosslessSampleRateHz`
pour le débit soutenu, paliers de gain, AGC).

### Transport IQ vers Spectrum

Le Pluto livre de l'IQ **complexe 12 bits sur 16 bits signés**. Deux options :

1. **Réutiliser rtl_tcp** (`IqTransportKind.rtlTcp`) en convertissant en u8 :
   zéro changement dans Spectrum, mais perte de dynamique (12 → 8 bits) et
   commandes rtl_tcp à traduire (gain par indice…). Acceptable pour un
   premier jalon.
2. **Transport natif** (nouveau cas, par exemple `.iqStream(port:format:)`,
   IQ s16 + en-tête de métadonnées) et une nouvelle source Spectrum.
   Recommandé ensuite : garde toute la dynamique du AD9361.

Dans les deux cas, garder la boucle locale (`127.0.0.1`) entre le compagnon et
Spectrum, et une file d'envoi bornée qui **compte** ses pertes.

### Dépendances et licences à auditer

| Composant | Licence (à confirmer à la version retenue) | Remarque |
|---|---|---|
| libiio (Analog Devices) | LGPL-2.1-or-later | liaison statique sur iOS : obligations LGPL (permettre la ré-édition des liens) à analyser |
| libad9361-iio | LGPL-2.1-or-later | filtres FIR ; optionnelle |
| libxml2 (dépendance de libiio) | MIT | |
| Implémentation iiod maison en Swift | notre code | évite libiio ; protocole iiod documenté par ADI |

Toute nouvelle dépendance passe par `THIRD_PARTY_NOTICES.md`, l'écran
« Licences open source » et `make_corresponding_source.sh`.

### Permissions iPadOS

- **Réseau local** : se connecter à `192.168.2.1` ou à une adresse du LAN
  déclenche l'autorisation « réseau local » : ajouter
  `NSLocalNetworkUsageDescription` et, pour Bonjour, `NSBonjourServices`
  (`_iio._tcp`). Le service rtl_tcp vers Spectrum, lui, reste en boucle locale
  et n'en a pas besoin.
- **Arrière-plan** : même contrainte que le RTL-SDR (voir
  `docs/APP_STORE_READINESS.md`, mode audio).

### Stratégie USB séparée (éventuelle)

Si l'interface réseau USB n'est pas disponible sur iPadOS, un accès libiio
**USB** demanderait un pilote DriverKit dédié (VID Analog Devices `0x0456`,
PID `0xB673`), son propre entitlement `transport.usb` accordé par Apple pour
ce vendeur, et une liste blanche des requêtes, sur le modèle de
`BelksiriRtlSdrDriver`. C'est un chantier distinct, à n'ouvrir qu'après
l'essai réseau.
