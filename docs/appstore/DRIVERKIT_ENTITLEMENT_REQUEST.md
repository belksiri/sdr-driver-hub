# Demande d'entitlements DriverKit — SDR Driver Hub 1.0.0

**Préparée le 2026-11-23 ; envoyée le 2026-11-24, demande Apple
`GZ4VQV28Y2`, en attente de réponse.** Le texte réellement envoyé est au § 5 ;
le § 3 en est la version longue, non envoyée telle quelle.

Formulaire Apple : *Request a System Extension or DriverKit entitlement*
(compte développeur › Contact › DriverKit). Le texte à coller est en anglais,
§ 3.

## 1. Ce qui est demandé

| Identifiant | Entitlement | Valeur |
|---|---|---|
| `com.belksiri.sdrdriverhub.rtlsdr-driver` (pilote) | `com.apple.developer.driverkit` | `true` |
| `com.belksiri.sdrdriverhub.rtlsdr-driver` (pilote) | `com.apple.developer.driverkit.transport.usb` | `[{ idVendor: 3034 }]` (Realtek, 0x0BDA) |
| `com.belksiri.sdrdriverhub` (application) | `com.apple.developer.driverkit.communicates-with-drivers` | `true` |
| `com.belksiri.spectrum` (Spectrum Analyzer One) | `com.apple.developer.driverkit.communicates-with-drivers` | `true` |

Ensuite : profils **App Store** pour l'application et pour le pilote.

Le profil de développement actuel du pilote accorde `idVendor = *` ; le
paquet App Store demande déjà **uniquement** 3034
(`Driver/BelksiriRtlSdrDriver.AppStore.entitlements`), et le pilote ne
s'apparie qu'à 0x0BDA:0x2838 et 0x0BDA:0x2832 (`Driver/Info.plist`).

Points à faire confirmer par Apple dans la même demande :

- la communication d'une **autre application de la même équipe**
  (Spectrum Analyzer One) avec ce pilote ne demande-t-elle rien de plus que
  `communicates-with-drivers` ? (Validé sur iPad en développement avec ce
  seul entitlement ; non vérifié en distribution.)
- le vendeur 0x0BDA **n'appartient pas** à Belksiri : Apple peut demander une
  justification ou l'accord de Realtek. Le § 3 l'explique d'emblée.

## 2. Matériel testé

| Élément | Valeur | Preuve |
|---|---|---|
| iPad | iPad Pro 11 pouces (M2) | journaux du 2026-11-21 (`docs/DIRECT_IQ_PATH.md`) |
| Récepteur | clé RTL2832U, identifiant USB 0x0BDA:0x2838 (« RTL2838UHIDIR »), tuner R820T | idem |
| Réception directe par Spectrum Analyzer One, SDR Driver Hub fermé | 1 218 blocs, 79 822 848 octets, 0 bloc perdu, 0 erreur USB | idem |
| SDR Driver Hub absent pendant la réception | 26 relevés de processus | idem |

Le RTL-SDR Blog V3 appartient à cette famille (RTL2832U + R820T2, même
identifiant USB) ; **aucun journal ne l'identifie séparément**. Aucun nouvel
essai iPad n'a été fait pour cette passe (iPad indisponible).

Pièces à joindre si Apple les demande : capture de l'écran principal du Hub
(« Prêt »), capture de Réglages › Apps › SDR Driver Hub › Pilotes, journal de
session `rtl_direct_session.log` du 2026-11-21 (sans identifiant d'appareil),
lien vers les sources publiques.

## 3. Texte de référence (version longue)

Le formulaire limite la description à 2 000 caractères : c'est la version
condensée du § 5 qui a été envoyée.

> **App:** SDR Driver Hub (`com.belksiri.sdrdriverhub`), a **free** companion
> app for **iPadOS** (iPad with an M-series chip), with one DriverKit driver,
> `com.belksiri.sdrdriverhub.rtlsdr-driver`. No in-app purchase, no
> subscription.
>
> **Purpose.** SDR Driver Hub installs a USB DriverKit driver for RTL-SDR
> receivers (Realtek RTL2832U USB demodulator with R820T/R82xx tuners).
> These are receive-only radio receivers. Our analysis app, Spectrum
> Analyzer One (`com.belksiri.spectrum`, same team), communicates directly
> with this driver through its user client to read the received IQ samples
> and display the radio spectrum. SDR Driver Hub does not need to stay open
> during reception.
>
> **Entitlements requested.**
> - Driver: `com.apple.developer.driverkit`, and
>   `com.apple.developer.driverkit.transport.usb` restricted to
>   `idVendor` 3034 (0x0BDA, Realtek). The driver matches only the RTL2832U
>   product IDs 0x2838 and 0x2832.
> - SDR Driver Hub app and Spectrum Analyzer One app:
>   `com.apple.developer.driverkit.communicates-with-drivers`.
>
> **Vendor ID.** We are not Realtek. RTL2832U receivers are made by many
> manufacturers under Realtek's vendor ID 0x0BDA, and there is no other
> vendor ID to match them. The driver does not identify itself as a Realtek
> product.
>
> **Safety and scope.**
> - USB driver, receive only: it configures the tuner and reads samples.
>   There is no transmission (the hardware cannot transmit).
> - No EEPROM write: control requests are checked against a white-list and
>   EEPROM writes are refused.
> - No firmware modification.
> - The driver refuses unauthorized clients (only apps signed by our team
>   with `communicates-with-drivers` are accepted) and refuses a second
>   simultaneous client.
> - USB access is limited to vendor 0x0BDA, products 0x2838 and 0x2832.
> - The App Store distribution contains no network server and no
>   background audio keep-alive; the app makes no network connection.
>
> **Tested hardware.** RTL2832U / R820T compatible receiver tested on a
> physical iPad Pro. Targeted models include the RTL-SDR Blog V3 and other
> RTL2832U/R82xx receivers.
>
> **Source.** The driver includes librtlsdr (GPL-2.0-or-later); the complete
> corresponding source is public:
> https://github.com/belksiri/sdr-driver-hub/tree/v1.0.0

## 4. Après l'accord

1. Profils App Store de l'application et du pilote (portail, ou signature
   automatique d'Xcode).
2. `HUB_SOURCE_URL` fixé, tag `v1.0.0` (`docs/CORRESPONDING_SOURCE.md`).
3. `HUB_DIST=1`, archive, `scripts/verify_bundle.sh --dist` : tous les
   contrôles, signatures comprises.
4. Export App Store **local** ; téléversement seulement sur GO explicite.

## 5. Envoi

| Élément | Valeur |
|---|---|
| Date | 2026-11-24 |
| Formulaire | developer.apple.com/contact/request/system-extension/ (compte de l'équipe `WVQH467NC8`) |
| Identifiant de la demande | **`GZ4VQV28Y2`** |
| Accusé de réception | page de confirmation : « Thank you for your submission. We'll review your request and contact you soon with a status update. Request ID: GZ4VQV28Y2 » ; aucun e-mail relevé à ce stade |
| Statut | en attente de la réponse d'Apple |
| Company / Product URL | https://github.com/belksiri/sdr-driver-hub |
| Entitlement | DriverKit Entitlement |
| Cases cochées | USB Transport ; UserClient Access |
| USB Vendor ID | 3034 |
| UserClient Bundle IDs | `com.belksiri.sdrdriverhub.rtlsdr-driver` |

Le formulaire n'a pas de champ distinct pour `communicates-with-drivers` :
l'accès des deux apps au pilote passe par la case *UserClient Access*
(bundle ID du pilote) et par la description.

Description envoyée, mot pour mot (1 806 caractères) :

```text
SDR Driver Hub (com.belksiri.sdrdriverhub) is a free iPadOS app (iPad with M-series chip), no in-app purchase or subscription, with one DriverKit driver: com.belksiri.sdrdriverhub.rtlsdr-driver.

Purpose: a USB driver for RTL-SDR receivers (Realtek RTL2832U with R820T/R82xx tuners), which are receive-only radios. Our app Spectrum Analyzer One (com.belksiri.spectrum, same team) communicates directly with the driver through its user client to read IQ samples and display the spectrum. SDR Driver Hub does not need to stay open during reception.

Requested:
- Driver: com.apple.developer.driverkit and driverkit.transport.usb limited to idVendor 3034 (0x0BDA, Realtek). It matches only products 0x2838 and 0x2832.
- SDR Driver Hub and Spectrum Analyzer One: driverkit.communicates-with-drivers, to open the user client of com.belksiri.sdrdriverhub.rtlsdr-driver. Please tell us if Spectrum Analyzer One needs anything more in App Store distribution.

Vendor ID: we are not Realtek. RTL2832U receivers from many makers use Realtek's vendor ID 0x0BDA; there is no other ID to match them. The driver does not present itself as a Realtek product.

Safety:
- USB driver, receive only; no transmission.
- No EEPROM write (control requests white-listed, EEPROM writes refused).
- No firmware modification.
- Unauthorized clients refused (only our team's apps with communicates-with-drivers); a second simultaneous client is refused.
- No network server and no background audio in the App Store distribution; no network connection.

Tested hardware: RTL2832U / R820T compatible receiver tested on a physical iPad Pro. Targeted: RTL-SDR Blog V3 and other RTL2832U/R82xx receivers.

Source: includes librtlsdr (GPL-2.0-or-later); complete corresponding source: https://github.com/belksiri/sdr-driver-hub/tree/v1.0.0
```
