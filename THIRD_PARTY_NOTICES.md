# Third-party notices — SDR Driver Hub

This file lists every third-party component compiled into SDR Driver Hub, and
the Belksiri code that is linked with it. It is shipped inside the application
(Open-source licenses screen) and at the root of the source tree. It is not a
legal opinion.

## librtlsdr (osmocom rtl-sdr)

| | |
|---|---|
| Project | rtl-sdr — turns a Realtek RTL2832U-based DVB dongle into an SDR receiver |
| Upstream | https://gitea.osmocom.org/sdr/rtl-sdr (mirror: https://github.com/osmocom/rtl-sdr) |
| Version | commit `84195f169f5b4b7dc06a10efb1e210d02b49e51c` (2026-02-14) |
| Licence | GNU General Public License, version 2 or (at your option) any later version. Full text: `LICENSES/GPL-2.0-or-later.txt` (identical to upstream `COPYING`, also kept in `Controller/librtlsdr/COPYING`). |
| Location | `Controller/librtlsdr/` |
| Linking | compiled from source and statically linked into the DriverKit driver executable `BelksiriRtlSdrDriver` (shipped inside SDR Driver Hub). Not linked into the application executable `SDRDriverHub`. Not part of Spectrum Analyzer One. |

Copyright holders named in the file headers (no header was removed or altered):

- `librtlsdr.c`, `rtl-sdr.h`: Steve Markgraf, Dimitri Stolnikov;
- `rtl-sdr_export.h`: Hoernchen;
- `tuner_fc0012`, `tuner_fc0013`: Hans-Frieder Vogt, Fitipower Integrated Technology Inc, Steve Markgraf;
- `tuner_r82xx`: Mauro Carvalho Chehab, Steve Markgraf;
- `tuner_e4k`: Sylvain Munaut, Hoernchen, Harald Welte;
- `tuner_fc2580`: no copyright line; derived from a Terratec kernel driver according to the file.

Files compiled: `src/librtlsdr.c`, `src/tuner_r82xx.c`, `src/tuner_e4k.c`,
`src/tuner_fc0012.c`, `src/tuner_fc0013.c`, `src/tuner_fc2580.c` and the
headers of `include/`. Not used: the upstream command-line programs
(`src/rtl_*.c`), and libusb.

### Modifications

Two files differ from upstream (verified file by file against the upstream
commit on 2026-11-21). The exact changes are in
`Controller/librtlsdr/LOCAL_CHANGES.patch`, and each modified file carries a
notice in its header (GPL-2.0 § 2a):

| File | Change |
|---|---|
| `include/rtl-sdr.h` | declaration of `rtlsdr_open_fd(rtlsdr_dev_t **, int)` |
| `src/librtlsdr.c` | `rtlsdr_open_fd()` (open a device from a pre-authorised handle instead of enumerating), shared `rtlsdr_finish_open()`, `#include <stdint.h>` |

The other 13 files and `COPYING` are byte-identical to upstream.

## Belksiri code linked with librtlsdr

Licensed GPL-2.0-or-later (SPDX header in each file):

| File | Role |
|---|---|
| `Controller/shim/libusb.h`, `brtl_libusb_shim.c`, `brtl_transport.h` | minimal libusb replacement written for SDR Driver Hub (only the declarations librtlsdr needs; not libusb code): control transfers go to the DriverKit driver |
| `Controller/brtl/brtl_rtl_controller.{c,h}` | narrow controller API over librtlsdr |
| `Driver/dkcompat/` | DriverKit substitutes for `<unistd.h>`, `<signal.h>`, `<errno.h>` and `fprintf`, so that librtlsdr compiles unmodified in the driver |

The rest of the DriverKit driver (`Driver/`, compiled into the same executable
as librtlsdr) and the application (`Hub/`, `Core/`, `HubCore/`, which contains
no librtlsdr code and talks to the driver through its user client) are
Belksiri code, distributed under the GNU General Public License version 2 or
later, like the whole repository (`LICENSE` at its root).

## Apple frameworks

SwiftUI, Foundation, IOKit, DriverKit and USBDriverKit (and, in development
builds only, Network and AVFoundation) are system frameworks provided by
Apple; they are not redistributed.

## Corresponding source

The complete corresponding source of each released build, with build
instructions, is published at the address shown in the application
(Open-source licenses › Source code) and is produced by
`scripts/make_corresponding_source.sh` (see `docs/CORRESPONDING_SOURCE.md`).

## Open legal points (not resolved — legal advice needed)

1. **Scope of the GPL.** The DriverKit driver executable statically links
   GPL-2.0-or-later code (the usual reading extends the GPL to that whole
   executable). Whether the application, a separate executable in the same
   package that talks to the driver through its user client, and Spectrum
   Analyzer One, a separate application that opens the same driver, are
   part of the same work or mere aggregates, has not been determined.
2. **App Store terms and the GPL.** The App Store terms of service and Apple's
   mandatory signing and usage rules may impose restrictions that GPL-2.0
   § 6 forbids adding. Several GPL copyright holders have considered App Store
   distribution incompatible in the past. Not reviewed by a lawyer for this
   project.
3. ~~Licence of the Belksiri code~~ — declared: GPL-2.0-or-later for the
   whole repository (`LICENSE`).
