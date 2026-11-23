# SDR Driver Hub 1.0.0 — fiche App Store Connect

**Préparée le 2026-11-23. Aucune fiche n'a été créée, rien n'a été
téléversé ni soumis.** La création de la fiche attend l'autorisation
explicite de Mounir ; la soumission à la revue, son GO final.

## 1. Identité

| Champ | Valeur |
|---|---|
| Nom | **SDR Driver Hub** |
| Sous-titre (≤ 30) | EN « RTL-SDR driver for iPad » · FR « Pilote RTL-SDR pour iPad » |
| Bundle ID | `com.belksiri.sdrdriverhub` |
| SKU proposé | `sdr-driver-hub` |
| Plateforme | iPadOS seulement (iPad à puce M ; `UIRequiredDeviceCapabilities = DriverKit`) |
| Version | 1.0.0 (build `AAAAMMJJ.HHMM`, toujours croissant ; aucun build déjà envoyé) |
| Prix | **Gratuit** — aucun achat intégré, aucun abonnement, aucune publicité |
| Catégorie principale | Utilitaires |
| Classification | 4+ (aucun contenu à déclarer) |
| Chiffrement | exempté (`ITSAppUsesNonExemptEncryption = NO`) |

## 2. Confidentialité (« App Privacy »)

Réponse proposée : **Data Not Collected / Aucune donnée collectée.**

Vérifié dans le build App Store : aucun réseau (Network.framework non lié,
aucune autorisation réseau local), aucun suivi, aucun SDK tiers,
`PrivacyInfo.xcprivacy` vide (aucune donnée, aucune API à raison
déclarée), journal en mémoire seulement, aucun fichier écrit.

Politique de confidentialité : projet dans [`PRIVACY_POLICY.md`](PRIVACY_POLICY.md),
à publier à une adresse stable.

## 3. Adresses à fournir

| Champ | État |
|---|---|
| URL de la politique de confidentialité | **à fournir** (publication de `PRIVACY_POLICY.md`) |
| URL d'assistance | **à fournir** — candidat : la page du dépôt public, une fois son propriétaire confirmé |
| URL marketing (facultative) | **à fournir** ou laisser vide |
| Code source (dans l'app, écran Licences) | `HUB_SOURCE_URL`, voir `docs/CORRESPONDING_SOURCE.md` |

Aucune de ces adresses n'est inventée ici.

## 4. Description

**EN**

> SDR Driver Hub adds RTL-SDR support to Spectrum Analyzer One on iPad.
>
> It installs a USB driver for RTL-SDR receivers (Realtek RTL2832U with
> R820T/R82xx tuners, such as the RTL-SDR Blog V3). Once the driver is turned
> on, Spectrum Analyzer One opens your RTL-SDR directly: SDR Driver Hub does
> not need to stay open while you listen.
>
> • Receive only: the driver tunes the receiver and reads its samples; it
>   never transmits, never writes the receiver's EEPROM and never changes its
>   firmware.
> • One app at a time: the driver opens only to Spectrum Analyzer One and to
>   SDR Driver Hub's own reception check.
> • Check reception: a 1.5-second test at 88.8 MHz verifies the real IQ
>   content of your receiver.
> • Free, no ads, no in-app purchase, no data collected.
>
> Requires an iPad with an M-series chip (DriverKit), an RTL-SDR receiver
> connected to the USB-C port, and Spectrum Analyzer One for analysis.
>
> SDR Driver Hub includes librtlsdr (osmocom rtl-sdr), free software under the
> GNU GPL version 2 or later; its complete source code is linked from the app.

**FR**

> SDR Driver Hub ajoute la prise en charge du RTL-SDR à Spectrum Analyzer One
> sur iPad.
>
> Il installe un pilote USB pour les récepteurs RTL-SDR (Realtek RTL2832U avec
> tuner R820T/R82xx, comme le RTL-SDR Blog V3). Une fois le pilote activé,
> Spectrum Analyzer One ouvre directement votre RTL-SDR : SDR Driver Hub n'a
> pas besoin de rester ouvert pendant l'écoute.
>
> • Réception seule : le pilote règle le récepteur et lit ses échantillons ; il
>   n'émet jamais, n'écrit jamais dans l'EEPROM du récepteur et ne modifie
>   jamais son firmware.
> • Une seule app à la fois : le pilote ne s'ouvre qu'à Spectrum Analyzer One
>   et à la vérification de réception de SDR Driver Hub.
> • Vérifier la réception : un essai de 1,5 s à 88,8 MHz contrôle le contenu
>   IQ réel de votre récepteur.
> • Gratuit, sans publicité, sans achat intégré, sans collecte de données.
>
> Nécessite un iPad à puce M (DriverKit), un récepteur RTL-SDR branché sur le
> port USB-C, et Spectrum Analyzer One pour l'analyse.
>
> SDR Driver Hub inclut librtlsdr (osmocom rtl-sdr), logiciel libre sous
> licence GNU GPL version 2 ou ultérieure ; son code source complet est
> accessible depuis l'app.

**Mots-clés (≤ 100 caractères)**

- EN : `RTL-SDR,SDR,RTL2832U,R820T,DriverKit,radio,receiver,USB,dongle,spectrum,FM`
- FR : `RTL-SDR,SDR,RTL2832U,R820T,pilote,radio,récepteur,USB,clé,spectre,FM`

**Texte promotionnel (≤ 170)** — EN « Turn on the RTL-SDR driver once, then
listen in Spectrum Analyzer One: no need to keep SDR Driver Hub open. » ·
FR « Activez le pilote RTL-SDR une fois, puis écoutez dans Spectrum Analyzer
One : inutile de garder SDR Driver Hub ouvert. »

## 5. Captures iPad

Format exigé : iPad 13 pouces (2064 × 2752 ou 2048 × 2732, portrait ou
paysage). Aucune nouvelle capture ne peut être faite pour cette passe (iPad
indisponible). À produire :

1. Écran principal, pilote prêt (« Prêt : Spectrum Analyzer One peut se
   connecter »).
2. Écran principal pendant la réception par Spectrum Analyzer One.
3. Vérification de la réception réussie (contenu IQ « Vérifié »).
4. Licences open source (librtlsdr, GPL, lien Code source).
5. Réglages › Apps › SDR Driver Hub › Pilotes (activation du pilote).

## 6. Informations pour App Review

Coordonnées : à remplir par Mounir (non reproduites ici). Pas de compte de
démonstration : l'app n'a pas de compte. **Une vidéo de démonstration est
recommandée**, l'examen demandant un RTL-SDR.

> **Notes for App Review**
>
> SDR Driver Hub adds RTL-SDR support to our app Spectrum Analyzer One
> (`com.belksiri.spectrum`, same team). It contains one DriverKit driver for
> RTL-SDR USB receivers (Realtek RTL2832U, vendor ID 0x0BDA).
>
> - The driver manages the USB device. Spectrum Analyzer One reads the IQ
>   samples by opening the driver's user client directly; the driver only
>   accepts clients signed by our team with the communicates-with-drivers
>   entitlement, one at a time.
> - SDR Driver Hub does not need to stay open during reception. It has no
>   background mode, no network server, and no audio keep-alive.
> - The driver is receive only: no transmission, no EEPROM write, no firmware
>   change.
> - No data is collected; the app makes no network connection.
>
> **How to test** (requires an RTL-SDR receiver, e.g. RTL-SDR Blog V3, and an
> iPad with an M-series chip):
> 1. Open SDR Driver Hub once.
> 2. Settings › Apps › SDR Driver Hub › Drivers: turn the RTL-SDR driver on.
> 3. Plug the RTL-SDR into the iPad's USB-C port. SDR Driver Hub shows
>    "Ready: Spectrum Analyzer One can connect."
> 4. Tap "Check reception": a 1.5-second capture at 88.8 MHz reports the IQ
>    content as "Verified".
> 5. Optionally, close SDR Driver Hub, open Spectrum Analyzer One and select
>    RTL-SDR: the spectrum is received directly from the driver.
>
> A demonstration video is attached.

## 7. Checklist

- [ ] Entitlements DriverKit de distribution accordés (`DRIVERKIT_ENTITLEMENT_REQUEST.md`)
- [ ] Profils App Store de l'application et du pilote
- [ ] Propriétaire du dépôt public confirmé ; `HUB_SOURCE_URL` = adresse du tag ; tag `v1.0.0` et archive des sources publiés
- [ ] Politique de confidentialité et URL d'assistance publiées
- [ ] Fiche créée (autorisation de Mounir), prix Gratuit, catégorie Utilitaires
- [ ] Confidentialité : « Aucune donnée collectée »
- [ ] Captures iPad 13 pouces et vidéo de démonstration
- [ ] Archive `HUB_DIST=1` : `verify_bundle.sh --dist` entièrement vert, signatures comprises
- [ ] Avis juridique sur la GPL et l'App Store (`docs/LICENSE_INVENTORY.md`)
- [ ] Téléversement, puis soumission : **GO explicite uniquement**
- [ ] Après création de la fiche : relever `https://apps.apple.com/app/id<identifiant>` pour Spectrum Analyzer One (`HUB_APP_STORE_URL`, passe séparée)
