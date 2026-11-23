# Demande d'entitlements DriverKit — SDR Driver Hub 1.0.0

**Préparée le 2026-11-23. NON ENVOYÉE.** L'envoi engage officiellement le
compte développeur : il attend le GO explicite de Mounir.

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

## 3. Texte de la demande (à coller)

> **App:** SDR Driver Hub (`com.belksiri.sdrdriverhub`), a free companion app
> for iPad, with one DriverKit driver,
> `com.belksiri.sdrdriverhub.rtlsdr-driver`.
>
> **Purpose.** SDR Driver Hub installs a USB DriverKit driver for RTL-SDR
> receivers (Realtek RTL2832U USB demodulator with R820T/R82xx tuners, such
> as the RTL-SDR Blog V3). These are receive-only radio receivers. Our
> analysis app, Spectrum Analyzer One (`com.belksiri.spectrum`, same team),
> opens this driver directly through its user client to read the received
> IQ samples and display the radio spectrum. SDR Driver Hub itself does not
> need to keep running during reception, and contains no network server.
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
> manufacturers under Realtek's vendor ID, and there is no other vendor ID to
> match them. The driver does not identify itself as a Realtek product.
>
> **Safety and scope.**
> - Receive only: the driver configures the tuner and reads samples; there is
>   no transmit function (the hardware cannot transmit).
> - The driver never writes the device EEPROM and never modifies firmware;
>   control requests are checked against a white-list, and EEPROM writes are
>   refused.
> - The driver accepts only clients signed by our team with
>   `communicates-with-drivers`, and only one client at a time.
> - USB access is limited to vendor 0x0BDA.
>
> **Tested hardware.** iPad Pro 11-inch (M2) with an RTL2832U / R820T
> receiver (USB 0x0BDA:0x2838).
>
> **Source.** The driver includes librtlsdr (GPL-2.0-or-later); the complete
> source is public: <adresse du dépôt public, une fois confirmée>.

## 4. Après l'accord

1. Profils App Store de l'application et du pilote (portail, ou signature
   automatique d'Xcode).
2. `HUB_SOURCE_URL` fixé, tag `v1.0.0` (`docs/CORRESPONDING_SOURCE.md`).
3. `HUB_DIST=1`, archive, `scripts/verify_bundle.sh --dist` : tous les
   contrôles, signatures comprises.
4. Export App Store **local** ; téléversement seulement sur GO explicite.
