#!/bin/bash
# make_corresponding_source.sh — Produit l'archive des sources correspondantes
# (GPL-2.0 § 3) d'une version de SDR Driver Hub.
#
#   scripts/make_corresponding_source.sh
#
# L'archive vient de `git archive` sur le commit COURANT : elle est donc
# reproductible octet pour octet pour un même commit, et ne peut contenir
# aucun fichier non suivi (journal, build, donnée locale). Le script refuse un
# arbre modifié : la source publiée doit être exactement celle du binaire.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"; cd "$ROOT"

if [ -n "$(git status --porcelain)" ]; then
  echo "Arbre modifié : committez d'abord, la source publiée doit correspondre au binaire." >&2
  exit 1
fi
COMMIT=$(git rev-parse HEAD)
SHORT=$(git rev-parse --short=12 HEAD)
VERSION=$(ruby -e 'puts File.read("scripts/generate_project.rb")[/MARKETING_VERSION = .(\d+\.\d+\.\d+)/, 1]')
NAME="SDRDriverHub-${VERSION}-${SHORT}-source"
OUT="build/corresponding-source"
mkdir -p "$OUT"

# Date de l'archive = date du commit : deux exécutions donnent le même fichier.
git archive --format=tar --prefix="$NAME/" "$COMMIT" | gzip -n -9 > "$OUT/$NAME.tar.gz"

cat > "$OUT/$NAME.txt" <<EOF
SDR Driver Hub ${VERSION} — corresponding source
Commit: ${COMMIT}
Archive: ${NAME}.tar.gz
SHA-256: $(shasum -a 256 "$OUT/$NAME.tar.gz" | cut -d' ' -f1)
Build tools used to produce this file: $(xcodebuild -version | tr '\n' ' ')
Build instructions: BUILDING.md inside the archive.
Third-party components and modifications: THIRD_PARTY_NOTICES.md and
Controller/librtlsdr/LOCAL_CHANGES.patch inside the archive.
EOF
echo "$OUT/$NAME.tar.gz"
cat "$OUT/$NAME.txt"
