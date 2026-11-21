/*
 * bhrf_diagnostics.h — Nommer ce qui échoue (pilote et application).
 *
 * C pur, compilé dans le dext ET dans l'application : classement d'un échec
 * USB à partir de son IOReturn, et libellés des statuts du protocole commun
 * (bhrf_shared.h). Aucune valeur propre à un équipement.
 *
 * Motivation : un diagnostic qui ne nomme pas ce qui a échoué oblige à
 * rebrancher le matériel pour le savoir — c'est-à-dire au moment le plus cher.
 */

#ifndef BHRF_DIAGNOSTICS_H
#define BHRF_DIAGNOSTICS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

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

/* Libellé court d'un BHRFStatus (bhrf_shared.h), pour les journaux. Jamais NULL. */
const char *bhrf_status_name(int32_t status);

#ifdef __cplusplus
}
#endif

#endif /* BHRF_DIAGNOSTICS_H */
