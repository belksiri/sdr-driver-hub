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

## 3. Adresses à fournir

| Champ | État |
|---|---|
| URL de la politique de confidentialité | https://github.com/belksiri/sdr-driver-hub/blob/master/PRIVACY.md — (§ 10) |
| URL d'assistance | https://github.com/belksiri/sdr-driver-hub (dépôt public, tickets ouverts) |
| URL marketing (facultative) | **à fournir** ou laisser vide |
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

## 8. État de la fiche dans App Store Connect (2026-11-24)

Apple ID **6838519640** ; URL App Store prévue
<https://apps.apple.com/app/id6838519640> (active seulement après
publication). SKU `sdr-driver-hub`, plateforme iOS, langue principale
anglais (États-Unis), accès complet.

| Champ | État |
|---|---|
| Nom, sous-titre EN | saisis (« SDR Driver Hub », « RTL-SDR driver for iPad ») |
| Catégorie principale | Utilitaires |
| Version | 1.0.0, publication **manuelle** après approbation |
| Texte promotionnel, description, mots-clés EN | saisis (§ 4) |
| URL d'assistance | https://github.com/belksiri/sdr-driver-hub |
| Copyright | « 2026 Belksiri » (en-têtes des sources) |
| Connexion requise | décochée (l'app n'a pas de compte) |
| Prix | 0,00 dans 175 pays ou régions |
| Confidentialité | « Données non collectées » enregistré ; **non publié** : URL de la politique manquante |
| Chiffrement | déclaré par le build (`ITSAppUsesNonExemptEncryption = NO`), aucun build envoyé |
| **URL de la politique de confidentialité** | **manquante** : compléter `PRIVACY.md` (`[DATE]`, `[CONTACT]`), le pousser, puis saisir l'URL du § 10 |
| **Coordonnées de revue** (prénom, nom, téléphone, e-mail) | **à saisir par Mounir** ; tant qu'elles manquent, ASC refuse d'enregistrer les notes de revue (texte prêt, § 6, sans la phrase sur la vidéo) |
| **Classification par âge** | non saisie ; réponses prêtes (§ 11) |
| **Droits relatifs au contenu** | non saisis ; réponse proposée « Non », à confirmer (§ 11) |
| **Disponibilité par pays** | non saisie ; proposé : tous sauf la Chine continentale (§ 11) |
| **Localisation française** | non saisie ; prête (§ 9) |
| **Captures iPad 13 pouces** | aucune (§ 5) |
| Build | aucun ; téléversement sur GO seulement |

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
| Captures | les mêmes que l'anglais tant qu'il n'existe pas de captures en français (§ 12) | — |

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

## 12. Captures : simulateur ou iPad réel

Le simulateur iPad n'a ni DriverKit ni USB : le build simulateur ne contient
même pas le pilote. Tout écran qui montre un pilote prêt, une réception ou un
contenu IQ vérifié exige un vrai iPad à puce M avec un RTL-SDR ; le simuler
serait faux.

| Capture | Simulateur (honnête) | iPad réel requis |
|---|---|---|
| Écran principal « Aucun RTL-SDR détecté » | **oui**, faite : `build/appstore-captures/simulator/` (2064 × 2752, EN et FR, build `HUB_DIST=1`, iPad Pro 13 pouces M5, iOS 26.5) | — |
| Licences open source (librtlsdr, GPL, lien Code source) | **oui** (écran statique) ; à prendre à la main dans Simulator, aucun outil de tap automatique installé | — |
| À propos | **oui**, même méthode | — |
| Écran principal « Prêt : Spectrum Analyzer One peut se connecter » | non | **oui** |
| Écran principal pendant la réception par Spectrum Analyzer One | non | **oui** |
| Vérification de la réception réussie (« Vérifié ») | non | **oui** |
| Réglages › Apps › SDR Driver Hub › Pilotes | non (aucun pilote dans le simulateur) | **oui** |

Réserves sur les captures du simulateur :

- la barre d'état affiche la date en français même pour l'app en anglais :
  pour des captures EN propres, régler aussi la langue du simulateur en
  anglais (Réglages › Général › Langue) avant de les refaire ;
- l'écran « Aucun RTL-SDR détecté » est un état réel mais d'erreur : utile en
  complément, insuffisant seul pour montrer la fonction de l'app (règle 2.3
  de la revue : les captures montrent l'app en usage).

Minimum recommandé pour soumettre : les 4 captures « iPad réel requis »
plus Licences.
