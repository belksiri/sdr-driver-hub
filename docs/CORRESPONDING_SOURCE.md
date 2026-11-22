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
