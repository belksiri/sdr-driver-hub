# SDR Driver Hub 1.0.0 — fiche App Store Connect

**Préparée le 2026-11-23 ; fiche créée le 2026-11-24 (Apple ID
`6838519640`). Aucun build téléversé, rien soumis.** Le téléversement et la
soumission attendent un nouveau GO explicite de Mounir. État réel : § 8.

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

Politique de confidentialité : [`PRIVACY.md`](../../PRIVACY.md) à la racine du
dépôt, bilingue, publiée ; URL : § 10.

## 3. Adresses

| Champ | État |
|---|---|
| URL de la politique de confidentialité | https://github.com/belksiri/sdr-driver-hub/blob/master/PRIVACY.md — (§ 10) |
| URL d'assistance | https://github.com/belksiri/sdr-driver-hub (dépôt public, tickets ouverts) |
| URL marketing (facultative) | laissée vide |
| Code source (dans l'app, écran Licences) | https://github.com/belksiri/sdr-driver-hub/tree/v1.0.0 (`HUB_SOURCE_URL`) |

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

Format : iPad 13 pouces, 2064 × 2752 (portrait). Décision de Mounir : les
captures 1.0.0 sont produites **dans le simulateur uniquement**, sans iPad
physique. Trois écrans réels du build App Store, en anglais et en français,
chargés dans App Store Connect : voir § 12 et
[`screenshots/v1.0.0/`](screenshots/v1.0.0/).

## 6. Informations pour App Review

Coordonnées : enregistrées dans App Store Connect (non reproduites ici). Pas de compte de
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

## 7. Checklist

- [ ] Entitlements DriverKit de distribution accordés (`DRIVERKIT_ENTITLEMENT_REQUEST.md`)
- [ ] Profils App Store de l'application et du pilote
- [x] Propriétaire du dépôt public confirmé ; `HUB_SOURCE_URL` = adresse du tag ; tag `v1.0.0` et archive des sources publiés
- [x] Politique de confidentialité et URL d'assistance publiées
- [x] Fiche créée (autorisation de Mounir), prix Gratuit, catégorie Utilitaires
- [x] Confidentialité : « Aucune donnée collectée » (publiée)
- [x] Coordonnées de revue et notes de revue
- [x] Captures iPad 13 pouces, anglais et français (simulateur ; vidéo de démonstration facultative, non fournie)
- [ ] Archive `HUB_DIST=1` : `verify_bundle.sh --dist` entièrement vert, signatures comprises
- [ ] Avis juridique sur la GPL et l'App Store (`docs/LICENSE_INVENTORY.md`)
- [ ] Téléversement, puis soumission : **GO explicite uniquement**
- [ ] Après création de la fiche : relever `https://apps.apple.com/app/id<identifiant>` pour Spectrum Analyzer One (`HUB_APP_STORE_URL`, passe séparée)

## 8. État de la fiche dans App Store Connect (relevé le 2026-11-24)

Apple ID **6838519640** ; URL App Store prévue
<https://apps.apple.com/app/id6838519640> (active seulement après
publication). SKU `sdr-driver-hub`, plateforme iOS (iPadOS), langue
principale anglais (États-Unis), accès complet.

| Champ | État |
|---|---|
| Nom, sous-titre | EN « SDR Driver Hub », « RTL-SDR driver for iPad » ; FR « SDR Driver Hub », « Pilote RTL-SDR pour iPad » — enregistrés |
| Texte promotionnel, description, mots-clés, URL d'assistance | **anglais et français enregistrés** (§ 4, § 9) |
| Catégorie principale | Utilitaires |
| Version | 1.0.0, publication **manuelle** après approbation |
| Copyright | « 2026 Belksiri » |
| Connexion requise | décochée (l'app n'a pas de compte) |
| Politique de confidentialité | **publiée** : https://github.com/belksiri/sdr-driver-hub/blob/master/PRIVACY.md, saisie pour l'anglais et le français |
| Confidentialité de l'app | **« Données non collectées », publiée** |
| Classification par âge | **4+** (toutes réponses « Non » / « Aucun », § 11) ; équivalents régionaux affichés par Apple pour le Brésil, la Corée du Sud et le Vietnam |
| Droits relatifs au contenu | **Oui** (justification et réserve GPL : § 11) |
| Prix | 0,00 (gratuit) |
| Disponibilité | **174 pays ou régions** ; **Chine continentale exclue** (aucune déclaration AIGC, aucun document chinois) |
| Mac à puce Apple | **désactivé** (« Rendre cette app disponible » décoché, vérifié après rechargement) : l'app est destinée à iPadOS seulement, le pilote n'est pas validé sur macOS |
| Apple Vision Pro | **désactivé** (« Rendre cette app disponible sur Apple Vision Pro » décoché, vérifié après rechargement) : le pilote DriverKit USB n'y est ni compatible ni validé |
| Chiffrement | déclaré par le build (`ITSAppUsesNonExemptEncryption = NO`) ; aucun build envoyé |
| Coordonnées de revue | **enregistrées** et vérifiées après rechargement : Mounir Boti, mboti.dev@gmail.com, téléphone fourni par Mounir (non reproduit ici) |
| Notes pour la revue | **enregistrées** (texte du § 6, 1 271 caractères), vérifiées après rechargement |
| Captures iPad 13 pouces | **3 anglaises et 3 françaises chargées**, emplacement iPad 13 pouces, 2064 × 2752, ordre 1-2-3 vérifié après rechargement ; aucune ne représente une fausse réception matérielle (§ 12, `screenshots/v1.0.0/`) |
| **Build** | **aucun téléversé** ; téléversement et soumission sur GO seulement |
| **Entitlements DriverKit** | demande **`GZ4VQV28Y2` en attente** de réponse d'Apple (`DRIVERKIT_ENTITLEMENT_REQUEST.md` § 5) ; aucune nouvelle demande |
| **GPL** | **non résolue** (`docs/LICENSE_INVENTORY.md`) : la soumission finale reste interdite |

## 9. Localisation française (fr-FR), prête à saisir

À ajouter dans App Store Connect : page de la version › menu de langue ›
« Français ». Les champs « Informations sur l'app » (nom, sous-titre,
URL de confidentialité) sont localisés à part, dans « Informations sur
l'app » › langue « Français ».

| Champ | Valeur | Longueur |
|---|---|---|
| Nom | SDR Driver Hub | 14 / 30 |
| Sous-titre | Pilote RTL-SDR pour iPad | 24 / 30 |
| URL de la politique de confidentialité | même URL que l'anglais (page bilingue, § 10) | — |
| Texte promotionnel | Activez le pilote RTL-SDR une fois, puis écoutez dans Spectrum Analyzer One : inutile de garder SDR Driver Hub ouvert. | 118 / 170 |
| Description | texte **FR** du § 4, tel quel | < 4 000 |
| Mots-clés | `RTL-SDR,SDR,RTL2832U,R820T,pilote,radio,récepteur,USB,clé,spectre,FM` | 68 / 100 |
| URL d'assistance | https://github.com/belksiri/sdr-driver-hub | — |
| URL marketing | vide | — |
| Captures | série française propre, chargée (§ 12) | — |

Les notes pour la revue et les coordonnées ne sont pas localisées.

## 10. Publication de la politique de confidentialité

- Fichier : [`PRIVACY.md`](../../PRIVACY.md), racine du dépôt, anglais puis
  français sur la même page.
- URL stable proposée :
  **https://github.com/belksiri/sdr-driver-hub/blob/master/PRIVACY.md**
  (page publique servie par GitHub, sans activer GitHub Pages ni créer de
  site). Elle suit `master` : chaque correction poussée la met à jour, et la
  date d'effet change avec elle.
- Date d'effet : 24 novembre 2026 ; contact public : mboti.dev@gmail.com
  (autorisé par Mounir le 2026-11-24).
- Ensuite seulement : saisir l'URL dans « Confidentialité de l'app » ›
  « Politique de confidentialité », puis (GO distinct) publier la
  déclaration « Données non collectées ».

## 11. Classification par âge, droits sur le contenu, disponibilité

### Classification par âge (questionnaire d'« Informations sur l'app »)

Toutes les réponses découlent du build App Store vérifié (`verify_bundle.sh
--dist`) : pas de réseau, pas de compte, pas de navigateur intégré (le lien
« Code source » ouvre le navigateur du système), aucun contenu affiché autre
que l'état du récepteur et les licences.

| Groupe | Question | Réponse |
|---|---|---|
| Contrôles intégrés | Contrôle parental | Non |
| | Validation de l'âge | Non |
| Capacités | Accès au Web sans restrictions | Non |
| | Contenu généré par les utilisateurs | Non |
| | Réseaux sociaux (et désactivation pour les moins de 13 ans) | Non |
| | Messagerie et chat | Non |
| | Position partagée | Non |
| | Publicité | Non |
| Thèmes matures | Vulgarité, horreur/peur, alcool-tabac-drogues, rencontres | Aucun |
| Médical ou bien-être | Informations médicales, santé/bien-être | Aucun |
| Sexualité ou nudité | les trois questions | Aucun |
| Violence | les quatre questions | Aucun |
| Activités basées sur le hasard | jeux de hasard, simulation, concours, coffres à butin | Non / Aucun |

Résultat attendu : **4+** dans tous les pays.

### Droits relatifs au contenu

Question d'Apple : l'app contient-elle, affiche-t-elle ou consulte-t-elle du
contenu tiers ? **Réponse décidée par Mounir le 2026-11-24 : Oui** (l'app
dispose des droits nécessaires).

Justification interne : SDR Driver Hub contient des composants logiciels
tiers open source, notamment librtlsdr. Leur utilisation et leur
distribution sont autorisées par leurs licences respectives. Les notices,
licences et sources correspondantes sont publiées avec l'application.

Cette réponse **ne signifie pas** que la compatibilité entre la GPL-2.0 et
les conditions de l'App Store est juridiquement résolue : ce blocage reste
ouvert (`docs/LICENSE_INVENTORY.md`) et interdit toujours la soumission
finale.

### Disponibilité

- **Tous les pays et régions sauf la Chine continentale** pour la version
  1.0.0 (décision de Mounir, 2026-11-24). Aucune déclaration AIGC : l'app
  n'a aucune fonction d'IA générative. Aucun document ICP ni évaluation de
  sécurité demandé pour cette version.
- Raison : App Store Connect affiche, pour toute nouvelle app distribuée en
  Chine continentale, deux exigences immédiates : un justificatif
  d'**évaluation de sécurité** avec rapport, et une **déclaration de services
  AIGC** avec informations de dépôt. Aucun de ces documents n'existe.
- Précommande : non. Distribution publique (pas privée). Apple School
  Manager / Business : laisser les valeurs par défaut.

## 12. Captures App Store (simulateur)

Le simulateur iPad n'a ni DriverKit ni USB : le build simulateur ne contient
pas le pilote. Les captures montrent donc les écrans réels du build App Store
(`HUB_DIST=1`) dans cet état, sans pilote chargé, sans RTL-SDR, sans
réception, sans débit IQ, sans vérification réussie et sans écran de
Réglages fictif. Ni le code ni les images n'ont été modifiés pour simuler un
état matériel.

- Simulateur : iPad Pro 13 pouces (M5), iOS 26.5 ; langue et région du
  simulateur en anglais (en_US) puis en français (fr_FR), date de la barre
  d'état dans la langue de la série ; barre d'état 9:41 / 09:41, Wi-Fi,
  batterie pleine ; aucune notification ni donnée personnelle.
- Captures natives 2064 × 2752 ; compositions finales 2064 × 2752, PNG sRGB
  sans transparence : fond, titre, sous-titre, capture réelle réduite
  uniformément (ni étirée, ni recadrée, ni retouchée).

| Ordre | Écran réel | Titre EN | Titre FR |
|---|---|---|---|
| 1 | Écran principal, état « aucun RTL-SDR détecté » | RTL-SDR support for iPad | Le pilote RTL-SDR pour iPad |
| 2 | À propos : version 1.0.0, matériel RTL-SDR, Spectrum Analyzer One ouvre le pilote directement | Connect Spectrum Analyzer One | Connectez Spectrum Analyzer One |
| 3 | Licences open source : librtlsdr, GPL v2, lien Code source | Open-source and transparent | Open source et transparent |

Chargement App Store Connect (2026-11-24) : 3 images en anglais (États-Unis)
et 3 en français, emplacement iPad 13 pouces, ordre 1-2-3 vérifié après
rechargement. Aucun build téléversé, aucune soumission.

Fichiers conservés : [`screenshots/v1.0.0/`](screenshots/v1.0.0/) (`final/`,
`contact-sheet.png`, `MANIFEST.md`, `manifest.json` avec les SHA-256). Les
captures brutes ne sont pas versionnées.

Non produits, faute d'écran correspondant dans l'app : un écran de
confidentialité (la mention « aucune donnée personnelle collectée » figure
dans le sous-titre de la capture 3) et un écran « Compatibilité » distinct
(le matériel pris en charge est dans À propos). Les écrans qui exigeraient
du matériel réel (pilote prêt, réception, vérification réussie, Réglages ›
Pilotes) ne sont pas représentés.
