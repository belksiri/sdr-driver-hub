# Sources correspondantes (GPL-2.0 § 3)

Chaque binaire distribué de SDR Driver Hub contient librtlsdr (GPL-2.0-or-later).
Ses sources correspondantes complètes doivent accompagner le binaire ou être
proposées par une offre écrite valable trois ans. La voie retenue : les publier
à une adresse fixe, affichée dans l'application (« Licences open source ›
Code source »).

## Produire l'archive

```sh
git status                                # doit être propre
scripts/make_corresponding_source.sh
# → build/corresponding-source/SDRDriverHub-<version>-<commit>-source.tar.gz
#   + un .txt : commit, SHA-256, version de Xcode
```

L'archive est faite par `git archive` sur le commit du build : elle est
reproductible (même commit, même archive) et ne peut contenir aucun fichier
local (journal, build, donnée d'appareil). Elle comprend :

- librtlsdr telle que compilée, avec `COPYING`, `VENDOR_COMMIT.txt` et
  `LOCAL_CHANGES.patch` ;
- tout le code de l'application et du pilote ;
- les scripts qui pilotent la compilation (`scripts/`) et `BUILDING.md`.

## Publier

1. Construire l'archive App Store **depuis le même commit**
   (`HUB_SOURCE_URL=<adresse> scripts/archive.sh`).
2. Déposer le `.tar.gz` et le `.txt` à `<adresse>` avant la mise en vente.
3. Garder chaque archive publiée au moins trois ans après la dernière
   distribution du binaire correspondant.

`HUB_SOURCE_URL` vaut par défaut l'adresse publique du dépôt,
<https://github.com/belksiri/sdr-driver-hub> : l'écran des licences affiche ce lien
« Code source ». Chaque archive de sources d'une version distribuée y est
déposée (étape 2 ci-dessus).

## Version 1.0.0 : procédure de publication

1. Les commits de la version sont faits, l'arbre est propre.
2. `HUB_SOURCE_URL` est fixé à l'adresse **publique exacte** du tag
   (`https://github.com/<propriétaire confirmé>/sdr-driver-hub/tree/v1.0.0`),
   dans `scripts/generate_project.rb`, puis commité. L'adresse n'est écrite
   qu'une fois le propriétaire du dépôt confirmé ; elle n'est jamais devinée.
3. Le tag `v1.0.0` est posé sur ce commit, celui du paquet candidat.
4. `scripts/make_corresponding_source.sh` produit l'archive depuis ce commit ;
   elle est reconstruite à part (voir ci-dessous), puis jointe à la release
   GitHub `v1.0.0` avec son `.txt` (commit, SHA-256, version de Xcode).
5. Le paquet App Store est construit depuis le même commit.

### Contrôle de reconstruction

Depuis l'archive extraite, sans le dépôt Git : `scripts/test_driver_logic.sh`,
`(cd HubCore && swift test)`, puis le build `HUB_DIST=1` non signé et
`scripts/verify_bundle.sh --dist` (`BUILDING.md`). Seuls les contrôles de
signature doivent échouer sur un build non signé.

### Provenance de librtlsdr (vérifiée le 2026-11-23)

Les fichiers de `Controller/librtlsdr/` ont été comparés à osmocom rtl-sdr
`84195f169f5b4b7dc06a10efb1e210d02b49e51c` (dépôt public
<https://github.com/osmocom/rtl-sdr>) : 13 fichiers et `COPYING` identiques
octet pour octet ; `include/rtl-sdr.h` et `src/librtlsdr.c` diffèrent, et
`LOCAL_CHANGES.patch` appliqué à l'amont reproduit exactement ces deux
fichiers. Le détail des modifications est dans `THIRD_PARTY_NOTICES.md`.
