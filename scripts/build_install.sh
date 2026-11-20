#!/bin/bash
# build_install.sh — Génère, construit, vérifie et (option) installe SDR Driver Hub.
#   scripts/build_install.sh [<UDID>]
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"; cd "$ROOT"
export GEM_HOME="${GEM_HOME:-/opt/homebrew/Cellar/cocoapods/1.16.2_1/libexec}"
ruby scripts/generate_project.rb
(cd HubCore && swift test >/dev/null) && echo "OK   HubCore : swift test"
rm -rf build/dd
xcodebuild build -project SDRDriverHub.xcodeproj -scheme SDRDriverHub -configuration Release \
  -destination 'generic/platform=iOS' -derivedDataPath build/dd -allowProvisioningUpdates > build/xcodebuild.log 2>&1 \
  || { grep -E "error:" build/xcodebuild.log | head; exit 1; }
A=build/dd/Build/Products/Release-iphoneos/SDRDriverHub.app
test -d "$A/SystemExtensions/BelksiriRtlSdrDriver.dext" && echo "OK   dext RTL-SDR embarqué"
codesign --verify --deep --strict "$A" && echo "OK   signature"
codesign -d --entitlements :- "$A/SystemExtensions/BelksiriRtlSdrDriver.dext" 2>/dev/null \
  | grep -q "driverkit.transport.usb" && echo "OK   entitlement transport.usb du dext"
echo "     BRTLBuildId = $(/usr/libexec/PlistBuddy -c 'Print BRTLBuildId' "$A/SystemExtensions/BelksiriRtlSdrDriver.dext/Info.plist")"
if [ $# -ge 1 ]; then xcrun devicectl device install app --device "$1" "$A"; fi
