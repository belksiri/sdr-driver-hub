#!/bin/bash
# archive.sh — Archive App Store de SDR Driver Hub, puis export LOCAL de contrôle.
#
#   HUB_SOURCE_URL=<adresse des sources> scripts/archive.sh
#
# Ne téléverse RIEN : l'export utilise `destination = export` (un .ipa sur le
# disque), jamais `upload`. La soumission à App Store Connect reste manuelle.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"; cd "$ROOT"
export GEM_HOME="${GEM_HOME:-/opt/homebrew/Cellar/cocoapods/1.16.2_1/libexec}"

[ -n "${HUB_SOURCE_URL:-}" ] || echo "WARN HUB_SOURCE_URL vide : aucune adresse de sources dans l'app — ne pas soumettre cette archive."
HUB_DIST=1 ruby scripts/generate_project.rb
(cd HubCore && swift test >/dev/null) && echo "OK   HubCore : swift test"
scripts/test_driver_logic.sh

ARCHIVE=build/archive/SDRDriverHub.xcarchive
rm -rf build/archive build/export
mkdir -p build/archive
xcodebuild archive -project SDRDriverHub.xcodeproj -scheme SDRDriverHub -configuration Release \
  -destination 'generic/platform=iOS' -archivePath "$ARCHIVE" -allowProvisioningUpdates \
  > build/archive/archive.log 2>&1 || { grep -E "error:|warning: .*(entitlement|profile)" build/archive/archive.log | head -20; exit 1; }
echo "OK   archive : $ARCHIVE"

APP="$ARCHIVE/Products/Applications/SDRDriverHub.app"
scripts/verify_bundle.sh "$APP" --dist || echo "(contrôles de distribution en échec : voir ci-dessus)"
[ -d "$ARCHIVE/dSYMs" ] && echo "     dSYMs : $(ls "$ARCHIVE/dSYMs" | tr '\n' ' ')"

# Export de contrôle, local. Échoue tant qu'Apple n'a pas accordé les
# entitlements DriverKit de distribution : c'est le blocage attendu.
cat > build/archive/ExportOptions.plist <<'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>method</key><string>app-store-connect</string>
	<key>destination</key><string>export</string>
	<key>signingStyle</key><string>automatic</string>
	<key>teamID</key><string>WVQH467NC8</string>
	<key>uploadSymbols</key><true/>
	<key>manageAppVersionAndBuildNumber</key><false/>
</dict>
</plist>
EOF
if xcodebuild -exportArchive -archivePath "$ARCHIVE" -exportPath build/export \
     -exportOptionsPlist build/archive/ExportOptions.plist > build/archive/export.log 2>&1; then
  echo "OK   export de contrôle : $(ls build/export/*.ipa)"
else
  echo "FAIL export de contrôle (aucun téléversement tenté) :"
  grep -E "error:|Error Domain|requires|entitlement|profile" build/archive/export.log | sort -u | head -12
  exit 2
fi
