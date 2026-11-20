/*
 * bhrf_policy.h — La liste blanche des requêtes de contrôle USB.
 *
 * Ce module est du C pur, SANS aucune dépendance à DriverKit, à IOKit ou à USB.
 * C'est délibéré et c'est le point le plus important du prototype : la règle de
 * sécurité qui fait tout l'argumentaire du dossier Apple doit être vérifiable
 * par un test hôte, en une seconde, sans iPad, sans Mac particulier et sans
 * HackRF. Un filtre de sécurité qu'on ne peut tester que branché au matériel est
 * un filtre qu'on ne teste pas.
 *
 * Le dext compile ce fichier tel quel et appelle `bhrf_policy_evaluate` AVANT
 * toute tentative de transfert. La valeur de retour kBHRFDenyNone est la seule
 * qui autorise un accès au bus.
 */

#ifndef BHRF_POLICY_H
#define BHRF_POLICY_H

#include "bhrf_shared.h"

#ifdef __cplusplus
extern "C" {
#endif

/* bmRequestType attendus. Relevés dans hackrf.c : toutes les requêtes du HackRF
 * sont de type VENDOR et de destinataire DEVICE.
 *   LIBUSB_ENDPOINT_OUT(0x00) | LIBUSB_REQUEST_TYPE_VENDOR(0x40) | RECIPIENT_DEVICE(0x00) = 0x40
 *   LIBUSB_ENDPOINT_IN (0x80) | LIBUSB_REQUEST_TYPE_VENDOR(0x40) | RECIPIENT_DEVICE(0x00) = 0xC0 */
#define BHRF_BMREQ_OUT_VENDOR_DEVICE 0x40u
#define BHRF_BMREQ_IN_VENDOR_DEVICE  0xC0u

/* Direction attendue pour une requête autorisée.
 *
 * Attention, contre-intuitif et vérifié dans le code vendored : SET_LNA_GAIN et
 * SET_VGA_GAIN sont des « set » mais voyagent en direction IN, parce que le
 * HackRF renvoie un octet d'acquittement (hackrf.c, hackrf_set_lna_gain et
 * hackrf_set_vga_gain utilisent LIBUSB_ENDPOINT_IN). Une liste blanche écrite
 * de mémoire refuserait les réglages de gain. */
typedef enum {
    kBHRFDirOut = 0, /* hôte → appareil, bmRequestType 0x40 */
    kBHRFDirIn  = 1  /* appareil → hôte, bmRequestType 0xC0 */
} BHRFDirection;

typedef struct {
    uint8_t  bRequest;
    uint8_t  direction;   /* BHRFDirection                                   */
    uint16_t maxLength;   /* wLength maximal accepté pour cette requête      */
} BHRFAllowedRequest;

/* La liste blanche, exposée pour que les tests puissent l'énumérer plutôt que
 * de la recopier — une liste de test recopiée finit toujours par diverger. */
extern const BHRFAllowedRequest kBHRFAllowedRequests[];
extern const unsigned           kBHRFAllowedRequestCount;

/*
 * Évalue une requête. Retourne kBHRFDenyNone si et seulement si elle peut être
 * transmise au bus USB ; sinon le motif exact du refus.
 *
 * Aucune exception, aucun mode « permissif », aucun drapeau de contournement :
 * l'absence de porte dérobée fait partie de ce qui est présenté à Apple.
 */
BHRFDenyReason bhrf_policy_evaluate(const BHRFControlRequest *req);

/* Libellé court et stable d'un motif de refus, pour les journaux et l'interface.
 * Jamais NULL. */
const char *bhrf_deny_reason_name(BHRFDenyReason reason);

/* Libellé d'un statut, même contrat. */
const char *bhrf_status_name(BHRFStatus status);

#ifdef __cplusplus
}
#endif

#endif /* BHRF_POLICY_H */
