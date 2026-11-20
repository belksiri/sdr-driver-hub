/*
 * BelksiriRtlSdrUserClient.cpp — Désérialisation, délégation, resérialisation.
 *
 * Chaque sélecteur suit le même schéma : vérifier la taille de la structure
 * reçue, appeler le pilote, produire une OSData de sortie. Une taille qui ne
 * correspond pas exactement est une erreur, jamais une tolérance : deux côtés
 * qui ne s'accordent pas sur la disposition d'une structure doivent s'arrêter,
 * pas interpréter des octets au hasard.
 */

#include <os/log.h>
#include <string.h>

#include <DriverKit/IOLib.h>
#include <DriverKit/IOUserClient.h>
#include <DriverKit/OSData.h>

#include "bhrf_shared.h"

#include "BelksiriRtlSdrDriver.h"
#include "BelksiriRtlSdrUserClient.h"

#define BRTL_UC_LOG(fmt, ...) os_log(OS_LOG_DEFAULT, "BRTL-UC: " fmt, ##__VA_ARGS__)

struct BelksiriRtlSdrUserClient_IVars {
    BelksiriRtlSdrDriver *driver;
};

bool BelksiriRtlSdrUserClient::init(void)
{
    if (!super::init()) {
        return false;
    }
    ivars = IONewZero(BelksiriRtlSdrUserClient_IVars, 1);
    return ivars != nullptr;
}

void BelksiriRtlSdrUserClient::free(void)
{
    IOSafeDeleteNULL(ivars, BelksiriRtlSdrUserClient_IVars, 1);
    super::free();
}

kern_return_t
IMPL(BelksiriRtlSdrUserClient, Start)
{
    kern_return_t ret = Start(provider, SUPERDISPATCH);
    if (ret != kIOReturnSuccess) {
        return ret;
    }

    ivars->driver = OSDynamicCast(BelksiriRtlSdrDriver, provider);
    if (ivars->driver == nullptr) {
        BRTL_UC_LOG("Start: le provider n'est pas le pilote RTL-SDR");
        Stop(provider, SUPERDISPATCH);
        return kIOReturnNoDevice;
    }
    ivars->driver->retain();

    BRTL_UC_LOG("Start: user client DÉMARRÉ et rattaché au pilote — "
                "l'application peut émettre des sélecteurs");
    return kIOReturnSuccess;
}

kern_return_t
IMPL(BelksiriRtlSdrUserClient, Stop)
{
    if (ivars != nullptr) {
        OSSafeReleaseNULL(ivars->driver);
    }
    BRTL_UC_LOG("Stop");
    return Stop(provider, SUPERDISPATCH);
}

/*
 * Petite aide : produire l'OSData de sortie. DriverKit attend que le pilote
 * alloue l'OSData ; l'appelant en consomme une référence.
 */
static kern_return_t bhrf_emit(IOUserClientMethodArguments *arguments,
                               const void *bytes, size_t length)
{
    OSData *out = OSData::withBytes(bytes, (uint32_t)length);
    if (out == nullptr) {
        return kIOReturnNoMemory;
    }
    arguments->structureOutput = out;
    return kIOReturnSuccess;
}

/* Vérifie que l'entrée est présente et de la taille EXACTE attendue. */
static const void *bhrf_input(IOUserClientMethodArguments *arguments,
                              size_t expected)
{
    if (arguments == nullptr || arguments->structureInput == nullptr) {
        return nullptr;
    }
    if (arguments->structureInput->getLength() != (uint32_t)expected) {
        return nullptr;
    }
    return arguments->structureInput->getBytesNoCopy();
}

// ExternalMethod est déclarée LOCALONLY dans IOUserClient.iig : elle ne
// traverse pas de frontière RPC, `iig` ne génère donc pas de structure
// d'arguments pour elle et la macro IMPL() ne s'applique pas. C'est une
// redéfinition C++ ordinaire.
kern_return_t BelksiriRtlSdrUserClient::ExternalMethod(
    uint64_t selector,
    IOUserClientMethodArguments *arguments,
    const IOUserClientMethodDispatch *dispatch,
    OSObject *target,
    void *reference)
{
    (void)dispatch;
    (void)target;
    (void)reference;

    if (ivars == nullptr || ivars->driver == nullptr) {
        return kIOReturnNotAttached;
    }
    if (arguments == nullptr) {
        return kIOReturnBadArgument;
    }

    switch (selector) {

    case kBHRFSelProtocolVersion: {
        BHRFProtocolInfo info = {};
        kern_return_t ret = ivars->driver->RtlGetProtocolInfo(&info);
        if (ret != kIOReturnSuccess) {
            return ret;
        }
        return bhrf_emit(arguments, &info, sizeof(info));
    }

    case kBHRFSelControlRequest: {
        const void *in = bhrf_input(arguments, sizeof(BHRFControlRequest));
        if (in == nullptr) {
            BRTL_UC_LOG("ControlRequest: entrée absente ou de taille inattendue");
            return kIOReturnBadArgument;
        }
        BHRFControlRequest req;
        memcpy(&req, in, sizeof(req));

        BHRFControlResponse resp = {};
        kern_return_t ret = ivars->driver->RtlControlRequest(&req, &resp);
        if (ret != kIOReturnSuccess) {
            return ret;
        }
        return bhrf_emit(arguments, &resp, sizeof(resp));
    }

    case kBHRFSelRingStats: {
        BHRFRingStats stats = {};
        kern_return_t ret = ivars->driver->RtlGetRingStats(&stats);
        if (ret != kIOReturnSuccess) {
            return ret;
        }
        return bhrf_emit(arguments, &stats, sizeof(stats));
    }

    /* kBHRFSelMapRing ne transporte aucune mémoire : il ne fait qu'ANNONCER
     * que le ring existe et sous quelle géométrie. Le mappage proprement dit
     * se fait par IOConnectMapMemory64, qui aboutit dans
     * CopyClientMemoryForType ci-dessous. Séparer les deux permet à
     * l'application de connaître blockCount et blockBytes avant de mapper. */
    case kBHRFSelMapRing: {
        BHRFRingStats stats = {};
        kern_return_t ret = ivars->driver->RtlGetRingStats(&stats);
        if (ret != kIOReturnSuccess) {
            return ret;
        }
        BHRFSimpleStatus st = {};
        st.blockCount = stats.blockCount;
        st.blockBytes = stats.blockBytes;
        st.status = (stats.blockCount > 0) ? kBHRFStatusOK : kBHRFStatusNotStreaming;
        return bhrf_emit(arguments, &st, sizeof(st));
    }

    case kBHRFSelStartRx: {
        const void *in = bhrf_input(arguments, sizeof(BHRFStartRxRequest));
        if (in == nullptr) {
            BRTL_UC_LOG("StartRx: entrée absente ou de taille inattendue");
            return kIOReturnBadArgument;
        }
        BHRFStartRxRequest req;
        memcpy(&req, in, sizeof(req));

        BHRFSimpleStatus st = {};
        kern_return_t ret = ivars->driver->RtlStartRx(&req, &st);
        if (ret != kIOReturnSuccess) {
            return ret;
        }
        return bhrf_emit(arguments, &st, sizeof(st));
    }

    case kBHRFSelStopRx: {
        BHRFSimpleStatus st = {};
        kern_return_t ret = ivars->driver->RtlStopRx(&st);
        if (ret != kIOReturnSuccess) {
            return ret;
        }
        return bhrf_emit(arguments, &st, sizeof(st));
    }

    default:
        BRTL_UC_LOG("sélecteur inconnu %llu", selector);
        return kIOReturnUnsupported;
    }
}


/*
 * Point d'arrivée d'IOConnectMapMemory64.
 *
 * Le ring est mappé en LECTURE SEULE côté application. C'est la contrepartie
 * directe du choix de conception annoncé dans bhrf_shared.h : l'application ne
 * détient aucun curseur dans la structure partagée, et le pilote n'a donc
 * jamais à faire confiance à une valeur venue de l'espace utilisateur. Une
 * application qui pourrait écrire dans l'en-tête pourrait mentir sur
 * writeIndex, sur la géométrie, ou corrompre les métadonnées de slot.
 */
kern_return_t
IMPL(BelksiriRtlSdrUserClient, CopyClientMemoryForType)
{
    if (ivars == nullptr || ivars->driver == nullptr) {
        return kIOReturnNotAttached;
    }
    if (type != kBHRFMemoryTypeRing) {
        BRTL_UC_LOG("CopyClientMemoryForType: type %llu inconnu", type);
        return kIOReturnBadArgument;
    }

    IOMemoryDescriptor *ring = nullptr;
    const kern_return_t ret = ivars->driver->RtlCopyRingMemory(&ring);
    if (ret != kIOReturnSuccess || ring == nullptr) {
        BRTL_UC_LOG("CopyClientMemoryForType: ring indisponible (0x%x) — "
                    "démarrez la réception d'abord", ret);
        return (ret == kIOReturnSuccess) ? kIOReturnNotReady : ret;
    }

    if (options != nullptr) {
        *options = kIOUserClientMemoryReadOnly;
    }
    *memory = ring;   /* la référence prise par le pilote passe à l'appelant */
    return kIOReturnSuccess;
}
