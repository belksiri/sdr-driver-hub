/*
 * brtl_client_policy.c — voir brtl_client_policy.h.
 *
 * Copyright (C) 2026 Belksiri.
 */
#include "brtl_client_policy.h"

#include <stddef.h>

#include "bhrf_shared.h"

static int str_equal(const char *a, const char *b)
{
    if (a == NULL || b == NULL) return 0;
    while (*a != '\0' && *a == *b) { a++; b++; }
    return *a == *b;
}

BRTLClientStatus brtl_client_authorize(int has_communicates_with_drivers,
                                       const char *client_team,
                                       const char *expected_team)
{
    if (!has_communicates_with_drivers) return kBRTLClientNotAuthorized;
    if (expected_team == NULL || expected_team[0] == '\0') return kBRTLClientNotAuthorized;
    if (!str_equal(client_team, expected_team)) return kBRTLClientNotAuthorized;
    return kBRTLClientOK;
}

void brtl_own_init(brtl_ownership *o)
{
    if (o == NULL) return;
    o->owner = NULL;
    o->acquires = o->releases = o->busy_refusals = o->owner_lost = 0;
}

BRTLClientStatus brtl_own_acquire(brtl_ownership *o, const void *client)
{
    if (o == NULL || client == NULL) return kBRTLClientBadArgument;
    if (o->owner == client) return kBRTLClientOK;
    if (o->owner != NULL) {
        o->busy_refusals++;
        return kBRTLClientBusy;
    }
    o->owner = client;
    o->acquires++;
    return kBRTLClientOK;
}

BRTLClientStatus brtl_own_release(brtl_ownership *o, const void *client)
{
    if (o == NULL || client == NULL) return kBRTLClientBadArgument;
    if (o->owner != client) return kBRTLClientNotOwner;
    o->owner = NULL;
    o->releases++;
    return kBRTLClientOK;
}

int brtl_own_client_gone(brtl_ownership *o, const void *client)
{
    if (o == NULL || client == NULL || o->owner != client) return 0;
    o->owner = NULL;
    o->owner_lost++;
    return 1;
}

void brtl_own_reset(brtl_ownership *o)
{
    if (o != NULL) o->owner = NULL;
}

int brtl_own_is_owner(const brtl_ownership *o, const void *client)
{
    return o != NULL && client != NULL && o->owner == client;
}

BRTLSelectorAccess brtl_selector_access(uint64_t selector)
{
    switch (selector) {
    case kBHRFSelProtocolVersion:
    case kBHRFSelRingStats:
    case kBRTLSelDeviceInfo:
    case kBRTLSelAcquire:        /* l'arbitrage se fait dans brtl_own_acquire */
        return kBRTLAccessAnyClient;
    case kBHRFSelMapRing:
    case kBHRFSelStartRx:
    case kBHRFSelStopRx:
    case kBRTLSelRelease:
    case kBRTLSelCommand:
        return kBRTLAccessOwnerOnly;
    case kBHRFSelControlRequest:
        return kBRTLAccessNever;
    default:
        return kBRTLAccessUnknown;
    }
}

BRTLClientStatus brtl_selector_gate(const brtl_ownership *o, const void *client,
                                    uint64_t selector)
{
    switch (brtl_selector_access(selector)) {
    case kBRTLAccessAnyClient: return kBRTLClientOK;
    case kBRTLAccessOwnerOnly:
        return brtl_own_is_owner(o, client) ? kBRTLClientOK : kBRTLClientNotOwner;
    case kBRTLAccessNever:     return kBRTLClientRawControlDenied;
    case kBRTLAccessUnknown:
    default:                   return kBRTLClientUnknownOpcode;
    }
}

static int in_range(int64_t v, int64_t lo, int64_t hi) { return v >= lo && v <= hi; }

BRTLClientStatus brtl_command_validate(const BRTLCommand *cmd)
{
    if (cmd == NULL) return kBRTLClientBadArgument;
    const int64_t v = cmd->value;
    switch (cmd->opcode) {
    case kBRTLOpSetFrequency:
        return in_range(v, BRTL_FREQ_MIN_HZ, BRTL_FREQ_MAX_HZ) ? kBRTLClientOK : kBRTLClientBadArgument;
    case kBRTLOpSetSampleRate:
        return (in_range(v, BRTL_RATE_LOW_MIN, BRTL_RATE_LOW_MAX) ||
                in_range(v, BRTL_RATE_HIGH_MIN, BRTL_RATE_HIGH_MAX))
                   ? kBRTLClientOK : kBRTLClientBadArgument;
    case kBRTLOpSetGain:
        return in_range(v, 0, BRTL_GAIN_MAX_TENTH_DB) ? kBRTLClientOK : kBRTLClientBadArgument;
    case kBRTLOpSetFreqCorrection:
        return in_range(v, -BRTL_PPM_LIMIT, BRTL_PPM_LIMIT) ? kBRTLClientOK : kBRTLClientBadArgument;
    case kBRTLOpSetDirectSampling:
        return in_range(v, 0, 2) ? kBRTLClientOK : kBRTLClientBadArgument;
    case kBRTLOpSetGainMode:
    case kBRTLOpSetRtlAgc:
    case kBRTLOpSetOffsetTuning:
    case kBRTLOpSetBiasTee:
        return in_range(v, 0, 1) ? kBRTLClientOK : kBRTLClientBadArgument;
    default:
        return kBRTLClientUnknownOpcode;
    }
}

const char *brtl_client_status_name(BRTLClientStatus s)
{
    switch (s) {
    case kBRTLClientOK:               return "ok";
    case kBRTLClientBusy:             return "busy";
    case kBRTLClientNotOwner:         return "notOwner";
    case kBRTLClientTunerUnavailable: return "tunerUnavailable";
    case kBRTLClientBadArgument:      return "badArgument";
    case kBRTLClientCommandFailed:    return "commandFailed";
    case kBRTLClientTerminated:       return "terminated";
    case kBRTLClientUnknownOpcode:    return "unknownOpcode";
    case kBRTLClientNotAuthorized:    return "notAuthorized";
    case kBRTLClientRawControlDenied: return "rawControlDenied";
    }
    return "unknown";
}

const char *brtl_opcode_name(uint32_t opcode)
{
    switch (opcode) {
    case kBRTLOpSetFrequency:      return "setFrequency";
    case kBRTLOpSetSampleRate:     return "setSampleRate";
    case kBRTLOpSetGainMode:       return "setGainMode";
    case kBRTLOpSetGain:           return "setGain";
    case kBRTLOpSetFreqCorrection: return "setFreqCorrection";
    case kBRTLOpSetRtlAgc:         return "setRtlAgc";
    case kBRTLOpSetDirectSampling: return "setDirectSampling";
    case kBRTLOpSetOffsetTuning:   return "setOffsetTuning";
    case kBRTLOpSetBiasTee:        return "setBiasTee";
    }
    return "unknown";
}
