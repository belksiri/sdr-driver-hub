/*
 * brtl_bus.h — Le transport de contrôle de librtlsdr DANS le pilote.
 *
 * Copyright (C) 2026 Belksiri.
 *
 * librtlsdr (compilée dans le dext) émet ses transferts de contrôle par le
 * shim libusb, qui appelle `brtl_bus_control_transfer`. Celui-ci emballe la
 * requête dans un BHRFControlRequest et la confie à `bus` — dans le pilote,
 * la porte unique RtlControlRequest : liste blanche brtl_policy PUIS
 * DeviceRequest. Rien ne contourne la liste blanche, pas même librtlsdr.
 *
 * Les codes rendus sont ceux de libusb, avec les correspondances exactes du
 * pont précédent de SDR Driver Hub (RtlControlBridge.swift) :
 *   refus de la politique → ACCESS ; périphérique parti → NO_DEVICE ;
 *   délai dépassé → TIMEOUT ; autre échec → IO.
 *
 * Quatre familles d'échec, jamais confondues : refus, erreurs USB, sondes
 * I²C sans réponse pendant la détection du tuner (attendues), échecs I²C
 * adressés au tuner détecté.
 */
#ifndef BRTL_BUS_H
#define BRTL_BUS_H

#include <stdint.h>

#include "bhrf_shared.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Codes libusb (valeurs normatives de libusb 1.0). */
#define BRTL_LIBUSB_ERROR_IO            (-1)
#define BRTL_LIBUSB_ERROR_INVALID_PARAM (-2)
#define BRTL_LIBUSB_ERROR_ACCESS        (-3)
#define BRTL_LIBUSB_ERROR_NO_DEVICE     (-4)
#define BRTL_LIBUSB_ERROR_TIMEOUT       (-7)

/* kIOReturnTimeout. */
#define BRTL_IORETURN_TIMEOUT ((int32_t)0xE00002D6)

/* Exécute la requête. Renvoie 0 si l'appel a eu lieu (`resp` est alors
 * rempli, refus compris), une autre valeur si la porte est indisponible. */
typedef int (*brtl_bus_fn)(void *ctx, const BHRFControlRequest *req,
                           BHRFControlResponse *resp);

typedef struct brtl_bus {
    brtl_bus_fn fn;
    void *ctx;

    int probing;            /* détection du tuner en cours                  */
    uint8_t tuner_addr;     /* adresse I²C du tuner détecté, 0 si inconnue   */

    uint64_t transfers;
    uint64_t denied;
    uint64_t usb_errors;
    uint64_t probe_misses;
    uint64_t tuner_failures;
    uint32_t last_deny_reason;
    int32_t last_io_return;

    /* Échecs I²C de la détection, classés à la fin de celle-ci. */
    uint32_t pending_i2c_failures;
    uint8_t pending_addr[16];
} brtl_bus;

void brtl_bus_init(brtl_bus *bus, brtl_bus_fn fn, void *ctx);

void brtl_bus_begin_probe(brtl_bus *bus);
void brtl_bus_finish_probe(brtl_bus *bus, uint8_t tuner_addr);

/* Signature de brtl_transport.control_transfer, ctx = brtl_bus*. */
int brtl_bus_control_transfer(void *ctx, uint8_t bmRequestType, uint8_t bRequest,
                              uint16_t wValue, uint16_t wIndex, uint8_t *data,
                              uint16_t wLength, unsigned int timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* BRTL_BUS_H */
