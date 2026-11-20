#include "bhrf_ring_geometry.h"

uint32_t bhrf_clamp_in_flight(uint32_t requested, uint32_t block_count)
{
    uint32_t v;

    /* Il faut pouvoir tenir le minimum ET en garder autant pour absorber. */
    if (block_count < 2u * BHRF_MIN_INFLIGHT) {
        return 0u;
    }

    v = requested;
    if (v < BHRF_MIN_INFLIGHT) { v = BHRF_MIN_INFLIGHT; }
    if (v > BHRF_MAX_INFLIGHT) { v = BHRF_MAX_INFLIGHT; }

    /* La moitié du ring au plus. Au-delà, un slot pourrait être resoumis alors
     * qu'un transfert y écrit encore, et il ne resterait plus rien au
     * consommateur pour absorber un retard. */
    if (v > block_count / 2u) { v = block_count / 2u; }

    return v;
}

uint32_t bhrf_next_slot(uint32_t slot, uint32_t in_flight, uint32_t block_count)
{
    if (block_count == 0u) {
        return 0u;
    }
    return (slot + in_flight) % block_count;
}
