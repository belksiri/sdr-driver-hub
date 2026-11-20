# SDR Driver Hub (nom provisoire)

> **EXPERIMENTAL — DEVELOPMENT ONLY.** Usage local de développement uniquement.
> Ce dépôt contient librtlsdr (osmocom, GPL-2.0-or-later, `LICENSES/`).

Application compagnon iPadOS : elle porte le pilote DriverKit RTL-SDR validé
sur l'iPad (prototype de Belksiri Spectrum, branche
`experiment/ipados-rtlsdr-v3-gpl`) et sert les IQ en **rtl_tcp** sur
`127.0.0.1:1234`. Belksiri Spectrum (source RTL-SDR sur iPadOS) en est le
client. Aucune fonction de Spectrum ici.

| Dossier | Contenu | Origine |
|---|---|---|
| `Driver/` | dext `BelksiriRtlSdrDriver` (user client, ring, politique `brtl_policy`) et C partagé `bhrf_*` | copié du prototype, inchangé |
| `Controller/` | librtlsdr (commit amont `84195f16…`, modification Android `rtlsdr_open_fd`), shim libusb, contrôleur `brtl` | copié ; contrôleur + 5 fonctions rtl_tcp (PPM, AGC RTL, échantillonnage direct, offset tuning, type de tuner) |
| `Core/` | transport DriverKit, drain du ring | copié du prototype HackRF/RTL (table de débits HackRF retirée) |
| `Hub/` | app : appareil RTL, serveur rtl_tcp, keep-alive audio, écran | nouveau ; `RtlControlBridge` extrait du prototype |
| `HubCore/` | protocole rtl_tcp, contrôle de contenu IQ, file d'envoi, regroupement des commandes de gain (Swift pur) | nouveau, `swift test` |

## Construire et installer

```sh
scripts/build_install.sh <UDID>
```

Premier usage sur l'iPad : ouvrir l'app une fois, Réglages › Apps ›
SDR Driver Hub › Pilotes : activer `BelksiriRtlSdrDriver`, brancher le RTL-SDR.
Le service démarre avec l'app et survit en arrière-plan (mode audio, silence
mélangé aux autres sons). URL : `sdrdriverhub://open`.

Un seul client à la fois (une nouvelle connexion remplace la précédente).
Les commandes de gain sont regroupées : la dernière valeur l'emporte, une
seule requête en attente, au plus 15 réglages par seconde, la valeur finale
toujours appliquée ; la connexion et le flux ne sont jamais interrompus.

## Vérifier le contenu depuis le Mac

```sh
python3 scripts/rtltcp_probe.py <adresse-wifi-de-l-ipad>
```

Mesure le débit, cherche les répétitions (blocs identiques, motif périodique)
et calcule un spectre hors ligne : les stations FM doivent ressortir. Le
diagnostic de l'app (« Test ») n'est vert que si le contenu est vivant, jamais
sur le seul débit.
