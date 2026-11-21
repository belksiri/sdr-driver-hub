/*
 * dkcompat/brtl_dk_prefix.h — Inclus en tête de chaque source librtlsdr
 * compilée dans le pilote (-include). DriverKit n'a ni stderr ni fprintf :
 * les messages de librtlsdr partent dans os_log par brtl_dk_log().
 *
 * Copyright (C) 2026 Belksiri.
 * SPDX-License-Identifier: GPL-2.0-or-later — built with librtlsdr; see THIRD_PARTY_NOTICES.md.
 */
#ifndef BRTL_DK_PREFIX_H
#define BRTL_DK_PREFIX_H
#ifndef __cplusplus
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
/* DriverKit n'expose ni calloc ni free : l'allocateur de librtlsdr passe par
 * IOMallocZero / IOFree (BelksiriRtlSdrDriver.cpp, brtl_dk_*). */
void *brtl_dk_malloc(size_t size);
void *brtl_dk_calloc(size_t count, size_t size);
void brtl_dk_free(void *ptr);
#define malloc(n) brtl_dk_malloc(n)
#define calloc(c, n) brtl_dk_calloc((c), (n))
#define free(p) brtl_dk_free(p)
/* Seul le chemin asynchrone de librtlsdr (rtlsdr_read_async), jamais appelé
 * dans le pilote, déclare des struct timeval : il suffit qu'elle existe. */
struct timeval { time_t tv_sec; int tv_usec; };
int brtl_dk_log(const char *format, ...) __attribute__((format(printf, 1, 2)));
#define fprintf(stream, ...) brtl_dk_log(__VA_ARGS__)
#define printf(...) brtl_dk_log(__VA_ARGS__)
#endif
#endif
