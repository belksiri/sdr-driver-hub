/*
 * bhrf_requests.h — Nommer ce qui échoue.
 *
 * Trois tables, en C pur, compilées dans le dext ET dans les applications ET
 * dans les tests hôtes :
 *
 *   1. le NOM symbolique de chaque requête vendor ;
 *   2. la version d'API USB MINIMALE que chaque requête exige ;
 *   3. la classification d'un échec USB à partir de son kern_return_t.
 *
 * Motivation. Sur le premier essai matériel, l'application n'a su dire que
 * « L'appareil a répondu : usb_error ». Ni quelle requête, ni pourquoi, ni si
 * c'était grave. Un diagnostic qui ne nomme pas ce qui a échoué oblige à
 * rebrancher le matériel pour le savoir — c'est-à-dire au moment le plus cher.
 *
 * Les versions minimales sont relevées dans les macros USB_API_REQUIRED du
 * hackrf.c vendored (libhackrf v2024.02.1). NE PAS les compléter de mémoire :
 * la garde et la requête sont dans deux endroits différents du fichier, et
 * l'association a été vérifiée fonction par fonction.
 */

#ifndef BHRF_REQUESTS_H
#define BHRF_REQUESTS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Nom symbolique d'une requête vendor. Jamais NULL : « INCONNU(n) » pour une
 * valeur hors table, écrit dans un tampon statique par appel. */
const char *bhrf_request_name(uint8_t b_request);

/*
 * Version d'API USB minimale (bcdDevice) exigée par la requête, ou 0 si la
 * requête existe depuis toujours.
 *
 * Relevé des USB_API_REQUIRED du hackrf.c vendored :
 *
 *     RESET (30) .................... 0x0102
 *     SPIFLASH_STATUS (33) .......... 0x0103
 *     SPIFLASH_CLEAR_STATUS (34) .... 0x0103
 *     CPLD_CHECKSUM (36) ............ 0x0103
 *     BOARD_REV_READ (45) ........... 0x0106
 *     SUPPORTED_PLATFORM_READ (46) .. 0x0106
 *     SET_LEDS (47) ................. 0x0107
 *     SET_USER_BIAS_T_OPTS (48) ..... 0x0108
 *
 * Toutes les autres requêtes de la liste blanche — SET_TRANSCEIVER_MODE,
 * SAMPLE_RATE_SET, BASEBAND_FILTER_BANDWIDTH_SET, BOARD_ID_READ,
 * VERSION_STRING_READ, SET_FREQ, AMP_ENABLE, SET_LNA_GAIN, SET_VGA_GAIN,
 * ANTENNA_ENABLE — n'ont AUCUNE garde de version dans libhackrf.
 */
uint16_t bhrf_min_usb_api(uint8_t b_request);

/* Vrai si le firmware annoncé par `bcd_device` sait traiter cette requête. */
int bhrf_request_supported_by(uint8_t b_request, uint16_t bcd_device);

/*
 * Ce qu'un échec USB veut dire. La distinction n'est pas cosmétique : un STALL
 * dit « le firmware ne connaît pas cette requête » et se corrige en
 * conditionnant l'appel, alors qu'un timeout dit « le lien est en cause » et se
 * corrige en changeant de câble ou d'alimentation. Les confondre envoie
 * chercher la panne au mauvais endroit.
 */
typedef enum {
    kBHRFUsbOk = 0,
    kBHRFUsbStalled,        /* le périphérique a renvoyé un STALL            */
    kBHRFUsbNoDevice,       /* débranché                                      */
    kBHRFUsbNotResponding,  /* lien muet                                      */
    kBHRFUsbTimeout,        /* délai dépassé                                  */
    kBHRFUsbUnderrun,       /* moins d'octets que demandé                     */
    kBHRFUsbOverrun,        /* plus d'octets que demandé                      */
    kBHRFUsbAborted,        /* transfert annulé                               */
    kBHRFUsbBadArgument,    /* paramètre refusé par la couche USB             */
    kBHRFUsbOther           /* autre IOReturn                                 */
} BHRFUsbFailureKind;

BHRFUsbFailureKind bhrf_classify_usb_failure(int32_t io_return);

/* Nom court, pour les journaux. Jamais NULL. */
const char *bhrf_usb_failure_kind_name(BHRFUsbFailureKind kind);

/* Phrase actionnable, en français, destinée à l'utilisateur. Jamais NULL. */
const char *bhrf_usb_failure_kind_advice(BHRFUsbFailureKind kind);

#ifdef __cplusplus
}
#endif

#endif /* BHRF_REQUESTS_H */
