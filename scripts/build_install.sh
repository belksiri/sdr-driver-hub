#!/bin/bash
# build_install.sh — Génère, teste, construit, vérifie et (option) installe SDR Driver Hub.
#   scripts/build_install.sh [<UDID>]
# Build Release signé avec les entitlements de DÉVELOPPEMENT (installable sur
# un iPad de l'équipe). L'archive App Store est faite par scripts/archive.sh.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"; cd "$ROOT"
export GEM_HOME="${GEM_HOME:-/opt/homebrew/Cellar/cocoapods/1.16.2_1/libexec}"
ruby scripts/generate_project.rb
(cd HubCore && swift test >/dev/null) && echo "OK   HubCore : swift test"
scripts/test_driver_logic.sh
rm -rf build/dd
xcodebuild build -project SDRDriverHub.xcodeproj -scheme SDRDriverHub -configuration Release \
  -destination 'generic/platform=iOS' -derivedDataPath build/dd -allowProvisioningUpdates > build/xcodebuild.log 2>&1 \
  || { grep -E "error:" build/xcodebuild.log | head; exit 1; }
scripts/verify_bundle.sh build/dd/Build/Products/Release-iphoneos/SDRDriverHub.app
if [ $# -ge 1 ]; then xcrun devicectl device install app --device "$1" build/dd/Build/Products/Release-iphoneos/SDRDriverHub.app; fi
