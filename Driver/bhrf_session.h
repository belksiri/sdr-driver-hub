/*
 * bhrf_session.h — Le cycle de vie d'une session de réception.
 *
 * Extrait du pilote parce que c'est là qu'était le défaut le plus coûteux du
 * prototype : après un premier Stop RX réussi, toute requête de configuration
 * répondait `terminated`, et seul un débranchement physique rendait l'appareil
 * utilisable. La règle fautive tenait en une ligne, noyée dans une complétion
 * asynchrone impossible à exercer sans matériel. Elle est ici, en C pur, avec
 * ses tests.
 *
 * ── Les quatre états, et pourquoi ils sont quatre ────────────────────────────
 *
 *   Idle        aucun transfert en vol ; configuration et démarrage permis
 *   Receiving   transferts en vol ; les complétions publient et se resoumettent
 *   Stopping    arrêt VOLONTAIRE demandé ; on attend le retour des transferts
 *   Terminated  le périphérique n'est plus là — état définitif
 *
 * `Stopping` n'existe que pour une raison, et c'est toute l'affaire : un arrêt
 * volontaire produit NÉCESSAIREMENT des complétions en erreur. Abandonner des
 * transferts en vol, c'est demander au contrôleur de les faire échouer. Sans
 * état distinct, ces erreurs attendues sont indiscernables d'une perte réelle
 * du périphérique — et c'est exactement la confusion qui bloquait la session.
 *
 * ── Qui a autorité pour déclarer la terminaison ──────────────────────────────
 *
 * Le fournisseur, et lui seul : IOKit appelle `Stop()` sur le pilote quand le
 * périphérique USB disparaît réellement. C'est une preuve.
 *
 * Le code de retour d'un transfert n'est qu'un INDICE. `kIOReturnNotResponding`
 * peut signaler un lien momentanément muet — après un changement de mode
 * transceiver, par exemple — dont on se remet sans rien débrancher. Seul
 * `kIOReturnNoDevice`, reçu pendant une réception active, vaut constat de
 * départ ; et même lui reste révocable par un nouveau démarrage réussi.
 */

#ifndef BHRF_SESSION_H
#define BHRF_SESSION_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    kBHRFSessionIdle       = 0,
    kBHRFSessionReceiving  = 1,
    kBHRFSessionStopping   = 2,
    kBHRFSessionTerminated = 3
} BHRFSessionState;

/* Ce que la complétion d'un transfert doit entraîner. */
typedef enum {
    /* Marche normale : publier le bloc, puis resoumettre le transfert. */
    kBHRFCompletionPublishAndResubmit = 0,
    /* Publier le bloc mais NE PAS resoumettre : la réception s'achève. */
    kBHRFCompletionPublishOnly = 1,
    /* Ne rien publier : complétion attendue d'un arrêt volontaire. */
    kBHRFCompletionDrain = 2
} BHRFCompletionAction;

typedef struct {
    uint8_t  state;
    uint32_t inFlight;
    /* Complétions arrivées pendant un arrêt volontaire. Comptées séparément :
     * ce ne sont PAS des erreurs USB, et les additionner aux vraies fausserait
     * le critère go/no-go. */
    uint64_t stopDrained;
    uint64_t usbErrors;
} BHRFSession;

void bhrf_session_init(BHRFSession *s);

BHRFSessionState bhrf_session_state(const BHRFSession *s);

/* Un démarrage n'est possible qu'à l'arrêt complet. */
int bhrf_session_can_start(const BHRFSession *s);

/* Les requêtes de contrôle restent possibles dans TOUS les états sauf la
 * terminaison — y compris pendant un arrêt : c'est ce qui permet de remettre
 * le HackRF en mode OFF juste après avoir abandonné les transferts. */
int bhrf_session_can_control(const BHRFSession *s);

void bhrf_session_started(BHRFSession *s, uint32_t in_flight);

/* Entre en arrêt volontaire. Retourne 0 si l'état ne s'y prête pas. */
int bhrf_session_begin_stop(BHRFSession *s);

/* LA règle. `io_return` est le code de la complétion. */
BHRFCompletionAction bhrf_session_completion(BHRFSession *s, int32_t io_return);

/* Achève l'arrêt : retour à Idle, sauf si le périphérique est réellement parti. */
void bhrf_session_finish_stop(BHRFSession *s);

/* La SEULE autorité pour déclarer la terminaison : IOKit a appelé Stop(). */
void bhrf_session_provider_terminated(BHRFSession *s);

/* L'état publié dans l'en-tête du ring (BHRFDeviceState). */
uint32_t bhrf_session_device_state(const BHRFSession *s);

/* Libellé court, pour les journaux. Jamais NULL. */
const char *bhrf_session_state_name(BHRFSessionState state);

#ifdef __cplusplus
}
#endif

#endif /* BHRF_SESSION_H */
