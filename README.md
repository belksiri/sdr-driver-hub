# SDR Driver Hub

Application compagnon iPadOS : elle possède les pilotes USB des récepteurs SDR
et sert leurs échantillons IQ, sur l'iPad lui-même, à **Spectrum Analyzer One**,
qui reste le logiciel d'analyse. Spectrum Analyzer One fonctionne seul avec le
HackRF ; le RTL-SDR devient disponible quand SDR Driver Hub est installé et
actif. Spectrum Analyzer One l'ouvre par `sdrdriverhub://open`.

Aujourd'hui : RTL-SDR (RTL2832U + R820T/R828D), servi en **rtl_tcp** sur
`127.0.0.1:1234` (boucle locale uniquement). L'architecture par backends
prépare d'autres équipements (`docs/ADDING_A_BACKEND.md`, cas ADALM-Pluto).

> **Compatibilité** : DriverKit sur iPadOS exige un iPad à puce Apple série M.
> L'iPad 10e génération (A14) et les autres iPad à puce A ne peuvent pas
> charger ces pilotes.

> Ce dépôt contient librtlsdr (osmocom, GPL-2.0-or-later) : voir
> `THIRD_PARTY_NOTICES.md` et `docs/LICENSE_INVENTORY.md`.

| Dossier | Contenu |
|---|---|
| `Hub/App/` | écran SwiftUI, modèle, licences, à propos |
| `Hub/Backends/RtlSdr/` | backend RTL-SDR : appareil, pont de contrôle, serveur rtl_tcp |
| `Hub/Support/` | maintien en arrière-plan, journal (désactivé par défaut) |
| `Hub/Resources/`, `Hub/Assets.xcassets` | icône, français, manifeste de confidentialité |
| `HubCore/` | Swift pur testé : contrat des backends, protocole rtl_tcp, contrôle du contenu IQ, file d'envoi, regroupement des gains |
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
SDR Driver Hub › Pilotes : activer le pilote, brancher le RTL-SDR. Le service
démarre avec l'app et continue en arrière-plan.

Un seul client à la fois (une nouvelle connexion remplace la précédente).
Les commandes de gain sont regroupées : la dernière valeur l'emporte, au plus
15 réglages par seconde, la valeur finale toujours appliquée, sans jamais
interrompre le flux.

## Diagnostic

Déposer `Documents/hub_debug.enable` dans le conteneur de l'app
(`xcrun devicectl device copy to … --domain-type appDataContainer`) active au
lancement suivant le journal `Documents/hub_events.log`.
`Documents/hub_debug.disable` l'efface.

## État App Store

`docs/APP_STORE_READINESS.md`.
