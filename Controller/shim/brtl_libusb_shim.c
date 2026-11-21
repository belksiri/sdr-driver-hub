/*
 * brtl_libusb_shim.c — Implémentation du shim libusb (voir libusb.h).
 *
 * Copyright (C) 2026 Belksiri.
 * SPDX-License-Identifier: GPL-2.0-or-later — linked with librtlsdr; see THIRD_PARTY_NOTICES.md.
 */
#ifdef BRTL_DRIVERKIT
/* Dans le pilote DriverKit, chaque appel au contrôleur (donc au shim) est
 * fait sous le verrou `ctrlLock` du pilote : aucun accès concurrent n'existe,
 * et DriverKit n'expose pas <pthread.h>. */
typedef int pthread_mutex_t;
#define PTHREAD_MUTEX_INITIALIZER 0
#define pthread_mutex_lock(m) ((void)(m))
#define pthread_mutex_unlock(m) ((void)(m))
#else
#include <pthread.h>
#endif
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "brtl_transport.h"
#include "libusb.h"

#define BRTL_MAX_TRANSPORTS 4

struct libusb_context { int unused; };
struct libusb_device { libusb_device_handle *handle; };
struct libusb_device_handle {
    int transport_id;
    struct libusb_device device;
};

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static brtl_transport g_transports[BRTL_MAX_TRANSPORTS + 1]; /* [0] inutilisé */
static int g_used[BRTL_MAX_TRANSPORTS + 1];

int brtl_transport_register(const brtl_transport *transport)
{
    if (transport == NULL || transport->control_transfer == NULL) return 0;
    pthread_mutex_lock(&g_lock);
    int id = 0;
    for (int i = 1; i <= BRTL_MAX_TRANSPORTS; i++) {
        if (!g_used[i]) {
            g_used[i] = 1;
            g_transports[i] = *transport;
            id = i;
            break;
        }
    }
    pthread_mutex_unlock(&g_lock);
    return id;
}

void brtl_transport_unregister(int id)
{
    if (id < 1 || id > BRTL_MAX_TRANSPORTS) return;
    pthread_mutex_lock(&g_lock);
    g_used[id] = 0;
    memset(&g_transports[id], 0, sizeof(g_transports[id]));
    pthread_mutex_unlock(&g_lock);
}

static int lookup(int id, brtl_transport *out)
{
    int ok = 0;
    pthread_mutex_lock(&g_lock);
    if (id >= 1 && id <= BRTL_MAX_TRANSPORTS && g_used[id]) {
        *out = g_transports[id];
        ok = 1;
    }
    pthread_mutex_unlock(&g_lock);
    return ok;
}

/* ── Contexte et poignées ──────────────────────────────────────────────────── */

int libusb_init(libusb_context **ctx)
{
    if (ctx == NULL) return LIBUSB_SUCCESS;
    *ctx = calloc(1, sizeof(libusb_context));
    return *ctx ? LIBUSB_SUCCESS : LIBUSB_ERROR_NO_MEM;
}

void libusb_exit(libusb_context *ctx) { free(ctx); }

int libusb_set_option(libusb_context *ctx, enum libusb_option option, ...)
{
    (void)ctx; (void)option;
    return LIBUSB_SUCCESS;
}

/* Aucune énumération sur iPadOS : l'appareil est celui du dext. */
ssize_t libusb_get_device_list(libusb_context *ctx, libusb_device ***list)
{
    (void)ctx;
    if (list) *list = NULL;
    return 0;
}

void libusb_free_device_list(libusb_device **list, int unref) { (void)list; (void)unref; }

int libusb_open(libusb_device *dev, libusb_device_handle **h)
{
    (void)dev; (void)h;
    return LIBUSB_ERROR_NOT_SUPPORTED;
}

int libusb_wrap_sys_device(libusb_context *ctx, intptr_t sys_dev,
                           libusb_device_handle **dev_handle)
{
    (void)ctx;
    brtl_transport t;
    if (dev_handle == NULL || !lookup((int)sys_dev, &t)) return LIBUSB_ERROR_NO_DEVICE;
    libusb_device_handle *h = calloc(1, sizeof(*h));
    if (h == NULL) return LIBUSB_ERROR_NO_MEM;
    h->transport_id = (int)sys_dev;
    h->device.handle = h;
    *dev_handle = h;
    return LIBUSB_SUCCESS;
}

void libusb_close(libusb_device_handle *h) { free(h); }

libusb_device *libusb_get_device(libusb_device_handle *h)
{
    return h ? &h->device : NULL;
}

/* Le dext a déjà ouvert l'appareil et son interface, en propriétaire unique. */
int libusb_kernel_driver_active(libusb_device_handle *h, int i) { (void)h; (void)i; return 0; }
int libusb_detach_kernel_driver(libusb_device_handle *h, int i) { (void)h; (void)i; return LIBUSB_ERROR_NOT_SUPPORTED; }
int libusb_attach_kernel_driver(libusb_device_handle *h, int i) { (void)h; (void)i; return LIBUSB_ERROR_NOT_SUPPORTED; }
int libusb_claim_interface(libusb_device_handle *h, int i) { (void)i; return h ? LIBUSB_SUCCESS : LIBUSB_ERROR_INVALID_PARAM; }
int libusb_release_interface(libusb_device_handle *h, int i) { (void)h; (void)i; return LIBUSB_SUCCESS; }
/* Une réinitialisation USB n'est pas exposée par le pilote : refus explicite. */
int libusb_reset_device(libusb_device_handle *h) { (void)h; return LIBUSB_ERROR_NOT_SUPPORTED; }

/* ── Transferts ────────────────────────────────────────────────────────────── */

int libusb_control_transfer(libusb_device_handle *h, uint8_t request_type,
                            uint8_t bRequest, uint16_t wValue, uint16_t wIndex,
                            unsigned char *data, uint16_t wLength,
                            unsigned int timeout)
{
    brtl_transport t;
    if (h == NULL || !lookup(h->transport_id, &t)) return LIBUSB_ERROR_NO_DEVICE;
    return t.control_transfer(t.ctx, request_type, bRequest, wValue, wIndex,
                              data, wLength, timeout);
}

int libusb_get_device_descriptor(libusb_device *dev, struct libusb_device_descriptor *d)
{
    if (dev == NULL || dev->handle == NULL || d == NULL) return LIBUSB_ERROR_INVALID_PARAM;
    uint8_t raw[18];
    memset(raw, 0, sizeof raw);
    const int r = libusb_control_transfer(dev->handle, 0x80, 6, (uint16_t)(1u << 8),
                                          0, raw, sizeof raw, 1000);
    if (r < 18) return r < 0 ? r : LIBUSB_ERROR_IO;
    d->bLength = raw[0];
    d->bDescriptorType = raw[1];
    d->bcdUSB = (uint16_t)(raw[2] | (raw[3] << 8));
    d->bDeviceClass = raw[4];
    d->bDeviceSubClass = raw[5];
    d->bDeviceProtocol = raw[6];
    d->bMaxPacketSize0 = raw[7];
    d->idVendor = (uint16_t)(raw[8] | (raw[9] << 8));
    d->idProduct = (uint16_t)(raw[10] | (raw[11] << 8));
    d->bcdDevice = (uint16_t)(raw[12] | (raw[13] << 8));
    d->iManufacturer = raw[14];
    d->iProduct = raw[15];
    d->iSerialNumber = raw[16];
    d->bNumConfigurations = raw[17];
    return LIBUSB_SUCCESS;
}

/* Descripteur de chaîne (USB 2.0 § 9.6.7) : UTF-16LE → ASCII, '?' hors ASCII.
 * Lu en 64 octets au plus (limite du pilote) : 31 caractères. */
int libusb_get_string_descriptor_ascii(libusb_device_handle *h, uint8_t index,
                                       unsigned char *out, int length)
{
    if (h == NULL || out == NULL || length <= 0) return LIBUSB_ERROR_INVALID_PARAM;
    if (index == 0) return LIBUSB_ERROR_INVALID_PARAM;
    uint8_t buf[64];
    memset(buf, 0, sizeof buf);
    int r = libusb_control_transfer(h, 0x80, 6, (uint16_t)(3u << 8), 0, buf, 4, 1000);
    if (r < 4) return r < 0 ? r : LIBUSB_ERROR_IO;
    const uint16_t langid = (uint16_t)(buf[2] | (buf[3] << 8));
    memset(buf, 0, sizeof buf);
    r = libusb_control_transfer(h, 0x80, 6, (uint16_t)((3u << 8) | index), langid,
                                buf, sizeof buf, 1000);
    if (r < 2) return r < 0 ? r : LIBUSB_ERROR_IO;
    if (buf[1] != 3) return LIBUSB_ERROR_IO;
    int total = buf[0] < r ? buf[0] : r;
    int n = 0;
    for (int i = 2; i + 1 < total && n < length - 1; i += 2) {
        out[n++] = (buf[i + 1] == 0 && buf[i] < 0x80) ? buf[i] : '?';
    }
    out[n] = 0;
    return n;
}

/* ── Asynchrone : non pris en charge (le flux vient du ring du dext) ───────── */

int libusb_bulk_transfer(libusb_device_handle *h, unsigned char ep, unsigned char *d,
                         int len, int *actual, unsigned int timeout)
{
    (void)h; (void)ep; (void)d; (void)len; (void)timeout;
    if (actual) *actual = 0;
    return LIBUSB_ERROR_NOT_SUPPORTED;
}

struct libusb_transfer *libusb_alloc_transfer(int iso) { (void)iso; return NULL; }
void libusb_free_transfer(struct libusb_transfer *t) { (void)t; }
int  libusb_submit_transfer(struct libusb_transfer *t) { (void)t; return LIBUSB_ERROR_NOT_SUPPORTED; }
int  libusb_cancel_transfer(struct libusb_transfer *t) { (void)t; return LIBUSB_ERROR_NOT_SUPPORTED; }
int  libusb_handle_events_timeout_completed(libusb_context *c, struct timeval *tv, int *done)
{
    (void)c; (void)tv; (void)done;
    return LIBUSB_ERROR_NOT_SUPPORTED;
}
unsigned char *libusb_dev_mem_alloc(libusb_device_handle *h, size_t n) { (void)h; (void)n; return NULL; }
int libusb_dev_mem_free(libusb_device_handle *h, unsigned char *b, size_t n) { (void)h; (void)b; (void)n; return 0; }
