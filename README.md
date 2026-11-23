# SDR Driver Hub

Application compagnon iPadOS, gratuite : elle installe le pilote DriverKit
des récepteurs RTL-SDR (Realtek RTL2832U + tuners R820T/R82xx, réception
seule). **Spectrum Analyzer One**, le logiciel d'analyse, ouvre ce pilote
**directement** (protocole client v2, `docs/DIRECT_IQ_PATH.md`) : SDR Driver
Hub n'a pas besoin de rester ouvert pendant l'écoute. Spectrum Analyzer One
fonctionne seul avec le HackRF ; le RTL-SDR devient disponible quand SDR
Driver Hub est installé et son pilote activé.

librtlsdr et tout le code RTL-SDR restent dans SDR Driver Hub et son pilote ;
Spectrum Analyzer One n'en embarque rien. Le pilote refuse les clients d'une
autre équipe et un second client simultané. L'architecture par backends
prépare d'autres équipements (`docs/ADDING_A_BACKEND.md`, cas ADALM-Pluto).

Les builds de **développement** ajoutent un serveur rtl_tcp sur
`127.0.0.1:1234` et un maintien en arrière-plan ; ni l'un ni l'autre n'existe
dans un build App Store (`HUB_DIST=1`).

> **Compatibilité** : DriverKit sur iPadOS exige un iPad à puce Apple série M.
> L'iPad 10e génération (A14) et les autres iPad à puce A ne peuvent pas
> charger ces pilotes.

> Licence : GPL-2.0-or-later pour tout le dépôt (`LICENSE`), qui contient
> librtlsdr (osmocom) : voir `THIRD_PARTY_NOTICES.md`,
> `docs/LICENSE_INVENTORY.md` et `docs/CORRESPONDING_SOURCE.md`.

| Dossier | Contenu |
|---|---|
| `Hub/App/` | écran SwiftUI, modèle, licences, à propos |
| `Hub/Backends/RtlSdr/` | backend RTL-SDR : appareil, pont de contrôle, vérification de la réception (serveur rtl_tcp : développement seulement) |
| `Hub/Support/` | journal en mémoire ; journal sur fichier et maintien en arrière-plan : développement seulement |
| `Hub/Resources/`, `Hub/Assets.xcassets` | icône, français, manifeste de confidentialité |
| `HubCore/` | Swift pur testé : contrat des backends, contrôle du contenu IQ, regroupement des gains ; protocole rtl_tcp et file d'envoi (développement seulement) |
| `Core/` | transport DriverKit générique et drain du ring |
| `Driver/` | pilote DriverKit `BelksiriRtlSdrDriver` (réception seule, liste blanche `brtl_policy`) et protocole commun `bhrf_*` |
| `Controller/` | librtlsdr (amont `84195f16…` + `LOCAL_CHANGES.patch`), shim libusb, contrôleur `brtl` |
| `Design/AppIcon/` | icône source et planche de contrôle |
| `scripts/` | génération du projet, build, vérification du paquet, archive, sources correspondantes |

## Construire et installer

Voir `BUILDING.md`. En résumé :

```sh
scripts/build_install.sh <UDID>     # build Release (entitlements de développement) + contrôles + installation
scripts/archive.sh                  # archive App Store + export LOCAL de contrôle (aucun envoi)
```

Premier usage sur l'iPad : ouvrir l'app une fois, Réglages › Apps ›
SDR Driver Hub › Pilotes : activer le pilote, brancher le RTL-SDR. L'écran
indique « Prêt : Spectrum Analyzer One peut se connecter » ; « Vérifier la
réception » contrôle 1,5 s de signal réel à 88,8 MHz. Spectrum Analyzer One
ouvre ensuite le pilote lui-même, SDR Driver Hub fermé.

Un seul client à la fois. Les commandes de gain sont regroupées : la dernière
valeur l'emporte, au plus 15 réglages par seconde, la valeur finale toujours
appliquée, sans jamais interrompre le flux.

## Diagnostic (builds de développement)

Déposer `Documents/hub_debug.enable` dans le conteneur de l'app
(`xcrun devicectl device copy to … --domain-type appDataContainer`) active au
lancement suivant le journal `Documents/hub_events.log`.
`Documents/hub_debug.disable` l'efface.

## État App Store

`docs/APP_STORE_READINESS.md` et `docs/appstore/` (fiche, revue, demande
d'entitlements DriverKit, politique de confidentialité).
