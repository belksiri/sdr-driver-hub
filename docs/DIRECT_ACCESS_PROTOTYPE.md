# Accès direct de Spectrum Analyzer One au pilote RTL-SDR

Prototype du 2026-11-20. Lecture seule : aucun flux IQ, aucune requête de contrôle, librtlsdr inchangée.

Marquage : [J] journal matériel ; [Doc] documentation Apple ; [NT] non testé.

## Preuve consignée

| Point | Ouverture n° 1 | Ouverture n° 2 |
|---|---|---|
| Heure (UTC) | 2026-11-20 21:56:17 | 2026-11-20 21:58:40 |
| PID de Spectrum Analyzer One | 620 | 621 (après réinstallation) |
| Processus SDR Driver Hub | absent à 22:56:15 et 22:56:21 (heure locale) | absent au relevé suivant |
| Processus du pilote | `BelksiriRtlSdrDriver` pid 603 | pid 603 (même instance) |
| Service IOKit trouvé | `BelksiriRtlSdrDriver`, registryEntryID 0x100001244, classe `IOUserService` | idem |
| User client ouvert | `IOServiceOpen` réussi, connexion 0x7907 | réussi, connexion 0x7a0f |
| Version du protocole | 1 | 1 |
| Version / build du pilote | 1.0.0, build 11202231 | idem |
| VID/PID | 0x0bda:0x2838 (bcdDevice 0x0100) | idem |
| Tuner | non transmis par le protocole v1 | idem |
| État du pilote | au repos | au repos |

Entitlements réellement présents dans les signatures : voir le tableau plus bas.

Le prototype a ensuite été retiré de Spectrum Analyzer One : le chemin IQ direct le remplace (`docs/DIRECT_IQ_PATH.md`).

## Montage

- **Code** : `belksiri_spectrum/ios/Runner/HackRF/Plugin/RtlDriverDirectProbe.swift`, appelé au lancement par `AppDelegate`.
  - Ne s'exécute qu'en présence de `Documents/rtl_direct_probe.enable`.
  - Le marqueur est effacé après une exécution.
  - Résultat dans `Documents/rtl_direct_probe.log`.
- **Séquence** :
  1. `IOServiceNameMatching("BelksiriRtlSdrDriver")` ;
  2. `IOServiceGetMatchingService` ;
  3. `IOServiceOpen` ;
  4. `IOConnectCallStructMethod(kBHRFSelProtocolVersion)` ;
  5. `IOServiceClose`.
- **Contexte** :
  - Spectrum Analyzer One n'embarque que `BelksiriHackRFDriver.dext`.
  - Le pilote interrogé appartient au paquet de SDR Driver Hub.
- **Matériel** :
  - iPad Pro 11 pouces (4e génération, M2), iPadOS ;
  - RTL-SDR Realtek RTL2838UHIDIR, tuner R820T.

## Résultat [J]

**Processus sur l'iPad** (`xcrun devicectl device info processes`), filtrés sur SDRDriverHub, Runner et BelksiriRtl :

```
22:56:15 avant le lancement
603   /private/var/db/driverkitd/SystemExtensions/824A7F5D-…/BelksiriRtlSdrDriver.dext/BelksiriRtlSdrDriver
22:56:21 après le lancement
603   /private/var/db/driverkitd/SystemExtensions/824A7F5D-…/BelksiriRtlSdrDriver.dext/BelksiriRtlSdrDriver
620   /private/var/containers/Bundle/Application/C59F9EA3-…/Runner.app/Runner
```

Aucun processus `SDRDriverHub` à aucun des deux instants.

**Journal du prototype** (`rtl_direct_probe.log`) :

```
time=2026-11-20T21:56:17Z client=com.belksiri.spectrum pid=620
service found name=BelksiriRtlSdrDriver registryEntryID=0x100001244
service.IOClass=IOUserService
IOServiceOpen OK connection=0x7907
reply bytes=20 protocolVersion=1 (app expects 1)
reply driverBuildId=11202231
reply idVendor=0x0bda idProduct=0x2838 bcdDevice=0x0100
reply deviceState=idle capabilities=0
tuner identity: not exposed by protocol v1 (probed by librtlsdr in SDR Driver Hub)
DONE — no IQ stream started, no control request sent
```

**Reproduit** à 21:58:40Z, depuis une nouvelle installation (pid 621), avec la même réponse et toujours sans processus SDRDriverHub. Le marqueur a été effacé après cette exécution.

**La réponse vient du dext de SDR Driver Hub :**
- `driverBuildId = 11202231` est l'identifiant compilé dans le pilote RTL-SDR 1.0.0 de SDR Driver Hub. Le journal du compagnon l'affiche aussi : « open — tuner R820T/R820T2/R860 « Realtek RTL2838UHIDIR » driver build 11202231 ».
- `0bda:2838` est le RTL-SDR. Le pilote HackRF de Spectrum ne correspond qu'à `1d50:6089`, et aucun de ses processus ne tournait.
- Seul le processus 603, le pilote RTL-SDR, pouvait répondre : SDR Driver Hub était absent.

**Le pilote survit aux deux apps** : à 22:56:15, le pilote (pid 603) tournait alors que ni SDR Driver Hub ni Spectrum Analyzer One n'étaient lancés.

**Limites** :
- **Identité du tuner** : le protocole v1 ne la porte pas, c'est librtlsdr qui la détecte dans le compagnon.
- **Propriétés du registre** : le prototype en a demandé plusieurs (`IOUserClass`, `CFBundleIdentifier`, `IOUserServerName`, et les noms et identifiants USB du fournisseur). Seule `IOClass` a été lue ; les autres ne sont pas exposées à l'app.

## Entitlements réellement signés (`codesign -d --entitlements`)

Builds de développement, équipe WVQH467NC8, certificat Apple Development de l'équipe.

| Paquet | Entitlements |
|---|---|
| Spectrum Analyzer One `com.belksiri.spectrum` | `com.apple.developer.driverkit.communicates-with-drivers = true`, `get-task-allow`, `application-identifier`, `team-identifier` |
| Pilote HackRF `com.belksiri.spectrum.hackrf-driver` | `com.apple.developer.driverkit = true`, `transport.usb = [{idVendor: "*"}]`, `get-task-allow` |
| SDR Driver Hub `com.belksiri.sdrdriverhub` | `communicates-with-drivers = true`, `get-task-allow` |
| Pilote RTL-SDR `com.belksiri.sdrdriverhub.rtlsdr-driver` | `com.apple.developer.driverkit = true`, `transport.usb = [{idVendor: "*"}]`, `get-task-allow` |

- **Pas d'`allow-third-party-userclients`** : aucun paquet ne le contient, et il n'est pas nécessaire. Apple accepte par défaut un client de la même équipe qui a `communicates-with-drivers` [Doc], et l'essai le confirme [J].
- **Pour la distribution** :
  - `transport.usb` limité à `idVendor 3034` (Realtek), à faire accorder par Apple ;
  - aucun entitlement supplémentaire pour l'accès croisé.

## Protocole proposé : v2, sans rtl_tcp ni audio silencieux

**Principe.**
- Le pilote fait tout : contrôle USB, tuner, flux.
- Spectrum Analyzer One est un client comme l'est aujourd'hui SDR Driver Hub.
- SDR Driver Hub ne sert plus pendant l'écoute : il installe, active, affiche l'état, et fait le test de réception ponctuel.

### Identité : sélecteur 0, inchangé, prolongé

- `protocolVersion = 2`.
- `capabilities` : bit 0 pour le contrôle de haut niveau, bit 1 pour l'identité du tuner disponible.
- Nouveau sélecteur `kSelDeviceInfo` :
  - type de tuner (E4000, FC0012, FC0013, FC2580, R820T, R828D) ;
  - fréquence de cristal ;
  - liste des gains en dixièmes de dB ;
  - bornes de fréquence et d'échantillonnage ;
  - chaînes fabricant et produit, lues par le pilote dans le descripteur USB.

### Commandes de contrôle : sélecteur unique `kSelCommand`

- Une structure fixe `{ uint32 opcode; int64 value; uint32 flags }`, avec un statut en retour.
- Opcodes :
  - `setFrequency(Hz)` ;
  - `setSampleRate(Hz)` ;
  - `setGainMode(auto|manuel)` et `setGain(dixièmes de dB)` ;
  - `setFreqCorrection(ppm)` ;
  - `setAgc(on|off)` ;
  - `setDirectSampling(0|1|2)` ;
  - `setOffsetTuning(on|off)` ;
  - `setBiasTee(on|off)`.
- Les opcodes reprennent un par un les commandes rtl_tcp que Spectrum Analyzer One envoie déjà : son adaptateur actuel se transpose sans changer de sémantique.
- Le regroupement des gains (au plus 15 par seconde, dernière valeur appliquée) passe dans le pilote ou reste côté client. Les tests de `HubCore` s'y transposent.
- **Le pilote reste seul juge de ce qui touche le bus.** Plus aucune requête USB brute n'est accessible au client : `kSelControlRequest` est réservé aux builds de développement.

### Flux IQ : ring partagé existant

- `kSelMapRing`, `IOConnectMapMemory64`, `kSelStartRx` et `kSelStopRx`, inchangés.
- Blocs u8 I/Q entrelacés, comme aujourd'hui.
- Discipline release/acquire déjà décrite dans `bhrf_shared.h`.
- L'app garde son curseur de lecture et compte ses propres pertes.

### Compteurs : `kSelRingStats` prolongé

Trois familles à ne jamais additionner entre elles :
- **Production** : blocs, octets, horodatage de la dernière complétion ;
- **Pertes** : blocs écrasés faute de lecture, erreurs USB, dernier statut USB ;
- **Commandes** : reçues, appliquées, refusées, avec le dernier code.

### Règles de session : à ajouter au pilote

1. **Mort du client** : si le processus client disparaît, le user client reçoit `Stop` et le pilote doit arrêter la réception lui-même. Aujourd'hui, `BelksiriRtlSdrUserClient::Stop` ne le fait pas : c'est l'origine de « startRx refused (usbError(0)) — stopping a stale session » au relancement [J].
2. **Un seul client actif** : si un client est déjà connecté, le suivant reçoit `kBHRFStatusAlreadyStreaming`. La reprise de main doit être un choix explicite, par exemple le test de réception de SDR Driver Hub, qui demande d'abord l'arrêt.
3. **Retour au premier plan** : Spectrum Analyzer One rouvre le user client et rejoue ses réglages. Le pilote ne garde aucun état client.

### Licences

- librtlsdr (GPL-2.0-or-later) passerait dans le dext, qui reste dans le paquet de SDR Driver Hub, avec ses sources correspondantes.
- Spectrum Analyzer One n'embarque toujours aucun code GPL.
- Il faut un avis juridique pour confirmer qu'un user client DriverKit reste une communication entre programmes distincts.

### Étapes suivantes, non commencées

1. Côté pilote :
   - arrêt de la réception à la mort du client, utile dès maintenant ;
   - `kSelDeviceInfo`, avec la détection du tuner faite dans le pilote.
2. Porter le chemin de contrôle de librtlsdr dans le dext : `IOUSBHostDevice::DeviceRequest`, `IOSleep`, `IOMalloc`. Tests hôtes du contrôle sur traces USB enregistrées.
3. Dans Spectrum Analyzer One, une source « RTL-SDR (pilote) » qui réutilise le drain du ring HackRF.
4. Retirer de SDR Driver Hub le serveur rtl_tcp et le mode `audio`, ou les garder seulement pour le développement.
