# Chemin IQ direct : Spectrum Analyzer One ↔ pilote RTL-SDR

Implémenté et essayé le 2026-11-21. Rien n'a été envoyé à Apple ; aucun
entitlement de distribution n'a été demandé.

Marquage : [T] test automatisé ; [J] journal matériel ; [V] constat visuel ; [NT] non testé.

## Compatibilité

DriverKit sur iPadOS exige un iPad à puce Apple série M (iPad Pro M1 et
suivants, iPad Air M1 et suivants). Les iPad à puce A, dont l'iPad
10e génération (A14), ne peuvent charger ni ce pilote ni le pilote HackRF de
Spectrum Analyzer One.

## Architecture

```
RTL-SDR ──USB──▶ BelksiriRtlSdrDriver.dext  (paquet SDR Driver Hub)
                  ├─ librtlsdr (inchangée, compilée dans le dext via Driver/dkcompat)
                  ├─ brtl_bus : liste blanche des transferts de contrôle
                  ├─ brtl_client_policy : autorisation, propriétaire unique, bornes
                  └─ ring partagé (lecture seule côté client), transferts bulk asynchrones
                          │  user client, protocole v2 (Driver/brtl_client_protocol.h)
                          ▼
Spectrum Analyzer One (Runner)
  RtlSdrIosSession ─ UnsignedIqConverter (u8 → s8) ─ RingFanOut (HackRF, partagé)
  └─ spectre / démodulation en natif ; Dart ne reçoit que des trames calculées
```

### Protocole client v2

- Versions : `BRTL_CLIENT_PROTOCOL_VERSION = 2`, `BRTL_RING_LAYOUT_VERSION = 1`.
  Tout écart est refusé des deux côtés.
- Sélecteurs : identité (0), mappage du ring, démarrage et arrêt de la
  réception, statistiques, `Acquire` (6), `Release` (7), `Command` (8),
  `DeviceInfo` (9 : tuner, gains, bornes, chaînes USB).
- Commandes : `BRTLCommand` (16 octets), opcodes 1 à 9 — fréquence,
  débit, mode de gain, gain, ppm, AGC RTL, échantillonnage direct, offset
  tuning, bias-tee. Le pilote borne chaque valeur et répond par un
  `BRTLClientResult` (statut, valeur appliquée).
- Flux : blocs de 64 Kio u8 I/Q entrelacés, 16 transferts en vol ; le
  client lit le ring sans jamais y écrire, et compte ses pertes à partir
  des index du producteur.

### Règles

- **Le pilote seul touche le bus** : les clients n'envoient que des
  commandes de haut niveau ; `kSelControlRequest` leur renvoie
  `kIOReturnNotPermitted`.
- **Autorisation** : le pilote lit lui-même les entitlements du client
  (`communicates-with-drivers`, `team-identifier` égal à l'équipe du build).
- **Propriétaire unique** : `Acquire` ; un second client reçoit `busy`. Le
  `Stop` du user client (fermeture ou mort du client) arrête la réception et
  libère le récepteur.
- **Retrait** : le `Stop` du pilote marque le contrôleur perdu ; le client
  voit la fin du flux et rouvre au rebranchement.
- **Séparation** : Spectrum Analyzer One n'embarque que son pilote HackRF et
  une copie identique de l'en-tête de protocole ; aucun code librtlsdr,
  aucune liste blanche commune, identifiants et services distincts. Son seul
  lien vers SDR Driver Hub est le bouton « ouvrir » (`sdrdriverhub://open`)
  affiché quand le pilote est absent.

### Rôle de SDR Driver Hub

Il embarque et met à jour le pilote, guide son activation, diagnostique, et
accueillera les autres backends. Il n'a plus à tourner pendant la réception.

Le serveur rtl_tcp et le maintien audio ne subsistent que dans les builds de
développement (`HUB_DEV_RTLTCP`). Le build App Store (`HUB_DIST=1`) ne compile
ni le serveur, ni le format rtl_tcp (`RtlTcpProtocol.swift`,
`IqSendQueue.swift`), ni le maintien audio, et ne déclare aucun
`UIBackgroundModes` (`verify_bundle.sh --dist`) [T].

## Essai matériel minimal du 2026-11-21 [J]

iPad Pro 11 pouces (M2), RTL-SDR RTL2838UHIDIR / R820T, builds de développement.

### Absence de SDR Driver Hub

Processus relevés par `devicectl device info processes` (26 relevés de
12:56:48 à 12:59:10, heure locale) :

| Instant | Processus |
|---|---|
| après rebranchement | pilote 701 (nouvelle extension), SDR Driver Hub 700, Spectrum 702 |
| 12:56:40, après fermeture des deux apps | pilote 701 seul |
| 12:57:51 | pilote 701, Spectrum 703 (ouvert puis fermé, sans rapport de plantage) |
| 12:58:14 → 12:59:10, réception comprise | pilote 701, Spectrum 704 |
| SDR Driver Hub | **absent des 26 relevés** |

### Flux

Journal de session de Spectrum Analyzer One (`rtl_direct_session.log`,
écrit à l'arrêt) :

| Champ | Valeur |
|---|---|
| Heure | 2026-11-21 11:58:46 UTC |
| Client | `com.belksiri.spectrum`, pid 704 |
| Pilote | `BelksiriRtlSdrDriver`, pid 701, protocole 2 |
| Build du pilote | 11202346 (pilote v2 de SDR Driver Hub 20261120.2346) |
| Réglages | 100 MHz, 2,56 Méch./s, gain automatique |
| Blocs produits par le pilote | 1 218 |
| Octets produits | 79 822 848 (≈ 15,6 s) |
| Blocs perdus | 0 |
| Erreurs USB | 0 |
| Dernier statut USB | `0xE00002EB` (`kIOReturnAborted`) |
| Octets convertis / blocs remis au spectre | 79 822 848 / 1 218 |
| Trames de spectre publiées | 314 |
| Commandes envoyées / refusées | 4 / 0 |

- `0xE00002EB` est l'annulation normale des transferts en vol au moment de
  l'arrêt ; ce n'est pas une erreur, et le compteur d'erreurs USB reste à 0.
- Les blocs viennent du pilote : `blocksProduced` est lu dans le ring du
  dext, le build 11202346 n'existe que dans le paquet de SDR Driver Hub, et
  aucun processus SDR Driver Hub ne tournait.
- Les octets convertis égalent les octets produits : aucun bloc perdu entre
  le pilote et le spectre.
- Le spectre s'est animé pendant la réception [V].

Non rejoué, car déjà validé : WFM, Analyze, New Signature, enregistrement,
changements de fréquence, Stop/Start répétés, HackRF.

## Tests automatisés [T]

- Pilote, tests C hôtes : 79 vérifications sur la politique client
  (autorisation, équipe, propriétaire, sélecteurs, bornes, mort du client)
  et 24 sur le bus (liste blanche, codes d'erreur, retrait).
- HubCore : 23 tests, dont l'accès direct sans service.
- Spectrum Analyzer One, `HackRFIosCoreTests` : 75 tests, dont 17 RTL-SDR
  (ouverture, versions, ring en lecture seule, progression des index,
  pertes, refus `busy`, arrêt pendant une lecture, retrait, reconnexion,
  conversion u8).
- Spectrum Analyzer One, `ProbeCoreTests` 62 (transport HackRF), Dart natif
  47, Flutter 1 217 (routage iPad, non-régression HackRF).

## Limites et risques restants

- **Client d'une autre équipe** : le refus est testé sur la logique
  (`brtl_client_authorize`) [T], **pas sur le matériel** [NT] — il faudrait
  une app signée par une autre équipe.
- **Entitlements de distribution** : non demandés. Il faut, pour le pilote,
  `com.apple.developer.driverkit` et `transport.usb` limité à
  `idVendor 3034` (Realtek, vendeur qui n'appartient pas à Belksiri), puis
  les profils App Store des deux apps.
- **Licence** : avis juridique nécessaire sur la GPL-2.0 dans l'App Store,
  et sur Spectrum Analyzer One en tant que client d'un pilote qui contient
  librtlsdr.
- **Sources correspondantes** : `HUB_SOURCE_URL` n'est pas configurée ;
  l'écran des licences n'affiche alors aucun lien. Blocage avant soumission.
- **Arrière-plan** : la réception suit la vie de Spectrum Analyzer One ; le
  comportement quand iPadOS suspend l'app n'a pas été essayé [NT].
