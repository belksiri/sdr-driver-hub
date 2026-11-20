/* brtl_iq.c — voir brtl_iq.h. */
#include "brtl_iq.h"

void brtl_iq_u8_to_s8(const uint8_t *in, int8_t *out, size_t count)
{
    if (in == NULL || out == NULL) return;
    /* u − 128 en complément à deux revient à inverser le bit 7. */
    for (size_t i = 0; i < count; i++) {
        out[i] = (int8_t)(uint8_t)(in[i] ^ 0x80u);
    }
}

size_t brtl_iq_u8_to_s8_count_clipped(const uint8_t *in, int8_t *out, size_t count)
{
    if (in == NULL || out == NULL) return 0;
    size_t clipped = 0;
    for (size_t i = 0; i < count; i++) {
        const uint8_t u = in[i];
        clipped += (size_t)(u == 0u || u == 255u);
        out[i] = (int8_t)(uint8_t)(u ^ 0x80u);
    }
    return clipped;
}
