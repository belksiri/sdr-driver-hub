/*
 * dkcompat/errno.h — DriverKit n'expose pas <errno.h> en C. librtlsdr n'en
 * prend que des constantes (valeurs Darwin), renvoyées négées comme codes.
 *
 * Copyright (C) 2026 Belksiri.
 * SPDX-License-Identifier: GPL-2.0-or-later — built with librtlsdr; see THIRD_PARTY_NOTICES.md.
 */
#ifndef BRTL_DKCOMPAT_ERRNO_H
#define BRTL_DKCOMPAT_ERRNO_H
#ifndef EIO
#define EIO 5
#endif
#ifndef ENOMEM
#define ENOMEM 12
#endif
#ifndef EBUSY
#define EBUSY 16
#endif
#ifndef EINVAL
#define EINVAL 22
#endif
#endif
