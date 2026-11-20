/*
 * bhrf_ring_geometry.h — La géométrie du ring, en C pur.
 *
 * Extrait du pilote pour une seule raison : c'est le calcul dont une erreur ne
 * se voit PAS. Une liste blanche mal écrite laisse passer une requête, et le
 * test le dit. Une rotation de slots mal écrite laisse tout fonctionner en
 * apparence — le débit est bon, les compteurs avancent — mais l'application lit
 * un bloc pour un autre, et l'on ne s'en aperçoit qu'en regardant le spectre.
 *
 * ── L'invariant ────────────────────────────────────────────────────────────
 *
 *     slot de la n-ième complétion  ==  n modulo blockCount
 *
 * Démonstration. Les complétions d'un même pipe bulk arrivent dans l'ordre de
 * soumission (garantie USB). La soumission initiale occupe les slots
 * 0..f-1, donc slot(n) = n pour n < f. Chaque complétion resoumet f plus loin :
 *
 *     slot(n) = (slot(n - f) + f) mod C
 *
 * Par récurrence, si slot(n-f) = (n-f) mod C alors
 * slot(n) = ((n-f) mod C + f) mod C = n mod C.  ∎
 *
 * Ce qui compte ici : la récurrence tient pour TOUT f, sans condition de
 * divisibilité entre f et C. Une première version de ce module imposait à f
 * d'être une puissance de deux divisant C, sur la foi d'un raisonnement faux ;
 * le test ci-contre, qui tentait de DÉMONTRER la dérive, a montré qu'elle
 * n'existait pas. La contrainte a été retirée : une contrainte inutile est une
 * dette comme une autre.
 *
 * ── La vraie contrainte ────────────────────────────────────────────────────
 *
 * Le slot s sert aux complétions s, s+C, s+2C… La soumission du transfert de
 * la complétion n a lieu au moment de la complétion n-f. Pour qu'un slot ne
 * soit jamais resoumis alors qu'un transfert y écrit encore, il faut que la
 * complétion précédente du même slot (n-C) soit déjà passée, c'est-à-dire
 *
 *     f < C
 *
 * On impose f <= C/2 : la moitié restante du ring est ce dont dispose le
 * consommateur pour absorber un retard sans se faire distancer.
 */

#ifndef BHRF_RING_GEOMETRY_H
#define BHRF_RING_GEOMETRY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bornes du nombre de transferts simultanément en vol. Le maximum est une borne
 * de tableau, pas un réglage : au-delà, le gain est nul et la mémoire
 * immobilisée croît. */
#define BHRF_MIN_INFLIGHT 4u
#define BHRF_MAX_INFLIGHT 32u

/* Ramène la demande dans les bornes et à la moitié du ring au plus.
 * Retourne 0 si le ring est trop petit pour le minimum requis. */
uint32_t bhrf_clamp_in_flight(uint32_t requested, uint32_t block_count);

/* Le slot que doit viser la resoumission après une complétion sur `slot`. */
uint32_t bhrf_next_slot(uint32_t slot, uint32_t in_flight, uint32_t block_count);

#ifdef __cplusplus
}
#endif

#endif /* BHRF_RING_GEOMETRY_H */
