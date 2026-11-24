# SDR Driver Hub 1.0.0 — état de préparation App Store

Relevé le 2026-11-24. Rien n'a été téléversé ni soumis. Demande DriverKit
envoyée (`GZ4VQV28Y2`) ; fiche App Store Connect créée (Apple ID
`6838519640`), sans build.

SDR Driver Hub est publié **avant** Spectrum Analyzer One, gratuitement, sans
achat intégré ni abonnement. Fiche, revue et demande d'entitlements :
[`docs/appstore/`](appstore/).

## Prêt

| Élément | État |
|---|---|
| Identité | `com.belksiri.sdrdriverhub`, pilote `com.belksiri.sdrdriverhub.rtlsdr-driver`, schéma `sdrdriverhub://` (identifiants provisionnés en développement, inchangés) |
| Nom | « SDR Driver Hub » (nom affiché et sous l'icône, en anglais et en français) |
| Version | marketing `1.0.0` ; build de l'app `AAAAMMJJ.HHMM`, toujours croissant (aucun build encore envoyé) ; pilote `1.0.0` (format imposé aux pilotes) |
| Appareils | iPad seulement, iPadOS 16.0 minimum, quatre orientations ; `UIRequiredDeviceCapabilities = DriverKit` (iPad à puce M). L'iPad 10e génération (A14) et les autres iPad à puce A ne sont **pas** compatibles |
| Matériel | Realtek 0x0BDA:0x2838 et 0x0BDA:0x2832 seulement (RTL2832U, tuners R820T/R82xx) ; réception seule ; aucune écriture EEPROM ni modification de firmware (liste blanche `brtl_policy`) |
| Paquet App Store (`HUB_DIST=1`) | un seul dext ; ni serveur rtl_tcp, ni format rtl_tcp, ni `IqTransportKind.rtlTcp`, ni maintien audio, ni `UIBackgroundModes`, ni Network.framework, ni AVFAudio, ni journal sur fichier, ni texte de l'écran Development ; aucune mention de rtl_tcp, de service local ou de 127.0.0.1 ; aucun code ni identifiant HackRF (`scripts/verify_bundle.sh --dist`) |
| Signature locale | build App Store signé avec les profils de **développement** : les 38 contrôles passent, `transport.usb` signé limité à `idVendor 3034` |
| Confidentialité | `PrivacyInfo.xcprivacy` : aucun suivi, aucune donnée collectée ; aucune connexion réseau |
| Chiffrement | `ITSAppUsesNonExemptEncryption = NO` |
| Licences | tout le dépôt est GPL-2.0-or-later (`LICENSE`) ; notices et GPL embarquées, écran « Licences open source » avec lien « Code source » ; archive de sources reproductible, reconstruite seule (sans Git) le 2026-11-23 ; provenance de librtlsdr vérifiée contre l'amont |
| Traductions | français complet pour tous les textes du build App Store |

## Blocages avant soumission

1. **Entitlements DriverKit de distribution** — le compte n'a que des profils
   de développement (aucun profil App Store pour l'app ni pour le pilote).
   Demande **envoyée le 2026-11-24**, identifiant `GZ4VQV28Y2`, en attente
   de réponse : [`appstore/DRIVERKIT_ENTITLEMENT_REQUEST.md`](appstore/DRIVERKIT_ENTITLEMENT_REQUEST.md) § 5.
   Le vendeur 0x0BDA n'appartient pas à Belksiri.
2. **Dépôt public — levé.** Dépôt officiel `belksiri/sdr-driver-hub`
   (confirmé le 2026-11-24) ; `HUB_SOURCE_URL` =
   <https://github.com/belksiri/sdr-driver-hub/tree/v1.0.0>, tag `v1.0.0` et
   archive des sources publiés avec la release GitHub `v1.0.0`.
3. **Licence GPL et App Store** — points **[AVIS JURIDIQUE]** restants
   (`LICENSE_INVENTORY.md`) : App Store et GPL-2.0 § 6, signature Apple,
   Spectrum Analyzer One client du pilote.
4. **Fiche App Store Connect** — créée ; restent l'URL de la politique de
   confidentialité (à publier), les coordonnées de revue, la classification
   par âge, les droits sur le contenu, la disponibilité par pays, la
   localisation française et les captures iPad 13 pouces
   ([`appstore/APP_STORE_CONNECT.md`](appstore/APP_STORE_CONNECT.md) § 8 ;
   préparés : localisation française § 9, politique `PRIVACY.md` et son URL
   § 10, classification, droits et disponibilité § 11, captures § 12).
5. **Version du pilote** — `1.0.0` alors que l'app porte un build daté : à
   confirmer au premier envoi à App Store Connect.

## Essais

- Tests automatisés : pilote (C, 79 + 24 vérifications), HubCore (23 tests).
- Matériel : essai du 2026-11-21 sur iPad Pro 11 pouces (M2), clé RTL2832U /
  R820T, builds de développement (`DIRECT_IQ_PATH.md`). **Aucun nouvel essai
  iPad pour cette passe** : l'iPad n'est plus disponible ; les validations
  antérieures restent valables pour le code inchangé du pilote et du
  protocole direct.
