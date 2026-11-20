/*
 * brtl_policy.h — Liste blanche des requêtes de contrôle du dext RTL-SDR.
 *
 * EXPERIMENTAL — GPL — DEVELOPMENT ONLY (voir docs/RTLSDR_GPL_PROTOTYPE.md).
 * Ce fichier est notre code ; l'encodage qu'il filtre est celui qu'emploie
 * librtlsdr (osmocom, GPL-2.0-or-later), que le prototype embarque.
 *
 * Le pilote est STRICTEMENT en réception. Évaluée dans le dext AVANT tout
 * accès au bus : un refus ne produit aucun octet sur l'USB.
 *
 * Encodage RTL2832U (requête vendeur n° 0) :
 *   lecture   bmRequestType 0xC0, wValue = adresse, wIndex = bloc << 8
 *   écriture  bmRequestType 0x40, wValue = adresse, wIndex = (bloc << 8) | 0x10
 *   démod     bloc 0 ; wValue = (registre << 8) | 0x20 ; wIndex = page (| 0x10)
 *   I²C       bloc 6 ; wValue = adresse I²C sur 8 bits
 * Blocs : 0 démodulateur, 1 USB, 2 système (GPIO), 3 tuner, 4 ROM, 5 IR, 6 I²C.
 *
 * Autorisé : lectures et écritures des blocs 0, 1, 2 et 6 ; descripteurs
 * standard (périphérique, configuration, chaînes) en lecture.
 * Refusé : tout autre type de requête ; blocs 3, 4, 5 ; écriture de DONNÉES
 * dans l'EEPROM (I²C 0xA0, longueur ≥ 2). L'écriture d'UN octet vers 0xA0
 * est permise : c'est le positionnement du pointeur d'adresse qui précède une
 * lecture, et il n'écrit rien dans la mémoire.
 */
#ifndef BRTL_POLICY_H
#define BRTL_POLICY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BRTL_BMREQ_IN_VENDOR    0xC0u
#define BRTL_BMREQ_OUT_VENDOR   0x40u
#define BRTL_BMREQ_IN_STANDARD  0x80u
#define BRTL_REQ_GET_DESCRIPTOR 6u
#define BRTL_CTRL_MAX_DATA      64u
#define BRTL_EEPROM_I2C_ADDR    0xA0u

typedef enum {
    kBRTLDenyNone              = 0,
    kBRTLDenyRequestType       = 1, /* ni vendeur IN/OUT, ni descripteur standard */
    kBRTLDenyUnknownRequest    = 2, /* bRequest ≠ 0 (vendeur) ou ≠ 6 (standard)  */
    kBRTLDenyBlock             = 3, /* bloc tuner direct, ROM ou IR               */
    kBRTLDenyEepromWrite       = 4, /* écriture de données dans l'EEPROM          */
    kBRTLDenyLengthTooLarge    = 5, /* wLength > BRTL_CTRL_MAX_DATA               */
    kBRTLDenyDescriptorType    = 6, /* descripteur autre que 1, 2 ou 3           */
    kBRTLDenyWriteFlag         = 7  /* sens et drapeau d'écriture incohérents     */
} BRTLDenyReason;

BRTLDenyReason brtl_policy_evaluate(uint8_t bmRequestType, uint8_t bRequest,
                                    uint16_t wValue, uint16_t wIndex,
                                    uint16_t wLength);

const char *brtl_deny_reason_name(BRTLDenyReason reason);

#ifdef __cplusplus
}
#endif

#endif /* BRTL_POLICY_H */
