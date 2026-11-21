# SDR Driver Hub 1.0.0 — état de préparation App Store

Relevé le 2026-11-20. Rien n'a été téléversé ni soumis ; aucune demande n'a
été faite à Apple.

## Prêt

| Élément | État |
|---|---|
| Identité | `com.belksiri.sdrdriverhub`, pilote `com.belksiri.sdrdriverhub.rtlsdr-driver`, schéma `sdrdriverhub://` (identifiants déjà provisionnés, inchangés) |
| Nom | « SDR Driver Hub » (nom affiché et sous l'icône, en anglais et en français) |
| Version | marketing `1.0.0` ; build de l'app `AAAAMMJJ.HHMM`, toujours croissant ; pilote `1.0.0` (format imposé aux extensions : un build daté y est refusé par le système) |
| Appareils | iPad seulement, iPadOS 16.0 minimum, quatre orientations. DriverKit sur iPadOS exige un iPad à puce Apple série M (iPad Pro M1 et suivants, iPad Air M1 et suivants) : `UIRequiredDeviceCapabilities = DriverKit` empêche l'installation ailleurs. L'iPad 10e génération (A14) et les autres iPad à puce A ne sont **pas** compatibles, ni avec ce pilote ni avec le pilote HackRF de Spectrum Analyzer One |
| Icône | source 1024 × 1024 sRGB sans transparence ni coins arrondis (`Design/AppIcon/`), planche de contrôle `Design/AppIcon/AppIcon-control-sheet.png` |
| Autorisations | aucune autorisation demandée ; aucun service réseau dans le build App Store (le serveur rtl_tcp de développement n'écoute que sur 127.0.0.1) |
| Confidentialité | `PrivacyInfo.xcprivacy` : aucun suivi, aucune donnée collectée, aucune API à raison déclarée |
| Chiffrement | `ITSAppUsesNonExemptEncryption = NO` |
| Journal | désactivé par défaut (mémoire seulement) ; fichier uniquement sur marqueur de diagnostic |
| Paquet | un seul dext, aucun framework embarqué, dépendances système uniquement, aucun code HackRF, aucun chemin, UDID, e-mail ni adresse IP (`scripts/verify_bundle.sh`) |
| Licences | notices et GPL embarquées, écran « Licences open source », archive de sources reproductible (`scripts/make_corresponding_source.sh`) |

## Blocages avant soumission

1. **Entitlements DriverKit de distribution** — le compte n'a que des profils
   de développement. Il faut demander à Apple, pour
   `com.belksiri.sdrdriverhub.rtlsdr-driver`, `com.apple.developer.driverkit`
   et `com.apple.developer.driverkit.transport.usb` limité à `idVendor 3034`
   (Realtek), puis un profil App Store pour l'app et le pilote. Le vendeur
   0x0BDA n'appartient pas à Belksiri : Apple peut demander une justification
   ou l'accord du fabricant. **Demande non faite (ARRÊT).**
2. **Licence GPL et App Store** — trois points demandent un avis juridique :
   étendue du copyleft à l'application, compatibilité des conditions de
   l'App Store avec la GPL-2.0 § 6, licence du code Belksiri
   (`docs/LICENSE_INVENTORY.md`).
3. **Adresse des sources correspondantes** — `HUB_SOURCE_URL` est vide :
   l'écran des licences n'a pas de lien « Code source ». À fournir avant
   l'archive soumise.
4. **Arrière-plan par un son silencieux — levé dans le code.** Spectrum
   Analyzer One ouvre désormais le pilote directement (protocole client v2,
   `docs/DIRECT_IQ_PATH.md`) : SDR Driver Hub n'a plus à tourner pendant
   l'écoute. Le build App Store (`HUB_DIST=1`) ne contient ni serveur
   rtl_tcp, ni format rtl_tcp, ni maintien audio, ni `UIBackgroundModes`
   (vérifié le 2026-11-21 : `verify_bundle.sh --dist`, Network.framework et
   AVFAudio non liés, aucune chaîne « RTL0 » ni symbole `RtlTcp`). Le serveur
   et le maintien ne subsistent que dans les builds de développement.
5. **Métadonnées App Store Connect** — catégorie (proposée : Utilitaires,
   secondaire Outils de développement), description, captures iPad, URL de
   support, politique de confidentialité, informations de revue (le RTL-SDR
   est nécessaire pour tester : prévoir une vidéo de démonstration).
6. **Validation Apple de la version du pilote** — le pilote est en `1.0.0`
   alors que l'app porte un build daté : à confirmer au premier envoi à App
   Store Connect (aucun envoi fait).
