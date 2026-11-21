/*
 * dkcompat/unistd.h — DriverKit n'a pas <unistd.h>. librtlsdr n'y prend que
 * usleep(), rendu ici par IOSleep (millisecondes, arrondi au-dessus).
 *
 * Copyright (C) 2026 Belksiri.
 * SPDX-License-Identifier: GPL-2.0-or-later — built with librtlsdr; see THIRD_PARTY_NOTICES.md.
 */
#ifndef BRTL_DKCOMPAT_UNISTD_H
#define BRTL_DKCOMPAT_UNISTD_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
void IOSleep(uint64_t ms);
static inline int brtl_dk_usleep(unsigned int us) { IOSleep(((uint64_t)us + 999u) / 1000u); return 0; }
#define usleep(us) brtl_dk_usleep(us)
#ifdef __cplusplus
}
#endif
#endif
