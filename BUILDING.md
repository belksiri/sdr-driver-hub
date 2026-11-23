# Building SDR Driver Hub

These instructions rebuild the application, its DriverKit driver and the
librtlsdr component from this source tree. They are part of the corresponding
source required by the GPL for librtlsdr.

## Requirements

- macOS with Xcode 27 or later (iOS 27 and DriverKit 27 SDKs; built and tested with Xcode 27.0, 27A266a) and its command-line tools.
- Ruby with the `xcodeproj` gem (shipped with CocoaPods), used to generate the
  Xcode project: `gem install xcodeproj`, or set `GEM_HOME` to a CocoaPods
  installation.
- To run on an iPad: an Apple Developer team with DriverKit development
  entitlements (`com.apple.developer.driverkit`,
  `com.apple.developer.driverkit.transport.usb`,
  `com.apple.developer.driverkit.communicates-with-drivers`), and an iPad
  with an M-series chip running iPadOS 16 or later.

## Steps

```sh
# 1. Pure-Swift core: IQ content check, gain coalescing, backend model, and
#    the development-only rtl_tcp protocol (no hardware needed).
(cd HubCore && swift test)

# 2. Generate SDRDriverHub.xcodeproj (application + DriverKit driver).
#    HUB_TEAM_ID selects your Apple Developer team.
HUB_TEAM_ID=<your team id> ruby scripts/generate_project.rb

# 3. Build, check the bundle, and optionally install on a connected iPad.
scripts/build_install.sh [<device UDID>]
```

`scripts/verify_bundle.sh` checks the result: identifiers, versions, a single
RTL-SDR driver, licence notices, signatures and entitlements.

## The App Store configuration, without a device or a signing identity

The published binary is built with `HUB_DIST=1`: the driver's USB access is
limited to Realtek (`idVendor` 3034 = 0x0BDA), and the development-only
rtl_tcp server, its protocol code and the audio keep-alive are not compiled
at all (no `HUB_DEV_RTLTCP`). The same configuration can be rebuilt and
checked on any Mac, unsigned:

```sh
# Driver logic (client policy, USB bus white-list), pure C on the host.
scripts/test_driver_logic.sh

HUB_DIST=1 ruby scripts/generate_project.rb
xcodebuild build -project SDRDriverHub.xcodeproj -scheme SDRDriverHub \
  -configuration Release -destination 'generic/platform=iOS' \
  -derivedDataPath build/dd-dist CODE_SIGNING_ALLOWED=NO
scripts/verify_bundle.sh build/dd-dist/Build/Products/Release-iphoneos/SDRDriverHub.app --dist
```

Unsigned, only the signature and entitlement checks fail; every content check
(no rtl_tcp, no network or audio framework, no background mode, librtlsdr in
the driver only, notices and GPL embedded) must pass.

## Versions

| Component | Version |
|---|---|
| Application | `CFBundleShortVersionString` 1.0.0; `CFBundleVersion` `YYYYMMDD.HHMM` of the build (always increasing), or `HUB_BUILD_NUMBER` |
| Driver (dext) | 1.0.0 (`MAJOR.minor.patch`, the format iPadOS accepts for a driver) |
| librtlsdr | osmocom rtl-sdr `84195f169f5b4b7dc06a10efb1e210d02b49e51c` + `LOCAL_CHANGES.patch` |
| Driver client protocol | 2 (`docs/DIRECT_IQ_PATH.md`) |

## Replacing librtlsdr

librtlsdr is compiled from `Controller/librtlsdr/` (osmocom rtl-sdr
`84195f169f5b4b7dc06a10efb1e210d02b49e51c` plus
`Controller/librtlsdr/LOCAL_CHANGES.patch`). To use a modified version, edit
or replace those files and rebuild with the steps above: the application uses
only the public librtlsdr API (`rtl-sdr.h`) plus `rtlsdr_open_fd`, through
`Controller/brtl/brtl_rtl_controller.c`.

On iPadOS, a modified build can only be installed on a device with a signing
identity and provisioning profile that grant the entitlements listed above.
