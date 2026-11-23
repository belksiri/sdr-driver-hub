#!/bin/bash
# verify_bundle.sh — Inspecte un SDRDriverHub.app construit. Sort en erreur au
# premier écart. Utilisé par build_install.sh et archive.sh.
#   scripts/verify_bundle.sh <SDRDriverHub.app> [--dist]
set -u
A="$1"; DIST="${2:-}"
fail=0
ok()  { echo "OK   $*"; }
bad() { echo "FAIL $*"; fail=1; }
D="$A/SystemExtensions/BelksiriRtlSdrDriver.dext"
plist() { /usr/libexec/PlistBuddy -c "Print $2" "$1" 2>/dev/null; }

# Identité et versions
[ "$(plist "$A/Info.plist" CFBundleIdentifier)" = "com.belksiri.sdrdriverhub" ] && ok "bundle id com.belksiri.sdrdriverhub" || bad "bundle id"
[ "$(plist "$A/Info.plist" CFBundleDisplayName)" = "SDR Driver Hub" ] && ok "nom affiché SDR Driver Hub" || bad "nom affiché"
V=$(plist "$A/Info.plist" CFBundleShortVersionString); B=$(plist "$A/Info.plist" CFBundleVersion)
[ "$V" = "1.0.0" ] && ok "version $V ($B)" || bad "version $V"
DV=$(plist "$D/Info.plist" CFBundleVersion)
[ "$(plist "$D/Info.plist" CFBundleShortVersionString)" = "$V" ] && echo "$DV" | grep -Eq '^[0-9]{1,4}(\.[0-9]{1,2}){0,2}$' \
  && ok "version du dext $V ($DV), au format des extensions" || bad "version du dext : $DV (format MAJ.mi.rv requis)"
[ "$(plist "$A/Info.plist" MinimumOSVersion)" = "16.0" ] && ok "iPadOS minimal 16.0" || bad "MinimumOSVersion"
[ "$(plist "$A/Info.plist" UIDeviceFamily:0)" = "2" ] && [ -z "$(plist "$A/Info.plist" UIDeviceFamily:1)" ] && ok "iPad seulement" || bad "UIDeviceFamily"
[ "$(plist "$A/Info.plist" CFBundleURLTypes:0:CFBundleURLSchemes:0)" = "sdrdriverhub" ] && ok "schéma d'URL sdrdriverhub://" || bad "schéma d'URL"
[ "$(plist "$A/Info.plist" ITSAppUsesNonExemptEncryption)" = "false" ] && ok "chiffrement : exempté" || bad "ITSAppUsesNonExemptEncryption"
plist "$A/Info.plist" NSLocalNetworkUsageDescription >/dev/null && bad "NSLocalNetworkUsageDescription inutile" || ok "pas d'autorisation réseau local (boucle locale seule)"

# Contenu
[ "$(ls "$A/SystemExtensions")" = "BelksiriRtlSdrDriver.dext" ] && ok "un seul dext : BelksiriRtlSdrDriver" || bad "dexts : $(ls "$A/SystemExtensions")"
[ "$(plist "$D/Info.plist" CFBundleIdentifier)" = "com.belksiri.sdrdriverhub.rtlsdr-driver" ] && ok "dext com.belksiri.sdrdriverhub.rtlsdr-driver" || bad "dext id"
[ -f "$A/Assets.car" ] && [ -n "$(plist "$A/Info.plist" CFBundleIcons~ipad:CFBundlePrimaryIcon:CFBundleIconName)" ] && ok "icône AppIcon (Assets.car)" || bad "icône"
[ -f "$A/PrivacyInfo.xcprivacy" ] && ok "manifeste de confidentialité" || bad "PrivacyInfo.xcprivacy"
[ -f "$A/THIRD_PARTY_NOTICES.md" ] && [ -f "$A/GPL-2.0-or-later.txt" ] && ok "notices et texte GPL embarqués" || bad "notices de licence"
[ -f "$A/fr.lproj/Localizable.strings" ] && ok "localisation française" || bad "fr.lproj"
for l in en fr; do
  [ "$(plutil -extract CFBundleDisplayName raw "$A/$l.lproj/InfoPlist.strings" 2>/dev/null)" = "SDR Driver Hub" ] \
    && ok "nom sous l'icône ($l) : SDR Driver Hub" || bad "InfoPlist.strings $l"
done
EXTRA=$(cd "$A" && find . -type f \( -iname "*.log" -o -iname "*.enable" -o -iname "*test*" -o -iname "*.patch" -o -iname "*.swiftmodule" -o -iname "*.md" ! -name THIRD_PARTY_NOTICES.md \) | head)
[ -z "$EXTRA" ] && ok "aucun fichier de test, journal ou artefact" || bad "fichiers inattendus : $EXTRA"
ICONS=$(cd "$A" && find . -iname "*flutter*" -o -iname "*dreamflow*" -o -iname "*runner*" | head)
[ -z "$ICONS" ] && ok "aucune trace Flutter/Dreamflow/Runner" || bad "traces : $ICONS"

# Code : pas de HackRF ; librtlsdr DANS LE PILOTE seulement (protocole client
# v2), jamais dans l'app.
SYMS=$(nm -U "$A/SDRDriverHub" 2>/dev/null)
echo "$SYMS" | grep -qi hackrf && bad "symboles HackRF dans l'app" || ok "aucun symbole HackRF dans l'app"
strings -a "$A/SDRDriverHub" "$D/BelksiriRtlSdrDriver" | grep -qi hackrf && bad "chaîne HackRF" || ok "aucune chaîne HackRF (app + dext)"
# Une archive retire la table des symboles : on reconnaît librtlsdr à ses chaînes.
strings -a "$D/BelksiriRtlSdrDriver" | grep -q "Found Rafael Micro R820T tuner" \
  && ok "librtlsdr compilée dans le pilote" || bad "librtlsdr absente du pilote"
{ echo "$SYMS" | grep -q "_rtlsdr_"; strings -a "$A/SDRDriverHub" | grep -q "Found Rafael Micro R820T tuner\|rtlsdr_"; } \
  && bad "librtlsdr dans l'app" || ok "aucun code librtlsdr dans l'app (symboles et chaînes)"
strings -a "$D/BelksiriRtlSdrDriver" | grep -q "com.apple.developer.team-identifier" \
  && ok "pilote : contrôle de l'équipe et de l'entitlement du client" || bad "contrôle du client absent du pilote"
LIBS=$(otool -L "$A/SDRDriverHub" | tail -n +2 | awk '{print $1}' | grep -v "^/System/Library/\|^/usr/lib/")
[ -z "$LIBS" ] && ok "dépendances : frameworks système uniquement" || bad "dépendances inattendues : $LIBS"
[ ! -d "$A/Frameworks" ] && ok "aucun framework embarqué" || bad "Frameworks/ présent"

# Données personnelles / environnement
LEAK=$(strings -a "$A/SDRDriverHub" "$D/BelksiriRtlSdrDriver" "$A/Info.plist" | grep -E "/Users/|@gmail|00008112|192\.168\.|10\.0\.[0-9]+\.[0-9]+" | head -3)
[ -z "$LEAK" ] && ok "aucun chemin utilisateur, UDID, e-mail ni adresse IP" || bad "fuite : $LEAK"
strings -a "$A/SDRDriverHub" | grep -q "hub_events.log" && ok "journal fichier présent mais opt-in (marqueur hub_debug.enable)" || true

# Signatures et entitlements
codesign --verify --deep --strict "$A" 2>/dev/null && ok "signature de l'app (deep, strict)" || bad "signature de l'app"
codesign --verify --strict "$D" 2>/dev/null && ok "signature du dext" || bad "signature du dext"
AE=$(codesign -d --entitlements :- "$A" 2>/dev/null); DE=$(codesign -d --entitlements :- "$D" 2>/dev/null)
echo "$AE" | grep -q "communicates-with-drivers" && ok "app : driverkit.communicates-with-drivers" || bad "entitlement app"
echo "$DE" | grep -q "com.apple.developer.driverkit<" && ok "dext : driverkit" || bad "entitlement driverkit du dext"
if [ "$DIST" = "--dist" ]; then
  # Ni serveur rtl_tcp, ni maintien audio, ni mode d'arrière-plan (2.5.4).
  [ -z "$(plist "$A/Info.plist" UIBackgroundModes)" ] && ok "aucun mode d'arrière-plan" || bad "UIBackgroundModes présent"
  # Le serveur est le seul utilisateur de Network.framework (NWListener) ; le
  # maintien audio, le seul d'AVFAudio.
  otool -L "$A/SDRDriverHub" | grep -q "Network.framework" \
    && bad "serveur rtl_tcp (Network.framework) dans le build App Store" \
    || ok "aucun serveur rtl_tcp (Network.framework non lié)"
  # Ni le format rtl_tcp (en-tête « RTL0 ») ni aucun type RtlTcp* compilé.
  { strings -a "$A/SDRDriverHub" | grep -q "RTL0" || { nm "$A/SDRDriverHub" 2>/dev/null; strings -a "$A/SDRDriverHub"; } | grep -q "RtlTcp"; } \
    && bad "code rtl_tcp compilé dans le build App Store" \
    || ok "aucun code rtl_tcp compilé (ni « RTL0 » ni symbole RtlTcp)"
  { otool -L "$A/SDRDriverHub" | grep -q "AVF" || strings -a "$A/SDRDriverHub" | grep -q "keep-alive audio"; } \
    && bad "maintien audio dans le build App Store" \
    || ok "aucun maintien audio (AVFAudio non lié)"
  # Nulle part dans le paquet (binaires, textes, notices, traductions, pilote) :
  # ni mention de rtl_tcp ou de son transport, ni service local, ni adresse de
  # boucle locale, ni file d'envoi, ni écouteur réseau, ni journal de diagnostic
  # sur fichier. Insensible à la casse.
  hits=""
  for pat in rtl_tcp rtltcp RTL0 IqSendQueue NWListener NWConnection AVAudio "local service" "127.0.0.1" UIBackgroundModes hub_debug hub_events; do
    f=$(grep -rlaiF "$pat" "$A" 2>/dev/null | sed "s#^$A/##" | tr '\n' ' ')
    [ -n "$f" ] && hits="$hits [$pat: $f]"
  done
  [ -z "$hits" ] && ok "aucune trace de développement dans le paquet (rtl_tcp, service local, réseau, audio, journal fichier)" \
    || bad "traces de développement :$hits"
  [ ! -e "$A/fr.lproj/Development.strings" ] && ok "pas de textes de l'écran Development" || bad "Development.strings embarqué"
  echo "$DE" | grep -A4 "transport.usb" | grep -q "<integer>3034</integer>" && ok "dext : transport.usb idVendor 3034 (Realtek)" || bad "transport.usb du dext"
  echo "$AE" | grep -A1 "get-task-allow" | grep -q "<true/>" \
    && echo "INFO get-task-allow présent : archive signée en développement, retiré au ré-export App Store" \
    || ok "pas de get-task-allow"
else
  echo "$DE" | grep -q "transport.usb" && ok "dext : transport.usb (développement)" || bad "transport.usb du dext"
  echo "INFO build de développement : serveur rtl_tcp et UIBackgroundModes audio présents (jamais en --dist)"
fi
[ "$(plist "$D/Info.plist" BRTLBuildId)" != "" ] && echo "     dext BRTLBuildId = $(plist "$D/Info.plist" BRTLBuildId)"
exit $fail
