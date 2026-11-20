/*
 * brtl_transport.h — Transport abstrait entre librtlsdr et le pilote DriverKit.
 *
 * EXPERIMENTAL — GPL — DEVELOPMENT ONLY. NOT APPROVED FOR APP STORE DISTRIBUTION.
 * Ce fichier fait partie du composant GPL du prototype (il est lié à
 * librtlsdr) : voir THIRD_PARTY_NOTICES_RTLSDR.md.
 *
 * librtlsdr ne parle qu'à libusb. Sur iPadOS, libusb n'existe pas : le shim
 * `libusb.h` de ce dossier traduit les quelques appels nécessaires vers ce
 * transport, implémenté côté application par le user client du dext
 * `BelksiriRtlSdrDriver`. Le flux bulk ne passe PAS par ici : le dext le
 * dépose directement dans le ring partagé.
 *
 * Un futur contrôleur indépendant pourra remplacer librtlsdr derrière la même
 * interface, sans toucher au pilote ni à l'application.
 */
#ifndef BRTL_TRANSPORT_H
#define BRTL_TRANSPORT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct brtl_transport {
    void *ctx;
    /* Transfert de contrôle synchrone. Renvoie le nombre d'octets transférés,
     * ou un code d'erreur libusb négatif (LIBUSB_ERROR_*). Un refus de la
     * politique du pilote est rendu par LIBUSB_ERROR_ACCESS. */
    int (*control_transfer)(void *ctx, uint8_t bmRequestType, uint8_t bRequest,
                            uint16_t wValue, uint16_t wIndex, uint8_t *data,
                            uint16_t wLength, unsigned int timeout_ms);
} brtl_transport;

/* Enregistre un transport et renvoie un identifiant > 0, à passer à
 * rtlsdr_open_fd() à la place d'un descripteur de fichier. 0 si plein. */
int brtl_transport_register(const brtl_transport *transport);

/* Retire le transport. Les poignées libusb ouvertes dessus deviennent
 * inutilisables (LIBUSB_ERROR_NO_DEVICE). */
void brtl_transport_unregister(int id);

#ifdef __cplusplus
}
#endif

#endif /* BRTL_TRANSPORT_H */
