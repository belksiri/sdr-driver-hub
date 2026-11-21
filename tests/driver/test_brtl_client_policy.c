/*
 * test_brtl_client_policy.c — Tests hôtes des règles du protocole client v2
 * (Driver/brtl_client_policy.c). Aucun matériel, aucun DriverKit.
 *
 * Copyright (C) 2026 Belksiri.
 */
#include <stdio.h>
#include <string.h>

#include "bhrf_shared.h"
#include "brtl_client_policy.h"

static int checks = 0, failures = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
    fprintf(stderr, "ÉCHEC %s:%d : %s\n", __FILE__, __LINE__, #cond); } } while (0)

static BRTLCommand cmd(uint32_t op, int64_t v) { BRTLCommand c; memset(&c, 0, sizeof c); c.opcode = op; c.value = v; return c; }

static void test_authorize(void)
{
    const char *team = "WVQH467NC8";
    CHECK(brtl_client_authorize(1, "WVQH467NC8", team) == kBRTLClientOK);
    /* Client non autorisé : sans entitlement, autre équipe, équipe absente. */
    CHECK(brtl_client_authorize(0, "WVQH467NC8", team) == kBRTLClientNotAuthorized);
    CHECK(brtl_client_authorize(1, "AAAAAAAAAA", team) == kBRTLClientNotAuthorized);
    CHECK(brtl_client_authorize(1, NULL, team) == kBRTLClientNotAuthorized);
    CHECK(brtl_client_authorize(1, "", team) == kBRTLClientNotAuthorized);
    CHECK(brtl_client_authorize(1, "WVQH467NC", team) == kBRTLClientNotAuthorized);   /* préfixe */
    CHECK(brtl_client_authorize(1, "WVQH467NC8X", team) == kBRTLClientNotAuthorized); /* suffixe */
    /* Pilote construit sans équipe attendue : tout est refusé. */
    CHECK(brtl_client_authorize(1, "WVQH467NC8", "") == kBRTLClientNotAuthorized);
    CHECK(brtl_client_authorize(1, "WVQH467NC8", NULL) == kBRTLClientNotAuthorized);
}

static void test_ownership(void)
{
    brtl_ownership o;
    brtl_own_init(&o);
    int spectrum = 1, hub = 2;
    CHECK(brtl_own_acquire(&o, &spectrum) == kBRTLClientOK);
    CHECK(brtl_own_acquire(&o, &spectrum) == kBRTLClientOK);          /* idempotent */
    CHECK(o.acquires == 1);
    CHECK(brtl_own_acquire(&o, &hub) == kBRTLClientBusy);             /* pas de vol */
    CHECK(o.busy_refusals == 1);
    CHECK(brtl_own_release(&o, &hub) == kBRTLClientNotOwner);
    CHECK(brtl_own_is_owner(&o, &spectrum));
    CHECK(!brtl_own_is_owner(&o, &hub));
    CHECK(brtl_own_release(&o, &spectrum) == kBRTLClientOK);
    CHECK(brtl_own_acquire(&o, &hub) == kBRTLClientOK);
    /* Disparition d'un client qui n'est pas propriétaire : sans effet. */
    CHECK(brtl_own_client_gone(&o, &spectrum) == 0);
    CHECK(brtl_own_is_owner(&o, &hub));
    /* Disparition du propriétaire : radio libérée, compteur dédié. */
    CHECK(brtl_own_client_gone(&o, &hub) == 1);
    CHECK(o.owner == NULL && o.owner_lost == 1);
    CHECK(brtl_own_acquire(&o, &spectrum) == kBRTLClientOK);
    brtl_own_reset(&o);                                               /* retrait */
    CHECK(o.owner == NULL);
    CHECK(brtl_own_acquire(&o, NULL) == kBRTLClientBadArgument);
}

static void test_selector_gate(void)
{
    brtl_ownership o;
    brtl_own_init(&o);
    int owner = 1, other = 2;
    brtl_own_acquire(&o, &owner);
    const uint64_t free_sel[] = { kBHRFSelProtocolVersion, kBHRFSelRingStats, kBRTLSelDeviceInfo, kBRTLSelAcquire };
    for (size_t i = 0; i < sizeof free_sel / sizeof free_sel[0]; i++) {
        CHECK(brtl_selector_gate(&o, &other, free_sel[i]) == kBRTLClientOK);
    }
    const uint64_t owned[] = { kBHRFSelMapRing, kBHRFSelStartRx, kBHRFSelStopRx, kBRTLSelRelease, kBRTLSelCommand };
    for (size_t i = 0; i < sizeof owned / sizeof owned[0]; i++) {
        CHECK(brtl_selector_gate(&o, &owner, owned[i]) == kBRTLClientOK);
        CHECK(brtl_selector_gate(&o, &other, owned[i]) == kBRTLClientNotOwner);
    }
    /* Requête USB brute : refusée à TOUT client, propriétaire compris. */
    CHECK(brtl_selector_gate(&o, &owner, kBHRFSelControlRequest) == kBRTLClientRawControlDenied);
    CHECK(brtl_selector_gate(&o, &other, kBHRFSelControlRequest) == kBRTLClientRawControlDenied);
    CHECK(brtl_selector_gate(&o, &owner, 42) == kBRTLClientUnknownOpcode);
    /* Le ring mappé reste réservé au propriétaire. */
    CHECK(brtl_selector_access(kBHRFSelMapRing) == kBRTLAccessOwnerOnly);
}

static void test_command_bounds(void)
{
    BRTLCommand c;
    c = cmd(kBRTLOpSetFrequency, 88800000);   CHECK(brtl_command_validate(&c) == kBRTLClientOK);
    c = cmd(kBRTLOpSetFrequency, 24000000);   CHECK(brtl_command_validate(&c) == kBRTLClientOK);
    c = cmd(kBRTLOpSetFrequency, 0);          CHECK(brtl_command_validate(&c) == kBRTLClientBadArgument);
    c = cmd(kBRTLOpSetFrequency, -1);         CHECK(brtl_command_validate(&c) == kBRTLClientBadArgument);
    c = cmd(kBRTLOpSetFrequency, 5000000000LL); CHECK(brtl_command_validate(&c) == kBRTLClientBadArgument);
    c = cmd(kBRTLOpSetSampleRate, 2560000);   CHECK(brtl_command_validate(&c) == kBRTLClientOK);
    c = cmd(kBRTLOpSetSampleRate, 2400000);   CHECK(brtl_command_validate(&c) == kBRTLClientOK);
    c = cmd(kBRTLOpSetSampleRate, 250000);    CHECK(brtl_command_validate(&c) == kBRTLClientOK);
    c = cmd(kBRTLOpSetSampleRate, 225000);    CHECK(brtl_command_validate(&c) == kBRTLClientBadArgument);
    c = cmd(kBRTLOpSetSampleRate, 600000);    CHECK(brtl_command_validate(&c) == kBRTLClientBadArgument); /* trou */
    c = cmd(kBRTLOpSetSampleRate, 900000);    CHECK(brtl_command_validate(&c) == kBRTLClientBadArgument);
    c = cmd(kBRTLOpSetSampleRate, 3200001);   CHECK(brtl_command_validate(&c) == kBRTLClientBadArgument);
    c = cmd(kBRTLOpSetGain, 297);             CHECK(brtl_command_validate(&c) == kBRTLClientOK);
    c = cmd(kBRTLOpSetGain, -1);              CHECK(brtl_command_validate(&c) == kBRTLClientBadArgument);
    c = cmd(kBRTLOpSetGain, 1001);            CHECK(brtl_command_validate(&c) == kBRTLClientBadArgument);
    c = cmd(kBRTLOpSetGainMode, 0);           CHECK(brtl_command_validate(&c) == kBRTLClientOK);
    c = cmd(kBRTLOpSetGainMode, 2);           CHECK(brtl_command_validate(&c) == kBRTLClientBadArgument);
    c = cmd(kBRTLOpSetRtlAgc, 1);             CHECK(brtl_command_validate(&c) == kBRTLClientOK);
    c = cmd(kBRTLOpSetBiasTee, -1);           CHECK(brtl_command_validate(&c) == kBRTLClientBadArgument);
    c = cmd(kBRTLOpSetFreqCorrection, -1000); CHECK(brtl_command_validate(&c) == kBRTLClientOK);
    c = cmd(kBRTLOpSetFreqCorrection, 1001);  CHECK(brtl_command_validate(&c) == kBRTLClientBadArgument);
    c = cmd(kBRTLOpSetDirectSampling, 2);     CHECK(brtl_command_validate(&c) == kBRTLClientOK);
    c = cmd(kBRTLOpSetDirectSampling, 3);     CHECK(brtl_command_validate(&c) == kBRTLClientBadArgument);
    c = cmd(0, 0);                            CHECK(brtl_command_validate(&c) == kBRTLClientUnknownOpcode);
    c = cmd(99, 0);                           CHECK(brtl_command_validate(&c) == kBRTLClientUnknownOpcode);
    CHECK(brtl_command_validate(NULL) == kBRTLClientBadArgument);
}

static void test_layout_and_names(void)
{
    CHECK(sizeof(BRTLCommand) == 16);
    CHECK(sizeof(BRTLClientResult) == 16);
    CHECK(sizeof(BRTLDeviceInfo) == 328);
    CHECK(BRTL_CLIENT_PROTOCOL_VERSION == 2u);
    CHECK(BRTL_RING_LAYOUT_VERSION == 1u);
    /* Les sélecteurs v2 prolongent ceux de bhrf_shared.h sans les recouvrir. */
    CHECK(kBRTLSelAcquire == kBHRFSelCount);
    CHECK(strcmp(brtl_client_status_name(kBRTLClientBusy), "busy") == 0);
    CHECK(strcmp(brtl_opcode_name(kBRTLOpSetGain), "setGain") == 0);
    CHECK(strcmp(brtl_opcode_name(77), "unknown") == 0);
}

int main(void)
{
    test_authorize();
    test_ownership();
    test_selector_gate();
    test_command_bounds();
    test_layout_and_names();
    printf("brtl_client_policy : %d vérifications, %d échec(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
