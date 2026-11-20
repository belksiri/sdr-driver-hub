/*
 * brtl_rtl_controller.h — Couche B : contrôleur RTL-SDR isolé (librtlsdr).
 *
 * EXPERIMENTAL — GPL — DEVELOPMENT ONLY. NOT APPROVED FOR APP STORE DISTRIBUTION.
 * Lié à librtlsdr (osmocom, GPL-2.0-or-later). Voir THIRD_PARTY_NOTICES_RTLSDR.md.
 *
 * L'application (Swift) ne voit que cette interface étroite. Derrière elle,
 * librtlsdr programme le RTL2832U et le tuner par le transport du dext. Un
 * contrôleur indépendant pourra la remplacer sans changer l'appelant.
 *
 * Périmètre de cette phase : tuner R82xx à l'adresse 0x34 (R820T, R820T2,
 * R860 — librtlsdr les nomme tous « R820T »). R828D (Blog V4), E4000, FC0012,
 * FC0013, FC2580 et « aucun tuner » sont REFUSÉS à l'ouverture.
 * Le bias-tee (GPIO 0) est forcé à OFF à l'ouverture et à la fermeture.
 */
#ifndef BRTL_RTL_CONTROLLER_H
#define BRTL_RTL_CONTROLLER_H

#include <stddef.h>
#include <stdint.h>

#include "brtl_transport.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BRTL_OK = 0,
    BRTL_ERR_ARGUMENT = -1,
    BRTL_ERR_TRANSPORT = -2,          /* plus de transport disponible           */
    BRTL_ERR_OPEN = -3,               /* librtlsdr n'a pas pu initialiser        */
    BRTL_ERR_UNSUPPORTED_TUNER = -4,  /* tuner reconnu mais hors périmètre       */
    BRTL_ERR_NO_TUNER = -5,           /* aucun tuner détecté                     */
    BRTL_ERR_SAMPLE_RATE = -6,        /* débit refusé par le rééchantillonneur   */
    BRTL_ERR_FREQUENCY = -7,
    BRTL_ERR_GAIN = -8,
    BRTL_ERR_USB = -9,
    BRTL_ERR_PLL_UNLOCKED = -10       /* tuner accordé mais PLL non verrouillée  */
} brtl_status;

typedef struct brtl_ctrl brtl_ctrl;

typedef struct {
    int tuner_type;           /* enum rtlsdr_tuner de librtlsdr               */
    char tuner_name[24];      /* "R820T/R820T2/R860", "R828D", "E4000"…       */
    char manufacturer[64];    /* chaîne USB iManufacturer                     */
    char product[64];         /* chaîne USB iProduct                          */
    uint32_t rtl_xtal_hz;
    uint32_t tuner_xtal_hz;
    uint8_t tuner_i2c_addr;   /* adresse I²C (8 bits) du tuner détecté, 0 si
                               * aucun : sépare les sondes de librtlsdr vers
                               * les tuners absents des vraies commandes     */
} brtl_device_info;

/* Ouvre le RTL2832U par [transport], détecte et initialise le tuner.
 * En cas de refus (tuner hors périmètre), [info] est tout de même rempli
 * pour que l'appelant puisse le nommer, et le périphérique est refermé. */
brtl_status brtl_ctrl_open(const brtl_transport *transport, brtl_ctrl **out,
                           brtl_device_info *info);

/* Débit d'échantillonnage. [applied_hz] reçoit le débit réellement produit
 * par le rééchantillonneur (il peut différer de quelques Hz). */
brtl_status brtl_ctrl_set_sample_rate(brtl_ctrl *c, uint32_t rate_hz,
                                      uint32_t *applied_hz);

/* Accorde, puis relit le bit de verrouillage de la PLL du tuner (librtlsdr
 * ne fait qu'avertir sur stderr) : BRTL_ERR_PLL_UNLOCKED s'il n'est pas mis. */
brtl_status brtl_ctrl_set_center_frequency(brtl_ctrl *c, uint32_t freq_hz,
                                           uint32_t *applied_hz);

/* 1 verrouillée, 0 non verrouillée, -1 lecture impossible. */
int brtl_ctrl_pll_locked(brtl_ctrl *c);

/* gain_tenth_db < 0 : gain automatique du tuner ; sinon gain manuel, arrondi
 * au palier pris en charge le plus proche ([applied_tenth_db]). */
brtl_status brtl_ctrl_set_gain(brtl_ctrl *c, int gain_tenth_db, int *applied_tenth_db);

/* Paliers de gain du tuner, en dixièmes de dB. Renvoie leur nombre. */
int brtl_ctrl_gains(brtl_ctrl *c, int *gains, int max);

/* Bias-tee V3 (GPIO 0). L'appelant porte la confirmation de l'utilisateur. */
brtl_status brtl_ctrl_set_bias_tee(brtl_ctrl *c, int on);
int brtl_ctrl_bias_tee(const brtl_ctrl *c);

/* Prépare un démarrage de flux (vidage du FIFO de l'endpoint). */
brtl_status brtl_ctrl_reset_stream(brtl_ctrl *c);

/* Le périphérique a disparu : la fermeture n'enverra plus rien sur le bus. */
void brtl_ctrl_mark_lost(brtl_ctrl *c);

/* Bias-tee OFF (si le périphérique est présent), puis libération. */
void brtl_ctrl_close(brtl_ctrl *c);

/* Commandes rtl_tcp supplémentaires (SDR Driver Hub). */
brtl_status brtl_ctrl_set_freq_correction(brtl_ctrl *c, int ppm);
brtl_status brtl_ctrl_set_rtl_agc(brtl_ctrl *c, int on);
brtl_status brtl_ctrl_set_direct_sampling(brtl_ctrl *c, int mode); /* 0 off, 1 I, 2 Q */
brtl_status brtl_ctrl_set_offset_tuning(brtl_ctrl *c, int on);
int brtl_ctrl_tuner_type(const brtl_ctrl *c);

const char *brtl_status_name(brtl_status s);

#ifdef __cplusplus
}
#endif

#endif /* BRTL_RTL_CONTROLLER_H */
