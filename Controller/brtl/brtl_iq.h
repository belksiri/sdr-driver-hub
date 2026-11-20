/*
 * brtl_iq.h — Conversion des échantillons IQ RTL-SDR vers le format du projet.
 *
 * Provenance (voir docs/R2_SOURCES.md) : arithmétique propre au projet.
 *   • Le RTL2832U livre de l'IQ entrelacé sur 8 bits NON signés, centré sur
 *     ≈ 127,5 (fait public, audit R1 § 8 « Format »).
 *   • Le reste de Belksiri (PMIQ, enregistreur natif, canal de démodulation)
 *     attend de l'IQ entrelacé sur 8 bits SIGNÉS (convention HackRF).
 *
 * Conversion retenue : s = u − 128 (bascule du bit de poids fort). Elle est
 * exacte, sans perte, réversible, et ne déplace la valeur médiane qu'au demi-
 * LSB près (127,5 → −0,5) : ce résidu est un DC de −0,5 LSB que le retrait de
 * DC du démodulateur traite déjà. Aucun arrondi, aucune saturation.
 */
#ifndef BRTL_IQ_H
#define BRTL_IQ_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Convertit [count] octets non signés en signés. `in` et `out` peuvent être
 * le même tampon (conversion en place). */
void brtl_iq_u8_to_s8(const uint8_t *in, int8_t *out, size_t count);

/* Même conversion, et renvoie le nombre de composantes (I ou Q) écrêtées,
 * c'est-à-dire aux butées de l'ADC : 0 ou 255 en entrée (−128 ou 127 en
 * sortie). Sert au taux d'écrêtage affiché : un gain trop fort sature l'ADC. */
size_t brtl_iq_u8_to_s8_count_clipped(const uint8_t *in, int8_t *out, size_t count);

#ifdef __cplusplus
}
#endif

#endif /* BRTL_IQ_H */
