# Inventaire des dépendances — SDR Driver Hub 1.0.0

Relevé le 2026-11-21 sur l'arbre de travail, mis à jour le 2026-11-20 pour le protocole client v2 (librtlsdr déplacée dans le pilote). Ce n'est pas un avis juridique :
les points marqués **[AVIS JURIDIQUE]** demandent un juriste.

## Composants tiers

### librtlsdr (osmocom rtl-sdr)

| Champ | Valeur |
|---|---|
| Version | commit `84195f169f5b4b7dc06a10efb1e210d02b49e51c` (2026-02-14), dépôt https://gitea.osmocom.org/sdr/rtl-sdr |
| Licence | GPL-2.0-or-later (`Controller/librtlsdr/COPYING`, identique à l'amont et à `LICENSES/GPL-2.0-or-later.txt`) |
| Fichiers utilisés | `src/librtlsdr.c`, `src/tuner_r82xx.c`, `src/tuner_e4k.c`, `src/tuner_fc0012.c`, `src/tuner_fc0013.c`, `src/tuner_fc2580.c`, `include/*.h` (9 en-têtes) |
| Type de liaison | compilé depuis les sources, **lié statiquement dans l'exécutable du pilote DriverKit** `BelksiriRtlSdrDriver` (dans `SDRDriverHub.app/SystemExtensions/`). **Absent de l'exécutable de l'application** `SDRDriverHub` et **absent de Spectrum Analyzer One** (vérifié par chaînes et symboles : `scripts/verify_bundle.sh`, test « embeds no RTL-SDR driver » de Spectrum Analyzer One). Les fichiers de librtlsdr ne sont pas modifiés pour DriverKit : la compatibilité passe par `Driver/dkcompat/` (en-têtes de substitution et inclusion forcée) |
| Modifications locales | 2 fichiers : `include/rtl-sdr.h`, `src/librtlsdr.c` (`rtlsdr_open_fd`, `rtlsdr_finish_open`, `#include <stdint.h>`). Diff exact : `Controller/librtlsdr/LOCAL_CHANGES.patch`, vérifié contre l'amont le 2026-11-21 ; les 13 autres fichiers et `COPYING` sont identiques octet pour octet |
| Obligations de redistribution (GPL-2.0) | § 1 : avis de copyright et de licence, copie de la GPL ; § 2a : mention des modifications datées dans les fichiers modifiés (présente) ; § 3 : sources correspondantes complètes (y compris scripts de compilation) avec le binaire ou offre écrite ; § 6 : aucune restriction supplémentaire imposée au destinataire |
| Emplacement des notices | `THIRD_PARTY_NOTICES.md` (racine et paquet), `LICENSES/GPL-2.0-or-later.txt` (paquet), écran « Licences open source » de l'app |
| Sources à publier | le dépôt complet au commit du build (`scripts/make_corresponding_source.sh`) : librtlsdr modifiée, son correctif, le code Belksiri lié, les scripts de génération et de compilation, `BUILDING.md` |

### Shim libusb et contrôleur (code Belksiri lié à librtlsdr)

| Champ | Valeur |
|---|---|
| Fichiers | `Controller/shim/libusb.h`, `Controller/shim/brtl_libusb_shim.c`, `Controller/shim/brtl_transport.h`, `Controller/brtl/brtl_rtl_controller.{c,h}`, `Driver/dkcompat/` (`unistd.h`, `signal.h`, `errno.h`, `brtl_dk_prefix.h`) |
| Origine | écrits pour le projet. `libusb.h` ne reprend **aucun code de libusb** : uniquement les noms et valeurs de l'API publique dont librtlsdr a besoin |
| Licence | GPL-2.0-or-later (en-tête SPDX) |
| Liaison | statique, dans le pilote `BelksiriRtlSdrDriver` |
| Point ouvert | reprise des noms et valeurs de l'API publique de libusb (LGPL-2.1) dans un en-tête réécrit : risque jugé faible, **[AVIS JURIDIQUE]** si l'on veut une certitude |

### Frameworks Apple

SwiftUI, Foundation, IOKit (app) ; Network et AVFoundation dans les builds
de développement seulement (serveur rtl_tcp, maintien audio : absents du
build App Store, vérifié par `verify_bundle.sh --dist`) ; DriverKit,
USBDriverKit (pilote). Fournis par le système, non redistribués, liés
dynamiquement (`otool -L` : uniquement `/System/Library` et `/usr/lib`).

### Aucun autre composant

Pas de gestionnaire de paquets, pas de framework embarqué, pas de SDK tiers,
pas de police ni d'image tierce (l'icône est une création transmise par
Mounir, `Design/AppIcon/AppIcon-source-1254.png`).

## Code Belksiri non lié à librtlsdr

| Partie | Contenu | Licence déclarée |
|---|---|---|
| `Hub/`, `Core/`, `HubCore/` | application SwiftUI, transport DriverKit, cœur pur | **aucune** |
| `Driver/` (hors `dkcompat/`) | pilote DriverKit `BelksiriRtlSdrDriver` : liste blanche, ring, protocole client v2 (`brtl_client_protocol.h`, `brtl_client_policy.c`, `brtl_bus.c`). **Compilé dans le même exécutable que librtlsdr** | **aucune** |
| `scripts/` | génération, construction, vérification | **aucune** |

**[DÉCISION MOUNIR + AVIS JURIDIQUE]** Le code du pilote est compilé dans le
même exécutable que librtlsdr ; celui de l'application est livré dans le même
paquet et lui parle par le user client.
L'option la plus simple et la plus sûre au regard de la GPL est de publier
tout le dépôt sous GPL-2.0-or-later. Toute autre option suppose de trancher
d'abord la question de l'étendue du copyleft.

## Compatibilité GPL / App Store / signature Apple

| Question | État | Commentaire |
|---|---|---|
| L'App Store impose-t-il des restrictions que la GPL-2.0 § 6 interdit ? | **[AVIS JURIDIQUE]** | Les conditions d'utilisation de l'App Store (règles d'usage, nombre d'appareils, DRM FairPlay du paquet) ont été jugées incompatibles avec la GPL par certains détenteurs de droits (retrait de VLC en 2011 à la demande d'un contributeur). D'autres applications GPL restent publiées, avec l'accord de tous les détenteurs ou sans contestation. Aucun accord des détenteurs de librtlsdr n'a été demandé. |
| La signature imposée par Apple empêche-t-elle d'exercer la GPL ? | **[AVIS JURIDIQUE]** | Un utilisateur ne peut installer une version modifiée qu'avec un compte développeur et les entitlements DriverKit. La GPL-2.0 (contrairement à la GPLv3 § 6 « Installation Information ») n'impose pas de fournir les clés de signature ; le point est donc moins critique qu'avec la GPLv3, mais il touche à l'esprit du § 6. |
| L'étendue du copyleft au code Belksiri | **[AVIS JURIDIQUE]** | voir ci-dessus. |
| Spectrum Analyzer One, client du pilote | **[AVIS JURIDIQUE]** | Spectrum Analyzer One (autre app) n'embarque aucun code GPL : il ouvre le pilote par IOKit et lui envoie des commandes et lit un ring d'échantillons, comme il lisait auparavant un socket rtl_tcp. La lecture habituelle traite ce mode (communication entre exécutables distincts) comme une agrégation ; l'échange de structures binaires partagées (`brtl_client_protocol.h`, ring mappé) est à faire confirmer. L'en-tête d'interface copié dans Spectrum Analyzer One ne contient aucun code de librtlsdr. |
| Notices, texte de licence, sources | prêt côté technique | Notices et GPL embarquées et affichées ; archive de sources reproductible. Adresse publique des sources (`HUB_SOURCE_URL`) : <https://github.com/belksiri/sdr-driver-hub>. |

Conclusion : l'application **n'est pas déclarée publiable** au regard des
licences tant que ces trois points n'ont pas reçu un avis juridique et que
l'adresse des sources n'est pas fixée. La voie qui supprime la question est
un contrôleur RTL2832U indépendant de librtlsdr (non GPL) derrière la même
interface `brtl_transport`.
