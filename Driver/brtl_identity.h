/*
 * brtl_identity.h — Ce qu'on sait d'un dongle avant de lui parler.
 *
 * Provenance (docs/R2_SOURCES.md) :
 *   • VID/PID 0BDA:2838 et 0BDA:2832 : identifiants USB publics, relevés sur
 *     le matériel (audit R1 § 9) — des faits, pas du code.
 *   • Chaînes « RTLSDRBlog » / « Blog V4 » / « Blog V4L » : communiquées
 *     directement par Carl Laufer (RTL-SDR Blog). Elles sont lues dans les
 *     descripteurs de chaînes USB standard (iManufacturer, iProduct — USB 2.0,
 *     § 9.6.1 et 9.6.7), jamais en écrivant l'EEPROM.
 *   • V3 : Carl Laufer — un V3 n'est pas distinguable de façon fiable d'un
 *     dongle générique par son tuner. Il est donc traité comme générique.
 *
 * Règle de sûreté : les fonctions propres à un produit (bias-tee, commutation
 * V4) ne sont proposées QUE si le produit est identifié de façon sûre. Le
 * bias-tee n'est jamais activé automatiquement.
 */
#ifndef BRTL_IDENTITY_H
#define BRTL_IDENTITY_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    kBRTLDeviceUnsupported = 0, /* VID/PID hors table */
    kBRTLDeviceGeneric     = 1, /* RTL2832U, produit non identifié (V3 inclus) */
    kBRTLDeviceBlogV4      = 2, /* « RTLSDRBlog » / « Blog V4 » */
    kBRTLDeviceBlogV4L     = 3  /* « RTLSDRBlog » / « Blog V4L » */
} BRTLDeviceKind;

/* VID/PID pris en charge au premier lot (R2). */
bool brtl_identity_usb_id_supported(uint16_t vendor_id, uint16_t product_id);

/* Identifie le produit d'après les chaînes USB déjà décodées en ASCII/UTF-8
 * (NULL si le descripteur est absent). Comparaison exacte, sensible à la
 * casse, espaces de fin ignorés : un produit n'est reconnu que s'il dit
 * exactement ce que son fabricant a déclaré. */
BRTLDeviceKind brtl_identity_classify(uint16_t vendor_id, uint16_t product_id,
                                      const char *manufacturer,
                                      const char *product);

/* Capacités offertes à l'interface pour ce produit. */
typedef struct {
    bool bias_tee_available;    /* proposé à l'utilisateur, jamais activé seul */
    bool hf_upconverter;        /* V4 / V4L : décalage HF de 28,8 MHz */
    bool band_switching;        /* V4 / V4L : plan de bandes brtl_v4 */
} BRTLCapabilities;

BRTLCapabilities brtl_identity_capabilities(BRTLDeviceKind kind);

const char *brtl_identity_kind_name(BRTLDeviceKind kind);

#ifdef __cplusplus
}
#endif

#endif /* BRTL_IDENTITY_H */
