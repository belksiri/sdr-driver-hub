#include "bhrf_session.h"
#include "bhrf_shared.h"
#include "bhrf_requests.h"

void bhrf_session_init(BHRFSession *s)
{
    if (s == 0) { return; }
    s->state = kBHRFSessionIdle;
    s->inFlight = 0u;
    s->stopDrained = 0u;
    s->usbErrors = 0u;
}

BHRFSessionState bhrf_session_state(const BHRFSession *s)
{
    return (s == 0) ? kBHRFSessionTerminated : (BHRFSessionState)s->state;
}

int bhrf_session_can_start(const BHRFSession *s)
{
    return (s != 0 && s->state == kBHRFSessionIdle) ? 1 : 0;
}

int bhrf_session_can_control(const BHRFSession *s)
{
    return (s != 0 && s->state != kBHRFSessionTerminated) ? 1 : 0;
}

void bhrf_session_started(BHRFSession *s, uint32_t in_flight)
{
    if (s == 0) { return; }
    s->state = kBHRFSessionReceiving;
    s->inFlight = in_flight;
    /* Un démarrage réussi prouve que le périphérique répond : les compteurs
     * d'une session précédente ne doivent pas s'additionner à ceux-ci. */
    s->usbErrors = 0u;
    s->stopDrained = 0u;
}

int bhrf_session_begin_stop(BHRFSession *s)
{
    if (s == 0 || s->state != kBHRFSessionReceiving) { return 0; }
    s->state = kBHRFSessionStopping;
    return 1;
}

BHRFCompletionAction bhrf_session_completion(BHRFSession *s, int32_t io_return)
{
    BHRFUsbFailureKind kind;

    if (s == 0) { return kBHRFCompletionDrain; }

    if (s->inFlight > 0u) { s->inFlight--; }

    /* ── Arrêt volontaire ──────────────────────────────────────────────────
     * Toute complétion, en erreur ou non, est ATTENDUE ici : abandonner des
     * transferts, c'est demander au contrôleur de les faire échouer. Les
     * compter comme des erreurs USB fausserait la mesure ; les lire comme une
     * perte de périphérique bloquait la session jusqu'au débranchement. */
    if (s->state == kBHRFSessionStopping) {
        s->stopDrained++;
        return kBHRFCompletionDrain;
    }

    /* Hors réception, plus rien à publier. */
    if (s->state != kBHRFSessionReceiving) {
        return kBHRFCompletionDrain;
    }

    kind = bhrf_classify_usb_failure(io_return);
    if (kind == kBHRFUsbOk) {
        return kBHRFCompletionPublishAndResubmit;
    }

    s->usbErrors++;

    /* Le périphérique a disparu pendant une réception active : c'est le seul
     * code qui vaille constat de départ. Et même lui n'est pas définitif —
     * seul le fournisseur l'est. */
    if (kind == kBHRFUsbNoDevice) {
        s->state = kBHRFSessionTerminated;
        return kBHRFCompletionPublishOnly;
    }

    /* Lien momentanément muet : arrêter la réception, MAIS revenir à un état
     * d'où un nouveau démarrage suffit. C'est le cas typique après un
     * changement de mode transceiver, et il ne justifie pas de débrancher. */
    if (kind == kBHRFUsbNotResponding) {
        s->state = kBHRFSessionIdle;
        return kBHRFCompletionPublishOnly;
    }

    /* Erreur isolée : la publier et continuer. Un incident sur un bus USB ne
     * justifie pas d'interrompre une campagne de trente minutes. */
    return kBHRFCompletionPublishAndResubmit;
}

void bhrf_session_finish_stop(BHRFSession *s)
{
    if (s == 0) { return; }
    if (s->state == kBHRFSessionTerminated) { return; }
    s->state = kBHRFSessionIdle;
    s->inFlight = 0u;
}

void bhrf_session_provider_terminated(BHRFSession *s)
{
    if (s == 0) { return; }
    s->state = kBHRFSessionTerminated;
    s->inFlight = 0u;
}

uint32_t bhrf_session_device_state(const BHRFSession *s)
{
    switch (bhrf_session_state(s)) {
    case kBHRFSessionReceiving:  return (uint32_t)kBHRFDeviceStateReceiving;
    case kBHRFSessionTerminated: return (uint32_t)kBHRFDeviceStateTerminated;
    /* Un arrêt en cours se présente comme « au repos » à l'application : de son
     * point de vue la réception est finie, et elle peut déjà reconfigurer. */
    case kBHRFSessionStopping:
    case kBHRFSessionIdle:
    default:                     return (uint32_t)kBHRFDeviceStateIdle;
    }
}

const char *bhrf_session_state_name(BHRFSessionState state)
{
    switch (state) {
    case kBHRFSessionIdle:       return "repos";
    case kBHRFSessionReceiving:  return "reception";
    case kBHRFSessionStopping:   return "arret_en_cours";
    case kBHRFSessionTerminated: return "termine";
    default:                     return "inconnu";
    }
}
