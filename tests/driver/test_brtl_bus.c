/*
 * test_brtl_bus.c — Tests hôtes du transport de contrôle interne au pilote
 * (Driver/brtl_bus.c), derrière la VRAIE liste blanche (Driver/brtl_policy.c)
 * et un faux RTL2832U.
 *
 * Copyright (C) 2026 Belksiri.
 */
#include <stdio.h>
#include <string.h>

#include "brtl_bus.h"
#include "brtl_policy.h"

static int checks = 0, failures = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
    fprintf(stderr, "ÉCHEC %s:%d : %s\n", __FILE__, __LINE__, #cond); } } while (0)

/* Faux appareil : reproduit la porte du pilote (politique PUIS bus). */
typedef struct {
    int device_calls;        /* requêtes qui ont « atteint le bus »           */
    int gone;                /* périphérique retiré                            */
    int fail_i2c_addr;       /* adresse I²C qui ne répond pas (0 : aucune)     */
    int timeout_next;
    uint8_t last_out[64];
    uint16_t last_len;
} fake_dev;

static int fake_gate(void *ctx, const BHRFControlRequest *req, BHRFControlResponse *resp)
{
    fake_dev *d = (fake_dev *)ctx;
    memset(resp, 0, sizeof *resp);
    const BRTLDenyReason deny = brtl_policy_evaluate(req->bmRequestType, req->bRequest,
                                                     req->wValue, req->wIndex, req->wLength);
    if (deny != kBRTLDenyNone) {
        resp->status = kBHRFStatusDenied;
        resp->denyReason = (uint32_t)deny;
        return 0;                                   /* AUCUN accès au bus */
    }
    if (d->gone) { resp->status = kBHRFStatusTerminated; return 0; }
    d->device_calls++;
    if (d->timeout_next) {
        d->timeout_next = 0;
        resp->status = kBHRFStatusUsbError;
        resp->ioReturn = BRTL_IORETURN_TIMEOUT;
        return 0;
    }
    const int i2c = (req->wIndex >> 8) == 6;
    if (i2c && d->fail_i2c_addr && (req->wValue & 0xFF) == d->fail_i2c_addr) {
        resp->status = kBHRFStatusUsbError;
        resp->ioReturn = (int32_t)0xE000404F;   /* pipe stall */
        return 0;
    }
    resp->status = kBHRFStatusOK;
    resp->actualLength = req->wLength;
    if (req->bmRequestType & 0x80) {
        for (uint16_t i = 0; i < req->wLength; i++) resp->data[i] = (uint8_t)(0xA0 + i);
    } else {
        memcpy(d->last_out, req->data, req->wLength);
        d->last_len = req->wLength;
    }
    return 0;
}

static int unavailable_gate(void *ctx, const BHRFControlRequest *req, BHRFControlResponse *resp)
{
    (void)ctx; (void)req; (void)resp;
    return 1;
}

int main(void)
{
    fake_dev d;
    memset(&d, 0, sizeof d);
    brtl_bus bus;
    brtl_bus_init(&bus, fake_gate, &d);

    /* Écriture démodulateur (bloc 1 USB, drapeau d'écriture) : passe, données copiées. */
    uint8_t out[2] = { 0x12, 0x34 };
    CHECK(brtl_bus_control_transfer(&bus, 0x40, 0, 0x2000, 0x0110, out, 2, 300) == 2);
    CHECK(d.last_len == 2 && d.last_out[0] == 0x12 && d.last_out[1] == 0x34);

    /* Lecture : les octets reviennent dans le tampon de librtlsdr. */
    uint8_t in[4] = { 0 };
    CHECK(brtl_bus_control_transfer(&bus, 0xC0, 0, 0x2000, 0x0100, in, 4, 300) == 4);
    CHECK(in[0] == 0xA0 && in[3] == 0xA3);

    /* Refus de la liste blanche : ACCESS, et RIEN n'atteint le bus. */
    const int before = d.device_calls;
    uint8_t ee[3] = { 0, 1, 2 };
    CHECK(brtl_bus_control_transfer(&bus, 0x40, 0, 0x00A0, 0x0610, ee, 3, 300) == BRTL_LIBUSB_ERROR_ACCESS);
    CHECK(brtl_bus_control_transfer(&bus, 0x40, 0, 0x0000, 0x0310, out, 1, 300) == BRTL_LIBUSB_ERROR_ACCESS); /* bloc ROM */
    CHECK(brtl_bus_control_transfer(&bus, 0x21, 9, 0, 0, NULL, 0, 300) == BRTL_LIBUSB_ERROR_ACCESS);            /* classe */
    CHECK(d.device_calls == before);
    CHECK(bus.denied == 3);
    CHECK(bus.last_deny_reason == (uint32_t)kBRTLDenyRequestType);

    /* Longueur au-delà du tampon du pilote : refusée avant même la porte. */
    uint8_t big[80] = { 0 };
    CHECK(brtl_bus_control_transfer(&bus, 0xC0, 0, 0, 0x0100, big, 80, 300) == BRTL_LIBUSB_ERROR_INVALID_PARAM);
    CHECK(brtl_bus_control_transfer(&bus, 0xC0, 0, 0, 0x0100, NULL, 4, 300) == BRTL_LIBUSB_ERROR_INVALID_PARAM);

    /* Délai dépassé : TIMEOUT ; compté comme erreur USB. */
    d.timeout_next = 1;
    CHECK(brtl_bus_control_transfer(&bus, 0xC0, 0, 0x2000, 0x0100, in, 1, 300) == BRTL_LIBUSB_ERROR_TIMEOUT);
    CHECK(bus.usb_errors == 1);

    /* Détection du tuner : les sondes sans réponse ne sont PAS des erreurs. */
    d.fail_i2c_addr = 0xC8;  /* E4000 absent */
    brtl_bus_begin_probe(&bus);
    CHECK(brtl_bus_control_transfer(&bus, 0xC0, 0, 0x00C8, 0x0600, in, 1, 300) == BRTL_LIBUSB_ERROR_IO);
    CHECK(brtl_bus_control_transfer(&bus, 0xC0, 0, 0x00C8, 0x0600, in, 1, 300) == BRTL_LIBUSB_ERROR_IO);
    brtl_bus_finish_probe(&bus, 0x34);  /* R820T détecté */
    CHECK(bus.probe_misses == 2);
    CHECK(bus.usb_errors == 1);
    CHECK(bus.tuner_failures == 0);

    /* Après la détection, un échec I²C du tuner détecté a sa propre famille. */
    d.fail_i2c_addr = 0x34;
    CHECK(brtl_bus_control_transfer(&bus, 0xC0, 0, 0x0034, 0x0600, in, 1, 300) == BRTL_LIBUSB_ERROR_IO);
    CHECK(bus.tuner_failures == 1 && bus.usb_errors == 1);

    /* Retrait du dongle : NO_DEVICE, jamais une erreur silencieuse. */
    d.gone = 1;
    CHECK(brtl_bus_control_transfer(&bus, 0xC0, 0, 0x2000, 0x0100, in, 1, 300) == BRTL_LIBUSB_ERROR_NO_DEVICE);

    /* Porte indisponible (pilote en arrêt) : NO_DEVICE. */
    brtl_bus b2;
    brtl_bus_init(&b2, unavailable_gate, NULL);
    CHECK(brtl_bus_control_transfer(&b2, 0xC0, 0, 0x2000, 0x0100, in, 1, 300) == BRTL_LIBUSB_ERROR_NO_DEVICE);
    CHECK(brtl_bus_control_transfer(NULL, 0xC0, 0, 0, 0, in, 1, 300) == BRTL_LIBUSB_ERROR_NO_DEVICE);

    printf("brtl_bus : %d vérifications, %d échec(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
