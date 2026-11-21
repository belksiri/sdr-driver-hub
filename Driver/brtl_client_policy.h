/*
 * brtl_client_policy.h — Règles du protocole client v2 du pilote RTL-SDR,
 * en C pur, testées sur hôte (tests/driver/test_brtl_client_policy.c).
 *
 * Copyright (C) 2026 Belksiri.
 *
 * Trois décisions qui ne doivent dépendre ni de DriverKit ni du matériel :
 *   1. le client a-t-il le droit d'ouvrir le pilote ? (entitlement + équipe) ;
 *   2. qui possède la radio, et ce sélecteur exige-t-il d'en être le
 *      propriétaire ? ;
 *   3. cette commande est-elle dans ses bornes ? — vérifié AVANT tout accès
 *      au bus : une valeur refusée n'émet rien.
 */
#ifndef BRTL_CLIENT_POLICY_H
#define BRTL_CLIENT_POLICY_H

#include <stdint.h>

#include "brtl_client_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── 1. Autorisation du client ─────────────────────────────────────────────
 * iPadOS n'ouvre déjà le pilote qu'aux apps de la même équipe dotées de
 * `com.apple.developer.driverkit.communicates-with-drivers`. Le pilote le
 * revérifie lui-même sur les entitlements du processus client : une erreur de
 * signature ou un changement de règle du système ne doit pas ouvrir la radio.
 * `client_team` peut être NULL (entitlement absent). */
BRTLClientStatus brtl_client_authorize(int has_communicates_with_drivers,
                                       const char *client_team,
                                       const char *expected_team);

/* ── 2. Propriété de la radio ──────────────────────────────────────────────
 * Le propriétaire est désigné par un jeton opaque (l'adresse du user client).
 * NULL : radio libre. */
typedef struct brtl_ownership {
    const void *owner;
    uint64_t acquires;
    uint64_t releases;
    uint64_t busy_refusals;
    uint64_t owner_lost;       /* propriétaires disparus sans Release */
} brtl_ownership;

void brtl_own_init(brtl_ownership *o);

/* Idempotent pour le propriétaire ; kBRTLClientBusy pour tout autre client
 * tant que la radio est prise. Aucune reprise de force. */
BRTLClientStatus brtl_own_acquire(brtl_ownership *o, const void *client);

/* kBRTLClientNotOwner si `client` n'est pas le propriétaire. */
BRTLClientStatus brtl_own_release(brtl_ownership *o, const void *client);

/* Le client a disparu (Stop du user client). Renvoie 1 s'il était
 * propriétaire — le pilote doit alors arrêter la réception lui-même. */
int brtl_own_client_gone(brtl_ownership *o, const void *client);

/* Le périphérique a disparu : plus de propriétaire. */
void brtl_own_reset(brtl_ownership *o);

int brtl_own_is_owner(const brtl_ownership *o, const void *client);

/* Accès par sélecteur. */
typedef enum {
    kBRTLAccessAnyClient = 0,  /* identité, état, compteurs                  */
    kBRTLAccessOwnerOnly = 1,  /* tout ce qui agit sur la radio ou le ring   */
    kBRTLAccessNever     = 2,  /* requête USB brute : refusée en v2          */
    kBRTLAccessUnknown   = 3
} BRTLSelectorAccess;

BRTLSelectorAccess brtl_selector_access(uint64_t selector);

/* Statut à rendre pour `selector` appelé par `client`, AVANT exécution :
 * kBRTLClientOK si l'appel peut se faire. */
BRTLClientStatus brtl_selector_gate(const brtl_ownership *o, const void *client,
                                    uint64_t selector);

/* ── 3. Bornes des commandes ───────────────────────────────────────────────*/
BRTLClientStatus brtl_command_validate(const BRTLCommand *cmd);

const char *brtl_client_status_name(BRTLClientStatus s);
const char *brtl_opcode_name(uint32_t opcode);

#ifdef __cplusplus
}
#endif

#endif /* BRTL_CLIENT_POLICY_H */
