/*
 * brtl_rtl_controller.c — voir brtl_rtl_controller.h.
 *
 * EXPERIMENTAL — GPL — DEVELOPMENT ONLY. NOT APPROVED FOR APP STORE DISTRIBUTION.
 * Copyright (C) 2026 Belksiri Spectrum. Distribué avec librtlsdr sous
 * GPL-2.0-or-later dans ce prototype.
 */
#include "brtl_rtl_controller.h"

#include <stdlib.h>
#include <string.h>

#include "rtl-sdr.h"

/* Accès aux chaînes déjà lues par librtlsdr (rtlsdr_get_usb_strings relit
 * l'appareil ; on préfère une seule lecture, faite ici). */
struct brtl_ctrl {
    rtlsdr_dev_t *dev;
    int transport_id;
    int bias_tee;
    int lost;
};

static const char *tuner_name(enum rtlsdr_tuner t)
{
    switch (t) {
    case RTLSDR_TUNER_R820T:  return "R820T/R820T2/R860";
    case RTLSDR_TUNER_R828D:  return "R828D";
    case RTLSDR_TUNER_E4000:  return "E4000";
    case RTLSDR_TUNER_FC0012: return "FC0012";
    case RTLSDR_TUNER_FC0013: return "FC0013";
    case RTLSDR_TUNER_FC2580: return "FC2580";
    case RTLSDR_TUNER_UNKNOWN:
    default:                  return "none";
    }
}

/* Adresses I²C sondées par librtlsdr (librtlsdr.c, tuner_*.h). */
static uint8_t tuner_i2c_addr(enum rtlsdr_tuner t)
{
    switch (t) {
    case RTLSDR_TUNER_E4000:  return 0xC8;
    case RTLSDR_TUNER_FC0012: return 0xC6;
    case RTLSDR_TUNER_FC0013: return 0xC6;
    case RTLSDR_TUNER_FC2580: return 0xAC;
    case RTLSDR_TUNER_R820T:  return 0x34;
    case RTLSDR_TUNER_R828D:  return 0x74;
    default:                  return 0;
    }
}

brtl_status brtl_ctrl_open(const brtl_transport *transport, brtl_ctrl **out,
                           brtl_device_info *info)
{
    if (transport == NULL || out == NULL) return BRTL_ERR_ARGUMENT;
    *out = NULL;
    if (info) memset(info, 0, sizeof(*info));

    const int id = brtl_transport_register(transport);
    if (id == 0) return BRTL_ERR_TRANSPORT;

    rtlsdr_dev_t *dev = NULL;
    if (rtlsdr_open_fd(&dev, id) != 0 || dev == NULL) {
        brtl_transport_unregister(id);
        return BRTL_ERR_OPEN;
    }

    const enum rtlsdr_tuner t = rtlsdr_get_tuner_type(dev);
    if (info) {
        info->tuner_type = (int)t;
        info->tuner_i2c_addr = tuner_i2c_addr(t);
        strncpy(info->tuner_name, tuner_name(t), sizeof(info->tuner_name) - 1);
        char m[256] = {0}, p[256] = {0};
        if (rtlsdr_get_usb_strings(dev, m, p, NULL) == 0) {
            strncpy(info->manufacturer, m, sizeof(info->manufacturer) - 1);
            strncpy(info->product, p, sizeof(info->product) - 1);
        }
        rtlsdr_get_xtal_freq(dev, &info->rtl_xtal_hz, &info->tuner_xtal_hz);
    }

    brtl_status refusal = BRTL_OK;
    if (t == RTLSDR_TUNER_UNKNOWN) refusal = BRTL_ERR_NO_TUNER;
    else if (t != RTLSDR_TUNER_R820T) refusal = BRTL_ERR_UNSUPPORTED_TUNER;
    if (refusal != BRTL_OK) {
        rtlsdr_close(dev);
        brtl_transport_unregister(id);
        return refusal;
    }

    brtl_ctrl *c = calloc(1, sizeof(*c));
    if (c == NULL) {
        rtlsdr_close(dev);
        brtl_transport_unregister(id);
        return BRTL_ERR_ARGUMENT;
    }
    c->dev = dev;
    c->transport_id = id;
    /* Bias-tee : jamais actif tant que l'utilisateur ne l'a pas confirmé. */
    rtlsdr_set_bias_tee(dev, 0);
    c->bias_tee = 0;
    *out = c;
    return BRTL_OK;
}

brtl_status brtl_ctrl_set_sample_rate(brtl_ctrl *c, uint32_t rate_hz, uint32_t *applied)
{
    if (c == NULL) return BRTL_ERR_ARGUMENT;
    if (rtlsdr_set_sample_rate(c->dev, rate_hz) != 0) return BRTL_ERR_SAMPLE_RATE;
    if (applied) *applied = rtlsdr_get_sample_rate(c->dev);
    return BRTL_OK;
}

/* Fonctions publiques de librtlsdr (non déclarées dans rtl-sdr.h). */
void rtlsdr_set_i2c_repeater(rtlsdr_dev_t *dev, int on);
int rtlsdr_i2c_read(rtlsdr_dev_t *dev, uint8_t i2c_addr, uint8_t *buffer, int len);

int brtl_ctrl_pll_locked(brtl_ctrl *c)
{
    if (c == NULL || c->lost) return -1;
    uint8_t raw[3] = {0};
    rtlsdr_set_i2c_repeater(c->dev, 1);
    const int r = rtlsdr_i2c_read(c->dev, 0x34, raw, 3);
    rtlsdr_set_i2c_repeater(c->dev, 0);
    if (r < 3) return -1;
    /* Le R82xx transmet ses octets bits inversés : le bit 6 de l'octet 2
     * (verrouillage de la PLL) arrive sur le bit 1. */
    return (raw[2] & 0x02) ? 1 : 0;
}

brtl_status brtl_ctrl_set_center_frequency(brtl_ctrl *c, uint32_t freq_hz, uint32_t *applied)
{
    if (c == NULL) return BRTL_ERR_ARGUMENT;
    if (rtlsdr_set_center_freq(c->dev, freq_hz) != 0) return BRTL_ERR_FREQUENCY;
    if (applied) *applied = rtlsdr_get_center_freq(c->dev);
    const int locked = brtl_ctrl_pll_locked(c);
    if (locked < 0) return BRTL_ERR_USB;
    return locked ? BRTL_OK : BRTL_ERR_PLL_UNLOCKED;
}

int brtl_ctrl_gains(brtl_ctrl *c, int *gains, int max)
{
    if (c == NULL) return 0;
    const int n = rtlsdr_get_tuner_gains(c->dev, NULL);
    if (n <= 0 || gains == NULL || max <= 0) return n < 0 ? 0 : n;
    int tmp[64];
    const int got = rtlsdr_get_tuner_gains(c->dev, n <= 64 ? tmp : NULL);
    const int k = got < max ? got : max;
    for (int i = 0; i < k && i < 64; i++) gains[i] = tmp[i];
    return k;
}

brtl_status brtl_ctrl_set_gain(brtl_ctrl *c, int gain, int *applied)
{
    if (c == NULL) return BRTL_ERR_ARGUMENT;
    if (gain < 0) {
        if (rtlsdr_set_tuner_gain_mode(c->dev, 0) != 0) return BRTL_ERR_GAIN;
        if (applied) *applied = -1;
        return BRTL_OK;
    }
    int list[64];
    const int n = brtl_ctrl_gains(c, list, 64);
    int best = gain;
    if (n > 0) {
        best = list[0];
        for (int i = 1; i < n; i++) {
            if (abs(list[i] - gain) < abs(best - gain)) best = list[i];
        }
    }
    if (rtlsdr_set_tuner_gain_mode(c->dev, 1) != 0) return BRTL_ERR_GAIN;
    if (rtlsdr_set_tuner_gain(c->dev, best) != 0) return BRTL_ERR_GAIN;
    if (applied) *applied = best;
    return BRTL_OK;
}

brtl_status brtl_ctrl_set_bias_tee(brtl_ctrl *c, int on)
{
    if (c == NULL) return BRTL_ERR_ARGUMENT;
    if (rtlsdr_set_bias_tee(c->dev, on ? 1 : 0) != 0) return BRTL_ERR_USB;
    c->bias_tee = on ? 1 : 0;
    return BRTL_OK;
}

int brtl_ctrl_bias_tee(const brtl_ctrl *c) { return c ? c->bias_tee : 0; }

brtl_status brtl_ctrl_reset_stream(brtl_ctrl *c)
{
    if (c == NULL) return BRTL_ERR_ARGUMENT;
    return rtlsdr_reset_buffer(c->dev) == 0 ? BRTL_OK : BRTL_ERR_USB;
}

void brtl_ctrl_mark_lost(brtl_ctrl *c)
{
    if (c == NULL) return;
    c->lost = 1;
    /* Plus rien ne doit partir sur le bus : le transport est retiré, toute
     * requête de librtlsdr reçoit LIBUSB_ERROR_NO_DEVICE. */
    brtl_transport_unregister(c->transport_id);
    c->transport_id = 0;
}

void brtl_ctrl_close(brtl_ctrl *c)
{
    if (c == NULL) return;
    if (!c->lost && c->bias_tee) rtlsdr_set_bias_tee(c->dev, 0);
    rtlsdr_close(c->dev);
    if (c->transport_id) brtl_transport_unregister(c->transport_id);
    free(c);
}

const char *brtl_status_name(brtl_status s)
{
    switch (s) {
    case BRTL_OK:                    return "ok";
    case BRTL_ERR_ARGUMENT:          return "badArgument";
    case BRTL_ERR_TRANSPORT:         return "noTransport";
    case BRTL_ERR_OPEN:              return "openFailed";
    case BRTL_ERR_UNSUPPORTED_TUNER: return "unsupportedTuner";
    case BRTL_ERR_NO_TUNER:          return "noTuner";
    case BRTL_ERR_SAMPLE_RATE:       return "invalidSampleRate";
    case BRTL_ERR_FREQUENCY:         return "frequencyFailed";
    case BRTL_ERR_GAIN:              return "gainFailed";
    case BRTL_ERR_USB:               return "usbError";
    case BRTL_ERR_PLL_UNLOCKED:      return "pllUnlocked";
    }
    return "unknown";
}

/* ── Commandes rtl_tcp supplémentaires (SDR Driver Hub) ──────────────────── */

brtl_status brtl_ctrl_set_freq_correction(brtl_ctrl *c, int ppm)
{
    if (c == NULL) return BRTL_ERR_ARGUMENT;
    const int r = rtlsdr_set_freq_correction(c->dev, ppm);
    /* librtlsdr renvoie -2 quand la valeur est déjà en place. */
    return (r == 0 || r == -2) ? BRTL_OK : BRTL_ERR_FREQUENCY;
}

brtl_status brtl_ctrl_set_rtl_agc(brtl_ctrl *c, int on)
{
    if (c == NULL) return BRTL_ERR_ARGUMENT;
    return rtlsdr_set_agc_mode(c->dev, on ? 1 : 0) == 0 ? BRTL_OK : BRTL_ERR_GAIN;
}

brtl_status brtl_ctrl_set_direct_sampling(brtl_ctrl *c, int mode)
{
    if (c == NULL || mode < 0 || mode > 2) return BRTL_ERR_ARGUMENT;
    return rtlsdr_set_direct_sampling(c->dev, mode) == 0 ? BRTL_OK : BRTL_ERR_USB;
}

brtl_status brtl_ctrl_set_offset_tuning(brtl_ctrl *c, int on)
{
    if (c == NULL) return BRTL_ERR_ARGUMENT;
    /* -2 sur R82xx (sans objet), comme librtlsdr : pas une erreur. */
    const int r = rtlsdr_set_offset_tuning(c->dev, on ? 1 : 0);
    return (r == 0 || r == -2) ? BRTL_OK : BRTL_ERR_USB;
}

int brtl_ctrl_tuner_type(const brtl_ctrl *c)
{
    return c == NULL ? 0 : (int)rtlsdr_get_tuner_type(c->dev);
}
