/*
 * brtl_bus.c — voir brtl_bus.h.
 *
 * Copyright (C) 2026 Belksiri.
 */
#include "brtl_bus.h"

#include <stddef.h>
#include <string.h>

/* Bloc I²C du RTL2832U (wIndex = bloc << 8), requête vendor 0. */
#define BRTL_I2C_BLOCK 6u

static int is_i2c(uint8_t bm, uint8_t req, uint16_t index)
{
    return (bm & 0x60u) == 0x40u && req == 0u && (index >> 8) == BRTL_I2C_BLOCK;
}

void brtl_bus_init(brtl_bus *bus, brtl_bus_fn fn, void *ctx)
{
    if (bus == NULL) return;
    memset(bus, 0, sizeof(*bus));
    bus->fn = fn;
    bus->ctx = ctx;
}

void brtl_bus_begin_probe(brtl_bus *bus)
{
    if (bus == NULL) return;
    bus->probing = 1;
    bus->tuner_addr = 0;
    bus->pending_i2c_failures = 0;
}

void brtl_bus_finish_probe(brtl_bus *bus, uint8_t tuner_addr)
{
    if (bus == NULL) return;
    bus->probing = 0;
    bus->tuner_addr = tuner_addr;
    const uint32_t kept = bus->pending_i2c_failures < 16u ? bus->pending_i2c_failures : 16u;
    for (uint32_t i = 0; i < bus->pending_i2c_failures; i++) {
        /* Au-delà des 16 mémorisées, une sonde ne peut pas être attribuée :
         * elle compte comme sonde manquée, ce qui ne masque aucune erreur du
         * tuner détecté (ses échecs surviennent après la détection). */
        if (i < kept && tuner_addr != 0 && bus->pending_addr[i] == tuner_addr) {
            bus->tuner_failures++;
        } else {
            bus->probe_misses++;
        }
    }
    bus->pending_i2c_failures = 0;
}

static void note_device_failure(brtl_bus *bus, uint8_t bm, uint8_t req,
                                uint16_t value, uint16_t index)
{
    if (is_i2c(bm, req, index)) {
        const uint8_t addr = (uint8_t)(value & 0xFFu);
        if (bus->probing) {
            if (bus->pending_i2c_failures < 16u) {
                bus->pending_addr[bus->pending_i2c_failures] = addr;
            }
            bus->pending_i2c_failures++;
            return;
        }
        if (bus->tuner_addr != 0 && addr == bus->tuner_addr) {
            bus->tuner_failures++;
            return;
        }
    }
    bus->usb_errors++;
}

int brtl_bus_control_transfer(void *ctx, uint8_t bm, uint8_t bRequest,
                              uint16_t wValue, uint16_t wIndex, uint8_t *data,
                              uint16_t wLength, unsigned int timeout_ms)
{
    (void)timeout_ms; /* la porte du pilote impose 1000 ms */
    brtl_bus *bus = (brtl_bus *)ctx;
    if (bus == NULL || bus->fn == NULL) return BRTL_LIBUSB_ERROR_NO_DEVICE;
    if (wLength > BHRF_CTRL_MAX_DATA) return BRTL_LIBUSB_ERROR_INVALID_PARAM;
    if (wLength > 0 && data == NULL) return BRTL_LIBUSB_ERROR_INVALID_PARAM;

    BHRFControlRequest r;
    memset(&r, 0, sizeof r);
    r.bmRequestType = bm;
    r.bRequest = bRequest;
    r.wValue = wValue;
    r.wIndex = wIndex;
    r.wLength = wLength;
    const int is_in = (bm & 0x80u) != 0;
    if (!is_in && wLength > 0) memcpy(r.data, data, wLength);

    BHRFControlResponse resp;
    memset(&resp, 0, sizeof resp);
    bus->transfers++;
    if (bus->fn(bus->ctx, &r, &resp) != 0) {
        bus->usb_errors++;
        return BRTL_LIBUSB_ERROR_NO_DEVICE;
    }

    switch (resp.status) {
    case kBHRFStatusOK: {
        uint16_t got = resp.actualLength;
        if (got > wLength) got = wLength;
        if (is_in && got > 0) memcpy(data, resp.data, got);
        return (int)got;
    }
    case kBHRFStatusDenied:
        bus->denied++;
        bus->last_deny_reason = resp.denyReason;
        return BRTL_LIBUSB_ERROR_ACCESS;
    case kBHRFStatusTerminated:
    case kBHRFStatusNoDevice:
    case kBHRFStatusNotOpen:
        bus->usb_errors++;
        return BRTL_LIBUSB_ERROR_NO_DEVICE;
    default:
        bus->last_io_return = resp.ioReturn;
        note_device_failure(bus, bm, bRequest, wValue, wIndex);
        return resp.ioReturn == BRTL_IORETURN_TIMEOUT ? BRTL_LIBUSB_ERROR_TIMEOUT
                                                      : BRTL_LIBUSB_ERROR_IO;
    }
}
