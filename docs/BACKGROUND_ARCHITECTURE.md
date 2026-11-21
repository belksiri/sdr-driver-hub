# Fonctionner sans maintien en arrière-plan par son silencieux

Étude du 2026-11-20. Aucune implémentation.

Marquage : [Doc] documentation Apple ; [J] journal matériel ; [NT] non testé.

## Le problème

Aujourd'hui, Spectrum Analyzer One est au premier plan pendant l'écoute. SDR Driver Hub est donc en arrière-plan, et iPadOS le suspend. Or c'est le compagnon qui exécute librtlsdr (réglage du tuner) et le serveur rtl_tcp. Pour qu'il ne soit pas suspendu, il joue du silence avec le mode d'arrière-plan `audio` (`Hub/Support/BackgroundKeepAlive.swift`).

La règle App Store 2.5.4 réserve ce mode à un audio audible pour l'utilisateur. Le risque de refus est élevé.

## Faits établis

1. **Un pilote peut servir une autre app de la même équipe** [Doc]. Page Apple `com.apple.developer.driverkit.allow-third-party-userclients` : « By default, an iPadOS driver accepts user-client connections from apps signed with the same team ID and the `communicates-with-drivers` entitlement ». L'entitlement `allow-third-party-userclients` n'est nécessaire que pour des apps d'une *autre* équipe. Spectrum Analyzer One a déjà `communicates-with-drivers`, pour son pilote HackRF, et la même équipe (WVQH467NC8).
2. **Le pilote ne dépend pas du cycle de vie de l'app** [J]. Le compagnon a été tué alors qu'un flux était en cours. Au lancement suivant, le pilote avait gardé la session ouverte : c'est le cas « stale session » géré par `RtlDevice.startRx`, qui fait plusieurs tentatives. Le pilote vit tant que le RTL-SDR est branché et le pilote activé.
3. **Le protocole du pilote est déjà générique** (`Driver/bhrf_shared.h`). Spectrum Analyzer One parle déjà ce protocole avec son pilote HackRF : ouverture du user client, ring IQ partagé, commandes.

4. **Contrainte matérielle, commune à toutes les options** : DriverKit sur iPadOS exige un iPad à puce Apple série M. L'iPad 10e génération (A14) n'est pas compatible.

## Options, par ordre de recommandation

### 1. Réception pilotée par le pilote lui-même (recommandée)

**Principe.**
- Le réglage du RTL-SDR (fréquence, échantillonnage, gain, correction de fréquence) passe dans le pilote : librtlsdr y est compilé, ses transferts de contrôle libusb sont remplacés par `IOUSBHostDevice::DeviceRequest`.
- Spectrum Analyzer One ouvre directement le pilote RTL-SDR, comme il ouvre déjà le pilote HackRF : commandes et ring IQ partagé. Il n'a plus besoin du serveur rtl_tcp.
- SDR Driver Hub devient une app de configuration :
  - il embarque et active le pilote ;
  - il affiche l'état, les licences et un test de réception ;
  - il n'a plus besoin de tourner pendant l'écoute ;
  - il n'utilise plus aucun mode d'arrière-plan.

**Avantages.**
- Le mode `audio` disparaît, et avec lui le risque 2.5.4.
- Plus de serveur TCP, même local.
- Débit meilleur ou égal : aucune copie réseau.
- L'architecture devient la même que pour le HackRF.

**Licences.**
- librtlsdr (GPL) reste dans le paquet de SDR Driver Hub, à l'intérieur du pilote.
- Spectrum Analyzer One n'embarque toujours aucun code GPL : il communique avec un programme distinct par des commandes et des échantillons.
- Il faut faire confirmer par un juriste qu'un user client DriverKit reste une communication entre programmes distincts, au même titre que le socket rtl_tcp actuel. Ce point rejoint le blocage juridique déjà listé.

**Coûts et risques.**
- Porter le chemin de contrôle de librtlsdr dans l'environnement restreint de DriverKit :
  - pas de libusb ;
  - libc partielle ;
  - attentes via `IOSleep`.
  - Le flux IQ, lui, est déjà dans le pilote.
- Un plantage du tuner fait redémarrer le pilote, et non l'app.
- Un client à la fois, à arbitrer dans le pilote : c'est déjà le cas pour le HackRF.
- Côté Spectrum Analyzer One, une source « RTL-SDR (pilote) » vient réutiliser le pont natif HackRF.

**Essai 1 réussi le 2026-11-20 [J]** : Spectrum Analyzer One a ouvert le pilote et lu son identité alors que SDR Driver Hub était absent (`docs/DIRECT_ACCESS_PROTOTYPE.md`, avec le protocole v2 proposé). L'essai 2 reste à faire.

**À valider d'abord : un essai court.**
1. Spectrum Analyzer One ouvre le user client `BelksiriRtlSdrDriver` alors que SDR Driver Hub est terminé, et lit l'identité de l'appareil.
2. Une commande de contrôle simple émise depuis le pilote (lecture d'un registre RTL2832U) réussit.

Si l'essai 1 échoue, l'option 1 tombe et l'option 2 devient la cible.

### 2. Service seulement pendant que le compagnon est visible

**Principe.**
- Aucun mode d'arrière-plan.
- Le service ne tourne que pendant que SDR Driver Hub est actif à l'écran : Split View, Slide Over ou Stage Manager à côté de Spectrum Analyzer One.
- Spectrum Analyzer One l'explique quand le flux s'arrête.

**Coûts.**
- Presque aucun code : retirer `BackgroundKeepAlive`, ajouter le message.
- Ergonomie nettement dégradée : le compagnon occupe une partie de l'écran pendant toute l'écoute.
- Le comportement d'une app en Slide Over masquée est à vérifier [NT].

**Usage.** Solution de repli pour soumettre la 1.0 sans attendre l'option 1.

### 3. Intégrer le RTL-SDR dans Spectrum Analyzer One (écartée)

Cette option obligerait :
- soit à placer tout Spectrum Analyzer One sous GPL ;
- soit à réécrire, sans s'appuyer sur librtlsdr, le pilotage du RTL2832U et du R820T, une réimplémentation longue et risquée.

C'est précisément ce que la séparation en deux apps évite.

### 4. Garder le son silencieux (écartée pour l'App Store)

Elle fonctionne techniquement [J], mais le refus à la revue est probable (2.5.4). On ne peut la garder que pour les builds de développement.

## Recommandation

1. Faire l'essai de l'option 1, environ une demi-journée.
2. S'il réussit, migrer vers l'option 1 avant la soumission. rtl_tcp peut rester disponible en option de développement.
3. S'il échoue, soumettre avec l'option 2 et retirer le mode `audio` de `Info.plist`.
