/*
 * BelksiriRtlSdrDriver.cpp — Pilote DriverKit générique pour RTL-SDR (RTL2832U).
 *
 * Pilote de SDR Driver Hub. Il ouvre l'appareil, applique la liste blanche
 * (brtl_policy) à CHAQUE requête de contrôle, et dépose le flux bulk IN dans
 * le ring partagé (bhrf_shared.h).
 *
 * Protocole client v2 (brtl_client_protocol.h) : la programmation du
 * démodulateur et du tuner se fait ICI, par librtlsdr (GPL-2.0-or-later,
 * Controller/) compilée dans le dext. Ses transferts de contrôle passent par
 * brtl_bus puis par la même porte RtlControlRequest que tout le reste : la
 * liste blanche s'applique aussi à librtlsdr. Les applications clientes
 * (Spectrum Analyzer One, SDR Driver Hub) n'envoient que des commandes de
 * haut niveau ; aucune requête USB brute n'est acceptée d'elles.
 *
 * Séquence de démarrage, ring zéro-copie et gestion Stop / Abort /
 * ClearStall reprises de notre pilote DriverKit précédent (même auteur). Aucune
 * valeur ni requête propre à un autre équipement n'y subsiste.
 */

#include <os/log.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <DriverKit/IOLib.h>
#include <DriverKit/IOUserClient.h>
#include <DriverKit/IOBufferMemoryDescriptor.h>
#include <DriverKit/IOMemoryDescriptor.h>
#include <DriverKit/OSAction.h>

// En-tête parapluie : apporte aussi USBDriverKitDefs.h, d'où vient
// IOUSBHostFreeDescriptor (surchargé par type de descripteur).
#include <USBDriverKit/USBDriverKit.h>

#include "bhrf_shared.h"
#include "bhrf_ring_geometry.h"
#include "bhrf_diagnostics.h"
#include "bhrf_session.h"
#include "brtl_identity.h"
#include "brtl_policy.h"
#include "brtl_bus.h"
#include "brtl_client_policy.h"
#include "brtl_client_protocol.h"
#include "brtl_rtl_controller.h"

#include "BelksiriRtlSdrDriver.h"
#include "BelksiriRtlSdrUserClient.h"

#define BRTL_LOG(fmt, ...) os_log(OS_LOG_DEFAULT, "BRTL: " fmt, ##__VA_ARGS__)

/* Journal dédié aux refus. Préfixe distinct et stable : c'est lui qu'on filtre
 * dans `log stream` pour produire la preuve de G9, et qu'on montre dans la
 * vidéo destinée à Apple. Ne pas le renommer sans mettre à jour les scripts. */
#define BRTL_DENY_LOG(fmt, ...) \
    os_log(OS_LOG_DEFAULT, "BRTL-DENY: " fmt, ##__VA_ARGS__)

/* Horodatage de build, pour distinguer deux dext lors d'une mise au point : sur
 * iPadOS le pilote ne se recharge qu'au débranchement, et savoir lequel des deux
 * est en mémoire évite de longues confusions. */
#ifndef BRTL_BUILD_ID
#define BRTL_BUILD_ID 0u
#endif

/* Équipe dont les clients sont acceptés (DEVELOPMENT_TEAM du projet). Un
 * pilote construit sans elle refuse tout client (brtl_client_authorize). */
#define BRTL_STR2(x) #x
#define BRTL_STR(x) BRTL_STR2(x)
#ifdef BRTL_TEAM_ID
static const char *const kBrtlExpectedTeam = BRTL_STR(BRTL_TEAM_ID);
#else
static const char *const kBrtlExpectedTeam = "";
#endif

/* Réglages appliqués à l'ouverture du contrôleur, ceux de rtl_tcp. */
#define BRTL_DEFAULT_RATE_HZ 2048000u
#define BRTL_DEFAULT_FREQ_HZ 100000000u

/* Messages de librtlsdr (fprintf redirigé par dkcompat/brtl_dk_prefix.h). */
extern "C" int brtl_dk_log(const char *format, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, format);
    const int n = vsnprintf(buf, sizeof buf, format, ap);
    va_end(ap);
    size_t len = strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) buf[--len] = 0;
    if (len > 0) {
        os_log(OS_LOG_DEFAULT, "BRTL-LIB: %{public}s", buf);
    }
    return n;
}

/* Allocateur de librtlsdr (dkcompat/brtl_dk_prefix.h). IOFree exige la taille
 * allouée : elle est rangée dans un en-tête de 16 octets (alignement conservé). */
namespace {
constexpr size_t kBrtlAllocHeader = 16;
}

extern "C" void *brtl_dk_calloc(size_t count, size_t size)
{
    if (count != 0 && size > (SIZE_MAX - kBrtlAllocHeader) / count) return nullptr;
    const size_t n = count * size;
    auto *base = static_cast<uint8_t *>(IOMallocZero(n + kBrtlAllocHeader));
    if (base == nullptr) return nullptr;
    *reinterpret_cast<size_t *>(base) = n;
    return base + kBrtlAllocHeader;
}

extern "C" void *brtl_dk_malloc(size_t size) { return brtl_dk_calloc(1, size); }

extern "C" void brtl_dk_free(void *ptr)
{
    if (ptr == nullptr) return;
    uint8_t *base = static_cast<uint8_t *>(ptr) - kBrtlAllocHeader;
    IOFree(base, *reinterpret_cast<size_t *>(base) + kBrtlAllocHeader);
}

/* Équipe et entitlement du client : voir BelksiriRtlSdrUserClient. */
extern "C" const char *brtl_expected_team(void) { return kBrtlExpectedTeam; }

/* Endpoint bulk IN des échantillons du RTL2832U. */
#define BRTL_BULK_IN_ENDPOINT 0x81u

/* Délai maximal d'un transfert bulk IN.
 *
 * Zéro — « sans délai » — a été essayé et retiré : quand le périphérique repasse en
 * mode OFF, l'endpoint cesse d'émettre et les transferts en vol ne reviennent
 * JAMAIS. Le pipe restait alors bloqué avec des transferts fantômes, et il
 * fallait débrancher.
 *
 * 5 secondes laissent une marge considérable : au débit le plus bas de la
 * campagne, 2 MS/s, un bloc de 256 KiB se remplit en 66 ms, et le plus profond
 * des 16 transferts en vol revient en une seconde environ. Le délai ne peut
 * donc pas se déclencher en marche normale — seulement quand l'endpoint s'est
 * réellement tu. */
#define BHRF_TRANSFER_TIMEOUT_MS 5000u

/* Les bornes et la règle de rotation vivent dans bhrf_ring_geometry.c, en C pur,
 * avec leur démonstration et leurs 1239 vérifications hôtes. C'est le calcul
 * dont une erreur ne produit aucun symptôme visible — seulement des blocs lus
 * les uns pour les autres. */
/* Adresse de writeIndex vue comme un uint64_t* aligné.
 *
 * La structure est `pack(1)`, donc le compilateur croit l'alignement de 1 et
 * avertit qu'un atomique 64 bits y serait coûteux — ou, pire, découpé. Le
 * static_assert de bhrf_shared.h garantit que le DÉCALAGE du champ est multiple
 * de 8 ; l'en-tête étant alloué aligné sur 4096, l'adresse l'est réellement.
 * Ce passage par un type aligné dit cela au compilateur au lieu de le laisser
 * produire un appel de repli. */
static inline uint64_t *bhrf_write_index_ptr(BHRFRingHeader *h)
{
    return reinterpret_cast<uint64_t *>(
        reinterpret_cast<uint8_t *>(h) + offsetof(BHRFRingHeader, writeIndex));
}

struct BelksiriRtlSdrDriver_IVars {
    IOUSBHostDevice    *device;
    IOUSBHostInterface *interface;
    IOUSBHostPipe      *inPipe;

    /* Tampon unique de la phase données des requêtes de contrôle. Alloué une
     * fois au démarrage : aucune allocation ne doit avoir lieu par requête. */
    IOBufferMemoryDescriptor *ctrlBuffer;
    uint8_t                  *ctrlBytes;

    uint16_t bcdDevice;
    uint16_t idVendor;
    uint16_t idProduct;

    /* Le cycle de vie de la réception vit dans bhrf_session.c, en C pur, avec
     * ses 2089 vérifications hôtes. Ce n'était pas un raffinement : la règle
     * qui distingue un arrêt VOLONTAIRE d'une perte de périphérique tenait en
     * une ligne enfouie dans une complétion asynchrone, impossible à exercer
     * sans matériel — et elle était fausse. */
    BHRFSession session;

    /* ── Étape 5 : le ring et les transferts en vol ───────────────────────
     * Le ring est alloué UNE seule fois, à la première réception, et n'est
     * libéré qu'à l'arrêt du pilote. Le libérer à chaque stop invaliderait le
     * mappage détenu par l'application, qui devrait le refaire — et surtout,
     * un mappage libéré sous les pieds d'un consommateur est un plantage. */
    IOBufferMemoryDescriptor *ringBuffer;
    BHRFRingHeader           *ringHeader;   /* vue CPU de l'en-tête           */
    IOMemoryDescriptor       *slots[BHRF_RING_BLOCK_COUNT];
    OSAction                 *readActions[BHRF_MAX_INFLIGHT];

    uint32_t blockCount;
    uint32_t blockBytes;
    uint32_t buffersInFlight;

    /* Compteur monotone de blocs publiés. C'est LUI qui devient writeIndex :
     * l'application en déduit le slot par un modulo, et ne reçoit donc jamais
     * d'index qui recule. */
    uint64_t produced;
    uint64_t bytesProduced;
    uint64_t ringUsbErrors;
    uint64_t lastCompletionNs;
    uint32_t lastUsbStatus;

    /* Compteurs séparés : « autorisées » et « refusées » ne se mélangent pas. */
    uint64_t controlAllowed;
    uint64_t controlDenied;
    uint64_t controlUsbErrors;

    /* ── Protocole client v2 ──────────────────────────────────────────────
     * Tout ce qui suit est protégé par `lock` : plusieurs user clients (donc
     * plusieurs files) peuvent appeler le pilote en même temps, et librtlsdr
     * n'est pas réentrante. La complétion bulk (ReadComplete) ne le prend
     * jamais : elle ne touche ni au contrôleur ni à la propriété. */
    IOLock         *lock;
    brtl_bus        bus;
    brtl_ctrl      *ctrl;
    brtl_device_info devInfo;
    int32_t         tunerStatus;      /* brtl_status de la dernière ouverture */
    brtl_ownership  own;

    uint32_t frequencyHz;
    uint32_t sampleRateHz;
    int32_t  gainTenthDb;             /* dernier gain manuel demandé          */
    int32_t  appliedGainTenthDb;      /* −1 : automatique                     */
    bool     manualGain;
    bool     rtlAgc;
    bool     biasTee;
    bool     offsetTuning;
    uint32_t directSampling;
    int32_t  ppm;

    uint64_t commandsReceived;
    uint64_t commandsApplied;
    uint64_t commandsRejected;
    uint64_t commandsFailed;
    uint64_t rawControlDenied;
    int32_t  lastCommandStatus;
};

/* La porte unique, vue de librtlsdr (brtl_bus) : liste blanche PUIS bus. */
static int BelksiriRtlSdrDriverBusGate(void *ctx, const BHRFControlRequest *req,
                                       BHRFControlResponse *resp)
{
    auto *self = reinterpret_cast<BelksiriRtlSdrDriver *>(ctx);
    if (self == nullptr) {
        return 1;
    }
    return self->RtlControlRequest(req, resp) == kIOReturnSuccess ? 0 : 1;
}

bool BelksiriRtlSdrDriver::init(void)
{
    if (!super::init()) {
        return false;
    }
    ivars = IONewZero(BelksiriRtlSdrDriver_IVars, 1);
    if (ivars == nullptr) {
        return false;
    }
    ivars->lock = IOLockAlloc();
    if (ivars->lock == nullptr) {
        IOSafeDeleteNULL(ivars, BelksiriRtlSdrDriver_IVars, 1);
        return false;
    }
    bhrf_session_init(&ivars->session);
    brtl_own_init(&ivars->own);
    ivars->appliedGainTenthDb = -1;
    return true;
}

void BelksiriRtlSdrDriver::free(void)
{
    if (ivars != nullptr && ivars->lock != nullptr) {
        IOLockFree(ivars->lock);
        ivars->lock = nullptr;
    }
    IOSafeDeleteNULL(ivars, BelksiriRtlSdrDriver_IVars, 1);
    super::free();
}

kern_return_t
IMPL(BelksiriRtlSdrDriver, Start)
{
    kern_return_t ret = Start(provider, SUPERDISPATCH);
    if (ret != kIOReturnSuccess) {
        BRTL_LOG("Start: super a échoué (0x%x)", ret);
        return ret;
    }

    BRTL_LOG("Start: protocole client v%u, build %u, équipe cliente %s",
             BRTL_CLIENT_PROTOCOL_VERSION, (unsigned)BRTL_BUILD_ID,
             kBrtlExpectedTeam[0] ? "configurée" : "ABSENTE (tout client refusé)");

    ivars->device = OSDynamicCast(IOUSBHostDevice, provider);
    if (ivars->device == nullptr) {
        BRTL_LOG("Start: le provider n'est pas un IOUSBHostDevice");
        Stop(provider, SUPERDISPATCH);
        return kIOReturnNoDevice;
    }
    ivars->device->retain();

    ret = ivars->device->Open(this, 0, 0);
    if (ret != kIOReturnSuccess) {
        BRTL_LOG("Start: Open(device) a échoué (0x%x)", ret);
        goto fail;
    }

    /* Descripteur de périphérique : c'est ici, et seulement ici, que bcdDevice
     * est accessible. L'application ne peut pas l'obtenir autrement sur iPadOS. */
    {
        const IOUSBDeviceDescriptor *desc = ivars->device->CopyDeviceDescriptor();
        if (desc == nullptr) {
            BRTL_LOG("Start: descripteur de périphérique indisponible");
            ret = kIOReturnNotFound;
            goto fail;
        }
        ivars->idVendor  = desc->idVendor;
        ivars->idProduct = desc->idProduct;
        ivars->bcdDevice = desc->bcdDevice;
        IOUSBHostFreeDescriptor(desc);

        /* Ceinture et bretelles : le matching du Info.plist a déjà filtré. */
        if (!brtl_identity_usb_id_supported(ivars->idVendor, ivars->idProduct)) {
            BRTL_LOG("Start: VID/PID inattendus %04x:%04x — abandon",
                     ivars->idVendor, ivars->idProduct);
            ret = kIOReturnNotPermitted;
            goto fail;
        }
        BRTL_LOG("Start: RTL2832U %04x:%04x bcdDevice=%04x",
                 ivars->idVendor, ivars->idProduct, ivars->bcdDevice);
    }

    /* matchInterfaces = false : nous restons seul propriétaire de l'appareil. */
    ret = ivars->device->SetConfiguration(1, false);
    if (ret != kIOReturnSuccess) {
        BRTL_LOG("Start: SetConfiguration(1) a échoué (0x%x)", ret);
        goto fail;
    }

    /* Interface 0 : celle du RTL2832U qui porte l'endpoint bulk IN. */
    {
        uintptr_t iter = 0;
        ret = ivars->device->CreateInterfaceIterator(&iter);
        if (ret != kIOReturnSuccess) {
            BRTL_LOG("Start: CreateInterfaceIterator a échoué (0x%x)", ret);
            goto fail;
        }
        ret = ivars->device->CopyInterface(iter, &ivars->interface);
        ivars->device->DestroyInterfaceIterator(iter);
        if (ret != kIOReturnSuccess || ivars->interface == nullptr) {
            BRTL_LOG("Start: aucune interface (0x%x)", ret);
            ret = kIOReturnNotFound;
            goto fail;
        }
    }

    ret = ivars->interface->Open(this, 0, nullptr);
    if (ret != kIOReturnSuccess) {
        BRTL_LOG("Start: Open(interface) a échoué (0x%x)", ret);
        goto fail;
    }

    /* UNIQUEMENT le pipe bulk IN : le pilote est en réception seule. */
    ret = ivars->interface->CopyPipe(BRTL_BULK_IN_ENDPOINT, &ivars->inPipe);
    if (ret != kIOReturnSuccess || ivars->inPipe == nullptr) {
        BRTL_LOG("Start: pipe bulk IN 0x%02x introuvable (0x%x)",
                 BRTL_BULK_IN_ENDPOINT, ret);
        ret = kIOReturnNotFound;
        goto fail;
    }
    BRTL_LOG("Start: pipe bulk IN 0x%02x ouvert — aucun pipe OUT ouvert",
             BRTL_BULK_IN_ENDPOINT);

    /* Tampon de contrôle, alloué une seule fois. */
    ret = IOBufferMemoryDescriptor::Create(kIOMemoryDirectionInOut,
                                           BHRF_CTRL_MAX_DATA, 8,
                                           &ivars->ctrlBuffer);
    if (ret != kIOReturnSuccess || ivars->ctrlBuffer == nullptr) {
        BRTL_LOG("Start: allocation du tampon de contrôle impossible (0x%x)", ret);
        ret = kIOReturnNoMemory;
        goto fail;
    }
    {
        IOAddressSegment seg = {};
        ret = ivars->ctrlBuffer->GetAddressRange(&seg);
        if (ret != kIOReturnSuccess) {
            BRTL_LOG("Start: GetAddressRange a échoué (0x%x)", ret);
            goto fail;
        }
        ivars->ctrlBytes = reinterpret_cast<uint8_t *>(seg.address);
    }

    bhrf_session_init(&ivars->session);
    /* Transport de librtlsdr : la porte RtlControlRequest de CE pilote. */
    brtl_bus_init(&ivars->bus, BelksiriRtlSdrDriverBusGate, this);

    /* Publication dans le registre IOKit. Ce que l'application devra chercher
     * n'est PAS la classe « BelksiriRtlSdrDriver » — le registre enregistre ce
     * service sous la classe IOUserService — mais le NOM d'entrée, qui vaut la
     * valeur d'IOUserClass. La trace le rappelle au bon endroit : c'est
     * exactement l'erreur qui a coûté la première session sur iPad. */
    RegisterService();
    BRTL_LOG("Start: RegisterService() — service publié sous le NOM « %s », "
             "classe de l'entrée : IOUserService", "BelksiriRtlSdrDriver");
    BRTL_LOG("Start: prêt");
    return kIOReturnSuccess;

fail:
    bhrf_session_provider_terminated(&ivars->session);
    Stop(provider, SUPERDISPATCH);
    return ret;
}

kern_return_t
IMPL(BelksiriRtlSdrDriver, Stop)
{
    BRTL_LOG("Stop: le fournisseur a disparu — terminaison définitive");
    BRTL_LOG("Stop: autorisées=%llu refusées=%llu erreursUSB=%llu",
             ivars ? ivars->controlAllowed : 0,
             ivars ? ivars->controlDenied : 0,
             ivars ? ivars->controlUsbErrors : 0);

    if (ivars != nullptr) {
        if (ivars->lock != nullptr) IOLockLock(ivars->lock);
        /* Ordre obligatoire : arrêter les transferts AVANT de relâcher le pipe
         * et l'interface. Une complétion qui arriverait après la libération du
         * ring écrirait dans de la mémoire rendue. */
        BHRFSimpleStatus st = {};
        RtlStopRx(&st);

        /* Le contrôleur ne doit plus rien émettre : le périphérique est parti. */
        if (ivars->ctrl != nullptr) {
            brtl_ctrl_mark_lost(ivars->ctrl);
            brtl_ctrl_close(ivars->ctrl);
            ivars->ctrl = nullptr;
        }
        brtl_own_reset(&ivars->own);

        /* ICI, et nulle part ailleurs, se déclare la terminaison : IOKit
         * n'appelle Stop() que lorsque le périphérique a réellement disparu.
         * C'est une preuve, là où le code de retour d'un transfert n'est qu'un
         * indice — confondre les deux bloquait la session après un simple
         * Stop RX. */
        bhrf_session_provider_terminated(&ivars->session);
        if (ivars->ringHeader != nullptr) {
            /* Publier l'état AVANT de partir : l'application scrute ce champ
             * et doit voir « terminé » plutôt qu'un ring qui se tait. */
            ivars->ringHeader->deviceState = kBHRFDeviceStateTerminated;
            __atomic_thread_fence(__ATOMIC_RELEASE);
        }

        for (uint32_t i = 0; i < BHRF_MAX_INFLIGHT; i++) {
            OSSafeReleaseNULL(ivars->readActions[i]);
        }
        for (uint32_t i = 0; i < BHRF_RING_BLOCK_COUNT; i++) {
            OSSafeReleaseNULL(ivars->slots[i]);
        }
        ivars->ringHeader = nullptr;
        OSSafeReleaseNULL(ivars->ringBuffer);

        OSSafeReleaseNULL(ivars->ctrlBuffer);
        ivars->ctrlBytes = nullptr;

        OSSafeReleaseNULL(ivars->inPipe);

        if (ivars->interface != nullptr) {
            ivars->interface->Close(this, 0);
            OSSafeReleaseNULL(ivars->interface);
        }
        if (ivars->device != nullptr) {
            ivars->device->Close(this, 0);
            OSSafeReleaseNULL(ivars->device);
        }
        if (ivars->lock != nullptr) IOLockUnlock(ivars->lock);
    }

    return Stop(provider, SUPERDISPATCH);
}

kern_return_t
IMPL(BelksiriRtlSdrDriver, NewUserClient)
{
    IOService *client = nullptr;

    /* Ce journal est le pendant, côté pilote, de la trace « IOServiceOpen » de
     * l'application. Voir les deux permet de trancher immédiatement entre
     * « l'application n'a pas trouvé le service » (aucune ligne ici) et
     * « elle l'a trouvé mais l'ouverture est refusée » (une ligne d'échec). */
    BRTL_LOG("NewUserClient: demande reçue (type %u) — une application ouvre "
             "le user client", type);

    kern_return_t ret = Create(this, "UserClientProperties", &client);
    if (ret != kIOReturnSuccess) {
        BRTL_LOG("NewUserClient: Create(UserClientProperties) a ÉCHOUÉ (0x%x). "
                 "Vérifier IOUserClass=BelksiriRtlSdrUserClient et "
                 "IOClass=IOUserUserClient dans le Info.plist.", ret);
        return ret;
    }

    *userClient = OSDynamicCast(IOUserClient, client);
    if (*userClient == nullptr) {
        BRTL_LOG("NewUserClient: l'objet créé n'est pas un IOUserClient");
        client->release();
        return kIOReturnError;
    }

    BRTL_LOG("NewUserClient: user client CRÉÉ et publié");
    return kIOReturnSuccess;
}

/* ───────────────────────── Surface pour le user client ──────────────────── */

kern_return_t BelksiriRtlSdrDriver::RtlGetProtocolInfo(BHRFProtocolInfo *outInfo)
{
    if (outInfo == nullptr || ivars == nullptr) {
        return kIOReturnBadArgument;
    }
    outInfo->protocolVersion = BRTL_CLIENT_PROTOCOL_VERSION;
    outInfo->driverBuildId   = (uint32_t)BRTL_BUILD_ID;
    outInfo->bcdDevice       = ivars->bcdDevice;
    outInfo->idVendor        = ivars->idVendor;
    outInfo->idProduct       = ivars->idProduct;
    outInfo->deviceState     = (uint16_t)bhrf_session_device_state(&ivars->session);
    outInfo->capabilities    = BRTL_CAP_TUNER_CONTROL | BRTL_CAP_DEVICE_INFO |
                               BRTL_CAP_OWNERSHIP;
    return kIOReturnSuccess;
}

kern_return_t BelksiriRtlSdrDriver::RtlControlRequest(const BHRFControlRequest *req,
                                                         BHRFControlResponse *resp)
{
    if (req == nullptr || resp == nullptr || ivars == nullptr) {
        return kIOReturnBadArgument;
    }

    memset(resp, 0, sizeof(*resp));

    /* ── LA porte. Rien au-dessus de cette ligne ne touche le bus USB. ──
     * L'évaluation précède tout, et sur un refus la fonction retourne AVANT
     * d'avoir approché `interface->DeviceRequest`. C'est pour cela que
     * `resp->ioReturn` reste à 0 : cette valeur nulle est la preuve, côté
     * application et dans les journaux, qu'aucun transfert n'a été tenté. */
    const BRTLDenyReason deny = brtl_policy_evaluate(req->bmRequestType,
                                                     req->bRequest, req->wValue,
                                                     req->wIndex, req->wLength);
    if (deny != kBRTLDenyNone) {
        ivars->controlDenied++;
        resp->status     = kBHRFStatusDenied;
        resp->denyReason = (uint32_t)deny;
        resp->ioReturn   = 0;
        BRTL_DENY_LOG("bmRequestType=0x%02x bRequest=%u wValue=0x%04x "
                      "wIndex=0x%04x wLength=%u motif=%s "
                      "— AUCUN octet émis sur le bus (refus #%llu)",
                      req->bmRequestType, req->bRequest, req->wValue,
                      req->wIndex, req->wLength, brtl_deny_reason_name(deny),
                      ivars->controlDenied);
        return kIOReturnSuccess; /* l'appel a abouti ; la requête, non */
    }

    if (ivars->interface == nullptr || ivars->ctrlBuffer == nullptr) {
        resp->status = kBHRFStatusNotOpen;
        return kIOReturnSuccess;
    }
    /* Les requêtes de contrôle restent permises dans TOUS les états sauf la
     * terminaison — y compris pendant un arrêt en cours, puisque c'est ainsi
     * qu'on remet le périphérique au repos juste après avoir abandonné les
     * transferts. */
    if (!bhrf_session_can_control(&ivars->session)) {
        resp->status = kBHRFStatusTerminated;
        BRTL_LOG("contrôle refusé : session %s",
                 bhrf_session_state_name(bhrf_session_state(&ivars->session)));
        return kIOReturnSuccess;
    }

    /* Phase données sortante : recopier ce que l'application a fourni. */
    const uint16_t len = req->wLength;
    if (len > 0 && ivars->ctrlBytes != nullptr) {
        if ((req->bmRequestType & 0x80u) == 0u) {
            memcpy(ivars->ctrlBytes, req->data, len);
        } else {
            memset(ivars->ctrlBytes, 0, len);
        }
    }
    ivars->ctrlBuffer->SetLength(len);

    const bool isIn = (req->bmRequestType & 0x80u) != 0u;

    uint16_t transferred = 0;
    const kern_return_t ret = ivars->interface->DeviceRequest(
        req->bmRequestType, req->bRequest, req->wValue, req->wIndex, len,
        (len > 0) ? ivars->ctrlBuffer : nullptr, &transferred,
        1000 /* ms — jamais 0 : une requête de contrôle ne doit pas pouvoir
              * bloquer indéfiniment le user client. */);

    resp->ioReturn = (int32_t)ret;

    /* Journal COMPLET de chaque requête autorisée, succès comme échec.
     *
     * Tout y est parce que le premier essai matériel n'a produit que
     * « usb_error » : ni la requête, ni sa direction, ni son code. Diagnostiquer
     * a alors demandé de rebrancher le matériel, c'est-à-dire au moment le plus
     * cher. Une ligne par requête de contrôle ne coûte rien : elles sont rares,
     * contrairement aux complétions de transfert. */
    if (ret != kIOReturnSuccess) {
        const BHRFUsbFailureKind kind = bhrf_classify_usb_failure((int32_t)ret);
        ivars->controlUsbErrors++;
        resp->status = kBHRFStatusUsbError;
        BRTL_LOG("USB-ÉCHEC bmRequestType=0x%02x bRequest=%u wValue=0x%04x "
                 "wIndex=0x%04x wLength=%u dir=%s → kern_return_t=0x%08x "
                 "classe=%s transféré=%u",
                 req->bmRequestType, req->bRequest, req->wValue, req->wIndex,
                 len, isIn ? "IN" : "OUT", (unsigned)ret,
                 bhrf_usb_failure_kind_name(kind), transferred);
        return kIOReturnSuccess;
    }

    ivars->controlAllowed++;
    resp->status = kBHRFStatusOK;
    resp->actualLength = transferred;
    /* Pas de journal par requête réussie : librtlsdr en émet plusieurs
     * centaines à l'initialisation et à chaque accord. Les compteurs
     * autorisées / refusées / erreurs sont journalisés au Stop. */
    if (transferred > 0 && isIn &&
        ivars->ctrlBytes != nullptr) {
        const uint16_t copy =
            (transferred > BHRF_CTRL_MAX_DATA) ? BHRF_CTRL_MAX_DATA : transferred;
        memcpy(resp->data, ivars->ctrlBytes, copy);
    }
    return kIOReturnSuccess;
}

kern_return_t BelksiriRtlSdrDriver::RtlGetRingStats(BHRFRingStats *outStats)
{
    if (outStats == nullptr || ivars == nullptr) {
        return kIOReturnBadArgument;
    }
    memset(outStats, 0, sizeof(*outStats));

    /* Lecture ACQUIRE de l'index publié, pour apparier la barrière RELEASE de
     * ReadComplete : les compteurs lus ensuite ne peuvent pas être plus anciens
     * que lui. */
    outStats->writeIndex = __atomic_load_n(&ivars->produced, __ATOMIC_ACQUIRE);
    outStats->blocksProduced   = outStats->writeIndex;
    outStats->bytesProduced    = ivars->bytesProduced;
    /* Les complétions d'un arrêt VOLONTAIRE ne figurent pas ici : ce ne sont
     * pas des erreurs de bus, et les y ajouter fausserait le critère go/no-go. */
    outStats->usbErrors        = ivars->ringUsbErrors;
    outStats->lastCompletionNs = ivars->lastCompletionNs;
    outStats->deviceState      = bhrf_session_device_state(&ivars->session);
    outStats->lastUsbStatus    = ivars->lastUsbStatus;
    outStats->blockCount       = ivars->blockCount;   /* 0 tant que non alloué */
    outStats->blockBytes       = ivars->blockBytes;
    outStats->inFlight         = ivars->session.inFlight;

    /* `overflowBlocks` reste à zéro ICI, et ce n'est pas un oubli : le pilote
     * ne peut pas le connaître. Le ring ne contient volontairement AUCUN
     * curseur de lecture — le pilote n'a ainsi jamais à faire confiance à une
     * valeur écrite par l'espace utilisateur. Seul le consommateur sait de
     * combien il a été distancé ; c'est RingReader qui le compte, et le
     * transport qui renseigne ce champ avant de le remonter au modèle. */
    return kIOReturnSuccess;
}

/* ─────────────────────────── Étape 5 : réception ────────────────────────── */

/* Alloue le ring : un en-tête de 4 KiB suivi de blockCount blocs de données.
 *
 * Le choix décisif est le ZÉRO-COPIE. Chaque slot du ring reçoit son propre
 * sous-descripteur, et c'est CE sous-descripteur qui est remis à AsyncIO : le
 * contrôleur USB écrit donc directement dans la mémoire que l'application a
 * mappée. À 40 Mo/s, une recopie intermédiaire coûterait une traversée de plus
 * de toute la bande passante mémoire, sur le chemin critique, pour rien. */
kern_return_t BelksiriRtlSdrDriver::RtlAllocateRingIfNeeded(uint32_t blockBytes)
{
    if (ivars->ringBuffer != nullptr) {
        return kIOReturnSuccess;
    }

    const uint32_t count = BHRF_RING_BLOCK_COUNT;
    const uint64_t total = (uint64_t)BHRF_RING_HEADER_BYTES +
                           (uint64_t)count * (uint64_t)blockBytes;

    kern_return_t ret = IOBufferMemoryDescriptor::Create(kIOMemoryDirectionInOut,
                                                          total, 4096,
                                                          &ivars->ringBuffer);
    if (ret != kIOReturnSuccess || ivars->ringBuffer == nullptr) {
        BRTL_LOG("ring: allocation de %llu octets impossible (0x%x)", total, ret);
        return kIOReturnNoMemory;
    }
    ivars->ringBuffer->SetLength(total);

    IOAddressSegment seg = {};
    ret = ivars->ringBuffer->GetAddressRange(&seg);
    if (ret != kIOReturnSuccess) {
        BRTL_LOG("ring: GetAddressRange a échoué (0x%x)", ret);
        OSSafeReleaseNULL(ivars->ringBuffer);
        return ret;
    }

    ivars->ringHeader = reinterpret_cast<BHRFRingHeader *>(seg.address);
    memset(ivars->ringHeader, 0, BHRF_RING_HEADER_BYTES);
    ivars->ringHeader->magic         = BHRF_RING_MAGIC;
    ivars->ringHeader->layoutVersion = BRTL_RING_LAYOUT_VERSION;
    ivars->ringHeader->blockCount    = count;
    ivars->ringHeader->blockBytes    = blockBytes;
    ivars->ringHeader->headerBytes   = BHRF_RING_HEADER_BYTES;
    ivars->ringHeader->deviceState   = kBHRFDeviceStateIdle;

    /* Un sous-descripteur par slot, créé une fois pour toutes. Les créer à
     * chaque soumission serait une allocation par transfert, soit 160 par
     * seconde à 20 Msps. */
    for (uint32_t i = 0; i < count; i++) {
        const uint64_t offset = (uint64_t)BHRF_RING_HEADER_BYTES +
                                (uint64_t)i * (uint64_t)blockBytes;
        ret = IOMemoryDescriptor::CreateSubMemoryDescriptor(
                  kIOMemoryDirectionIn, offset, blockBytes,
                  ivars->ringBuffer, &ivars->slots[i]);
        if (ret != kIOReturnSuccess || ivars->slots[i] == nullptr) {
            BRTL_LOG("ring: sous-descripteur %u impossible (0x%x)", i, ret);
            for (uint32_t j = 0; j < i; j++) {
                OSSafeReleaseNULL(ivars->slots[j]);
            }
            ivars->ringHeader = nullptr;
            OSSafeReleaseNULL(ivars->ringBuffer);
            return ret;
        }
    }

    ivars->blockCount = count;
    ivars->blockBytes = blockBytes;
    BRTL_LOG("ring: %u blocs de %u octets (%llu octets, zéro-copie)",
             count, blockBytes, total);
    return kIOReturnSuccess;
}

kern_return_t BelksiriRtlSdrDriver::RtlCopyRingMemory(IOMemoryDescriptor **outMemory)
{
    if (outMemory == nullptr || ivars == nullptr) {
        return kIOReturnBadArgument;
    }
    if (ivars->ringBuffer == nullptr) {
        return kIOReturnNotReady;   /* démarrer la réception d'abord */
    }
    ivars->ringBuffer->retain();    /* une référence pour l'appelant */
    *outMemory = ivars->ringBuffer;
    return kIOReturnSuccess;
}

kern_return_t BelksiriRtlSdrDriver::RtlStartRx(const BHRFStartRxRequest *req,
                                                  BHRFSimpleStatus *outStatus)
{
    if (req == nullptr || outStatus == nullptr || ivars == nullptr) {
        return kIOReturnBadArgument;
    }
    memset(outStatus, 0, sizeof(*outStatus));

    if (bhrf_session_state(&ivars->session) == kBHRFSessionTerminated) {
        outStatus->status = kBHRFStatusTerminated;
        return kIOReturnSuccess;
    }
    if (!bhrf_session_can_start(&ivars->session)) {
        outStatus->status = kBHRFStatusAlreadyStreaming;
        BRTL_LOG("rx: démarrage refusé — session %s",
                 bhrf_session_state_name(bhrf_session_state(&ivars->session)));
        return kIOReturnSuccess;
    }
    if (ivars->inPipe == nullptr) {
        outStatus->status = kBHRFStatusNotOpen;
        return kIOReturnSuccess;
    }

    /* Géométrie : la taille de bloc demandée est bornée par celle du contrat et
     * alignée sur 512 octets, taille de paquet d'un endpoint bulk haute vitesse.
     * Un bloc non aligné ferait terminer chaque transfert sur un paquet court,
     * ce qui, sur un endpoint bulk, signale une fin de transfert prématurée. */
    uint32_t blockBytes = req->bufferBytes;
    if (blockBytes == 0 || blockBytes > BHRF_RING_BLOCK_BYTES) {
        blockBytes = BHRF_RING_BLOCK_BYTES;
    }
    blockBytes &= ~511u;
    if (blockBytes < 4096u) {
        blockBytes = 4096u;
    }

    kern_return_t ret = RtlAllocateRingIfNeeded(blockBytes);
    if (ret != kIOReturnSuccess) {
        outStatus->status   = kBHRFStatusNoMemory;
        outStatus->ioReturn = (int32_t)ret;
        return kIOReturnSuccess;
    }
    /* Le ring existe déjà d'une session précédente : c'est sa géométrie qui
     * fait foi, pas la demande. */
    blockBytes = ivars->blockBytes;

    const uint32_t inFlight = bhrf_clamp_in_flight(req->buffersInFlight,
                                                   ivars->blockCount);
    if (inFlight == 0u) {
        outStatus->status = kBHRFStatusBadArgument;
        BRTL_LOG("rx: géométrie de ring inexploitable (%u blocs)", ivars->blockCount);
        return kIOReturnSuccess;
    }
    ivars->buffersInFlight = inFlight;

    /* Repartir d'un ring vierge : les compteurs d'une campagne précédente ne
     * doivent pas s'additionner à ceux de la suivante. */
    ivars->produced         = 0;
    ivars->bytesProduced    = 0;
    ivars->ringUsbErrors    = 0;
    ivars->lastUsbStatus    = 0;
    ivars->lastCompletionNs = 0;
    memset(ivars->ringHeader->slots, 0, sizeof(ivars->ringHeader->slots));
    ivars->ringHeader->blocksProduced   = 0;
    ivars->ringHeader->bytesProduced    = 0;
    ivars->ringHeader->overflowBlocks   = 0;
    ivars->ringHeader->usbErrors        = 0;
    ivars->ringHeader->lastCompletionNs = 0;
    ivars->ringHeader->lastUsbStatus    = 0;
    ivars->ringHeader->deviceState      = kBHRFDeviceStateReceiving;
    __atomic_store_n(bhrf_write_index_ptr(ivars->ringHeader), (uint64_t)0,
                     __ATOMIC_RELEASE);

    /* L'état passe à « réception » AVANT la première soumission : une
     * complétion peut survenir pendant la boucle, et doit trouver l'état
     * cohérent. `inFlight` est remis à zéro puis incrémenté à chaque
     * soumission réussie, pour ne compter que ce qui est réellement parti. */
    bhrf_session_started(&ivars->session, 0);

    for (uint32_t i = 0; i < inFlight; i++) {
        if (ivars->readActions[i] == nullptr) {
            ret = CreateActionReadComplete(sizeof(uint32_t), &ivars->readActions[i]);
            if (ret != kIOReturnSuccess || ivars->readActions[i] == nullptr) {
                BRTL_LOG("rx: CreateActionReadComplete %u a échoué (0x%x)", i, ret);
                break;
            }
        }
        /* La référence de l'action porte le slot que ce transfert remplit. */
        uint32_t *slotRef =
            reinterpret_cast<uint32_t *>(ivars->readActions[i]->GetReference());
        if (slotRef == nullptr) {
            ret = kIOReturnNoMemory;
            break;
        }
        *slotRef = i;

        ret = ivars->inPipe->AsyncIO(ivars->slots[i], blockBytes,
                                     ivars->readActions[i],
                                     BHRF_TRANSFER_TIMEOUT_MS);
        if (ret != kIOReturnSuccess) {
            BRTL_LOG("rx: AsyncIO initial %u a échoué (0x%x)", i, ret);
            break;
        }
        ivars->session.inFlight++;
    }

    if (ivars->session.inFlight == 0) {
        bhrf_session_finish_stop(&ivars->session);
        ivars->ringHeader->deviceState = bhrf_session_device_state(&ivars->session);
        outStatus->status   = kBHRFStatusUsbError;
        outStatus->ioReturn = (int32_t)ret;
        BRTL_LOG("rx: aucun transfert n'a pu être soumis");
        return kIOReturnSuccess;
    }

    outStatus->status     = kBHRFStatusOK;
    outStatus->blockCount = ivars->blockCount;
    outStatus->blockBytes = ivars->blockBytes;
    BRTL_LOG("rx: démarré — %u transferts en vol, blocs de %u octets",
             ivars->session.inFlight, ivars->blockBytes);
    return kIOReturnSuccess;
}

kern_return_t BelksiriRtlSdrDriver::RtlStopRx(BHRFSimpleStatus *outStatus)
{
    if (ivars == nullptr) {
        return kIOReturnBadArgument;
    }
    if (outStatus != nullptr) {
        memset(outStatus, 0, sizeof(*outStatus));
    }

    /* Entrer explicitement en « arrêt en cours ». C'est cet état, et lui seul,
     * qui permet aux complétions qui vont suivre d'être reconnues pour ce
     * qu'elles sont — les conséquences ATTENDUES d'un abandon — plutôt que
     * prises pour la disparition du périphérique. */
    if (!bhrf_session_begin_stop(&ivars->session)) {
        if (outStatus != nullptr) {
            outStatus->status =
                (bhrf_session_state(&ivars->session) == kBHRFSessionTerminated)
                    ? kBHRFStatusTerminated : kBHRFStatusNotStreaming;
        }
        return kIOReturnSuccess;
    }

    if (ivars->inPipe != nullptr) {
        /* forClient = nullptr, et c'est OBLIGATOIRE sur un pipe bulk.
         *
         * La documentation d'IOUSBHostPipe::Abort est explicite : « If NULL,
         * all requests will be aborted. Only control endpoints can specify a
         * non-NULL value. » Ce paramètre valait `this`. L'abandon n'avait donc
         * jamais lieu : les transferts restaient en vol, le périphérique passait
         * ensuite en mode OFF, et ces transferts finissaient par échouer en
         * « ne répond pas » — ce que la complétion prenait pour un
         * débranchement. C'est l'origine exacte du blocage après Stop RX. */
        kern_return_t ret =
            ivars->inPipe->Abort(kIOUSBAbortSynchronous, kIOReturnAborted, nullptr);
        if (ret != kIOReturnSuccess) {
            BRTL_LOG("rx: Abort a répondu 0x%08x (%s)", (unsigned)ret,
                     bhrf_usb_failure_kind_name(
                         bhrf_classify_usb_failure((int32_t)ret)));
        }

        /* Un endpoint bulk passe à l'état « halted » après toute erreur d'E/S
         * autre qu'un délai dépassé — un abandon en est une. Sans ce nettoyage,
         * le PROCHAIN démarrage échouerait, et l'on aurait corrigé un blocage
         * pour en créer un autre.
         *
         * `withRequest = false` : c'est nous qui avons abandonné, le périphérique n'a
         * rien refusé. Lui envoyer un CLEAR_FEATURE ENDPOINT_HALT alors qu'il
         * n'a pas calé serait lui parler d'un incident qui n'a pas eu lieu. */
        ret = ivars->inPipe->ClearStall(false);
        if (ret != kIOReturnSuccess) {
            BRTL_LOG("rx: ClearStall a répondu 0x%08x", (unsigned)ret);
        }
    }

    /* Retour au repos. Inconditionnel, sauf terminaison réelle du fournisseur :
     * la version précédente ne restaurait l'état que s'il valait encore
     * « réception », de sorte qu'un « terminé » posé à tort par une complétion
     * n'était JAMAIS révoqué. Il fallait débrancher le périphérique. */
    bhrf_session_finish_stop(&ivars->session);

    if (ivars->ringHeader != nullptr) {
        ivars->ringHeader->deviceState = bhrf_session_device_state(&ivars->session);
        __atomic_thread_fence(__ATOMIC_RELEASE);
    }

    BRTL_LOG("rx: arrêté — %llu blocs, %llu octets, %llu erreurs USB, "
             "%llu complétion(s) d'abandon (qui ne sont PAS des erreurs), "
             "session %s",
             ivars->produced, ivars->bytesProduced, ivars->ringUsbErrors,
             ivars->session.stopDrained,
             bhrf_session_state_name(bhrf_session_state(&ivars->session)));

    if (outStatus != nullptr) {
        outStatus->status     = kBHRFStatusOK;
        outStatus->blockCount = ivars->blockCount;
        outStatus->blockBytes = ivars->blockBytes;
    }
    return kIOReturnSuccess;
}

/*
 * La complétion. Chemin critique : environ 160 passages par seconde à 20 Msps.
 *
 * La discipline de publication est celle annoncée dans bhrf_shared.h, et son
 * ORDRE est tout :
 *   1. écrire validBytes et captureNs du slot ;
 *   2. publier writeIndex en RELEASE.
 * Inverser ces deux temps laisserait l'application lire un bloc annoncé mais
 * dont les métadonnées ne sont pas encore écrites — un défaut qui ne se
 * manifeste que sous charge, c'est-à-dire exactement à 20 Msps.
 */
void
IMPL(BelksiriRtlSdrDriver, ReadComplete)
{
    if (ivars == nullptr || ivars->ringHeader == nullptr) {
        return;
    }

    uint32_t *slotRef = (action != nullptr)
        ? reinterpret_cast<uint32_t *>(action->GetReference()) : nullptr;
    if (slotRef == nullptr) {
        return;
    }
    const uint32_t slot = *slotRef % ivars->blockCount;

    /* LA décision, prise par une fonction en C pur testée sur hôte : que faire
     * de cette complétion ? Elle seule sait distinguer un abandon volontaire —
     * dont les erreurs sont attendues — d'une perte réelle du périphérique. */
    const BHRFCompletionAction todo =
        bhrf_session_completion(&ivars->session, (int32_t)status);

    if (status != kIOReturnSuccess) {
        ivars->lastUsbStatus = (uint32_t)status;
        ivars->ringHeader->lastUsbStatus = (uint32_t)status;
        /* Les erreurs d'un arrêt volontaire ne sont PAS comptées comme erreurs
         * USB : la session les a rangées à part, et les additionner ici
         * fausserait la mesure d'une campagne. */
        if (todo != kBHRFCompletionDrain) {
            ivars->ringUsbErrors = ivars->session.usbErrors;
            ivars->ringHeader->usbErrors = ivars->ringUsbErrors;
        }
        actualByteCount = 0;   /* bloc vide, mais le rang est conservé */
    }

    if (todo == kBHRFCompletionDrain) {
        /* Rien à publier : ni le rang ni les octets. Au prochain démarrage, le
         * ring repart de zéro, l'invariant slot == produced % blockCount est
         * donc rétabli par construction. */
        return;
    }

    /* Le rang avance à CHAQUE complétion publiée, y compris en erreur. C'est ce
     * qui maintient l'invariant slot == produced % blockCount : un rang sauté
     * décalerait durablement l'application d'un bloc. Un bloc en erreur est
     * publié avec validBytes = 0, et le consommateur le saute. */
    ivars->ringHeader->slots[slot].validBytes = actualByteCount;
    ivars->ringHeader->slots[slot].captureNs  = completionTimestamp;

    ivars->produced++;
    ivars->bytesProduced    += actualByteCount;
    ivars->lastCompletionNs  = completionTimestamp;

    ivars->ringHeader->blocksProduced   = ivars->produced;
    ivars->ringHeader->bytesProduced    = ivars->bytesProduced;
    ivars->ringHeader->lastCompletionNs = completionTimestamp;
    ivars->ringHeader->deviceState      =
        bhrf_session_device_state(&ivars->session);

    /* LA publication. Tout ce qui précède devient visible à l'application au
     * moment exact où cette écriture le devient. */
    __atomic_store_n(bhrf_write_index_ptr(ivars->ringHeader), ivars->produced,
                     __ATOMIC_RELEASE);

    if (todo == kBHRFCompletionPublishOnly) {
        BRTL_LOG("rx: réception interrompue — session %s, dernier code 0x%08x",
                 bhrf_session_state_name(bhrf_session_state(&ivars->session)),
                 (unsigned)status);
        return;
    }

    /* Resoumission sur le slot situé `buffersInFlight` plus loin. */
    const uint32_t next = bhrf_next_slot(slot, ivars->buffersInFlight,
                                        ivars->blockCount);
    *slotRef = next;

    const kern_return_t ret = ivars->inPipe->AsyncIO(ivars->slots[next],
                                                     ivars->blockBytes,
                                                     action,
                                                     BHRF_TRANSFER_TIMEOUT_MS);
    if (ret == kIOReturnSuccess) {
        ivars->session.inFlight++;
    } else {
        ivars->session.usbErrors++;
        ivars->ringUsbErrors = ivars->session.usbErrors;
        ivars->lastUsbStatus = (uint32_t)ret;
        ivars->ringHeader->usbErrors     = ivars->ringUsbErrors;
        ivars->ringHeader->lastUsbStatus = (uint32_t)ret;
        BRTL_LOG("rx: resoumission impossible (0x%08x) — %u transferts restants",
                 (unsigned)ret, ivars->session.inFlight);
    }
}

/* ───────────────────────── Protocole client v2 ──────────────────────────── */

namespace {

/* Verrou du pilote, relâché en sortie de portée. */
struct BrtlLockGuard {
    IOLock *lock;
    explicit BrtlLockGuard(IOLock *l) : lock(l) { if (lock) IOLockLock(lock); }
    ~BrtlLockGuard() { if (lock) IOLockUnlock(lock); }
};

void brtl_result(BRTLClientResult *out, BRTLClientStatus status,
                 int32_t controller = 0, int64_t applied = 0)
{
    if (out == nullptr) return;
    out->status = (int32_t)status;
    out->controllerStatus = controller;
    out->applied = applied;
}

void brtl_copy_name(char *dst, size_t cap, const char *src)
{
    if (dst == nullptr || cap == 0) return;
    size_t i = 0;
    if (src != nullptr) {
        for (; i + 1 < cap && src[i] != 0; i++) dst[i] = src[i];
    }
    dst[i] = 0;
}

} // namespace

kern_return_t BelksiriRtlSdrDriver::RtlEnsureControllerLocked(void)
{
    if (ivars->ctrl != nullptr) {
        return kIOReturnSuccess;
    }
    if (bhrf_session_state(&ivars->session) == kBHRFSessionTerminated ||
        ivars->interface == nullptr) {
        return kIOReturnNoDevice;
    }
    brtl_transport t = {};
    t.ctx = &ivars->bus;
    t.control_transfer = brtl_bus_control_transfer;

    brtl_device_info info = {};
    brtl_ctrl *c = nullptr;
    brtl_bus_begin_probe(&ivars->bus);
    const brtl_status st = brtl_ctrl_open(&t, &c, &info);
    brtl_bus_finish_probe(&ivars->bus, st == BRTL_OK ? info.tuner_i2c_addr : 0);
    ivars->devInfo = info;
    ivars->tunerStatus = (int32_t)st;
    if (st != BRTL_OK || c == nullptr) {
        BRTL_LOG("contrôleur : ouverture refusée (%s), tuner « %s »",
                 brtl_status_name(st), info.tuner_name);
        return kIOReturnUnsupported;
    }
    ivars->ctrl = c;

    /* Réglages de départ, ceux de rtl_tcp ; le client applique les siens. */
    uint32_t applied = 0;
    if (brtl_ctrl_set_sample_rate(c, BRTL_DEFAULT_RATE_HZ, &applied) == BRTL_OK) {
        ivars->sampleRateHz = applied;
    }
    applied = 0;
    const brtl_status fs = brtl_ctrl_set_center_frequency(c, BRTL_DEFAULT_FREQ_HZ, &applied);
    if (fs == BRTL_OK || fs == BRTL_ERR_PLL_UNLOCKED) {
        ivars->frequencyHz = applied;
    }
    int g = -1;
    brtl_ctrl_set_gain(c, -1, &g);
    ivars->manualGain = false;
    ivars->appliedGainTenthDb = -1;
    ivars->biasTee = false;
    ivars->rtlAgc = false;
    ivars->offsetTuning = false;
    ivars->directSampling = 0;
    ivars->ppm = 0;

    BRTL_LOG("contrôleur : tuner %s « %s » prêt (%llu transferts, %llu sondes "
             "sans réponse, %llu refus, %llu erreurs USB)",
             info.tuner_name, info.product, ivars->bus.transfers,
             ivars->bus.probe_misses, ivars->bus.denied, ivars->bus.usb_errors);
    return kIOReturnSuccess;
}

kern_return_t BelksiriRtlSdrDriver::RtlAcquire(OSObject *client, BRTLClientResult *out)
{
    if (ivars == nullptr || out == nullptr) return kIOReturnBadArgument;
    BrtlLockGuard guard(ivars->lock);
    if (bhrf_session_state(&ivars->session) == kBHRFSessionTerminated) {
        brtl_result(out, kBRTLClientTerminated);
        return kIOReturnSuccess;
    }
    const bool already = brtl_own_is_owner(&ivars->own, client);
    const BRTLClientStatus own = brtl_own_acquire(&ivars->own, client);
    if (own != kBRTLClientOK) {
        BRTL_LOG("acquire : refusé (%s) — la radio appartient à un autre client",
                 brtl_client_status_name(own));
        brtl_result(out, own);
        return kIOReturnSuccess;
    }
    if (RtlEnsureControllerLocked() != kIOReturnSuccess) {
        if (!already) brtl_own_release(&ivars->own, client);
        brtl_result(out, kBRTLClientTunerUnavailable, ivars->tunerStatus);
        return kIOReturnSuccess;
    }
    if (!already) {
        BRTL_LOG("acquire : radio attribuée (acquisition n°%llu)", ivars->own.acquires);
    }
    brtl_result(out, kBRTLClientOK);
    return kIOReturnSuccess;
}

/* Rend la radio au repos : réception arrêtée, bias-tee coupé. Verrou pris. */
static void brtl_quiesce_locked(BelksiriRtlSdrDriver *self, BelksiriRtlSdrDriver_IVars *iv)
{
    if (bhrf_session_state(&iv->session) == kBHRFSessionReceiving) {
        BHRFSimpleStatus st = {};
        self->RtlStopRx(&st);
    }
    if (iv->ctrl != nullptr && iv->biasTee) {
        brtl_ctrl_set_bias_tee(iv->ctrl, 0);
        iv->biasTee = false;
    }
}

kern_return_t BelksiriRtlSdrDriver::RtlRelease(OSObject *client, BRTLClientResult *out)
{
    if (ivars == nullptr || out == nullptr) return kIOReturnBadArgument;
    BrtlLockGuard guard(ivars->lock);
    if (!brtl_own_is_owner(&ivars->own, client)) {
        brtl_result(out, kBRTLClientNotOwner);
        return kIOReturnSuccess;
    }
    brtl_quiesce_locked(this, ivars);
    brtl_own_release(&ivars->own, client);
    BRTL_LOG("release : radio libérée par son propriétaire");
    brtl_result(out, kBRTLClientOK);
    return kIOReturnSuccess;
}

kern_return_t BelksiriRtlSdrDriver::RtlClientGone(OSObject *client)
{
    if (ivars == nullptr) return kIOReturnBadArgument;
    BrtlLockGuard guard(ivars->lock);
    if (!brtl_own_is_owner(&ivars->own, client)) {
        return kIOReturnSuccess;
    }
    /* Le propriétaire est parti sans Release (fermeture, plantage, fin du
     * processus) : c'est au pilote d'arrêter la réception. Sans cela, le
     * client suivant trouvait une session « déjà en réception ». */
    BRTL_LOG("propriétaire disparu — arrêt de la réception par le pilote");
    brtl_quiesce_locked(this, ivars);
    brtl_own_client_gone(&ivars->own, client);
    return kIOReturnSuccess;
}

kern_return_t BelksiriRtlSdrDriver::RtlCheckOwner(OSObject *client)
{
    if (ivars == nullptr) return kIOReturnBadArgument;
    BrtlLockGuard guard(ivars->lock);
    return brtl_own_is_owner(&ivars->own, client) ? kIOReturnSuccess : kIOReturnNotPermitted;
}

kern_return_t BelksiriRtlSdrDriver::RtlNoteRawControlDenied(void)
{
    if (ivars == nullptr) return kIOReturnBadArgument;
    BrtlLockGuard guard(ivars->lock);
    ivars->rawControlDenied++;
    return kIOReturnSuccess;
}

kern_return_t BelksiriRtlSdrDriver::RtlCommand(OSObject *client, const BRTLCommand *cmd,
                                               BRTLClientResult *out)
{
    if (ivars == nullptr || cmd == nullptr || out == nullptr) return kIOReturnBadArgument;
    BrtlLockGuard guard(ivars->lock);
    ivars->commandsReceived++;

    BRTLClientStatus gate = brtl_own_is_owner(&ivars->own, client)
                                ? brtl_command_validate(cmd) : kBRTLClientNotOwner;
    if (gate == kBRTLClientOK && RtlEnsureControllerLocked() != kIOReturnSuccess) {
        gate = kBRTLClientTunerUnavailable;
    }
    if (gate != kBRTLClientOK) {
        ivars->commandsRejected++;
        ivars->lastCommandStatus = (int32_t)gate;
        brtl_result(out, gate, gate == kBRTLClientTunerUnavailable ? ivars->tunerStatus : 0);
        return kIOReturnSuccess;
    }

    brtl_ctrl *c = ivars->ctrl;
    const int64_t v = cmd->value;
    brtl_status st = BRTL_OK;
    int64_t applied = 0;
    switch (cmd->opcode) {
    case kBRTLOpSetFrequency: {
        uint32_t a = 0;
        st = brtl_ctrl_set_center_frequency(c, (uint32_t)v, &a);
        /* PLL non verrouillée : la fréquence est en place, mais le client
         * doit le savoir (controllerStatus). */
        if (st == BRTL_OK || st == BRTL_ERR_PLL_UNLOCKED) ivars->frequencyHz = a;
        applied = a;
        break;
    }
    case kBRTLOpSetSampleRate: {
        uint32_t a = 0;
        st = brtl_ctrl_set_sample_rate(c, (uint32_t)v, &a);
        if (st == BRTL_OK) ivars->sampleRateHz = a;
        applied = a;
        break;
    }
    case kBRTLOpSetGainMode: {
        int g = -1;
        const bool manual = v != 0;
        st = brtl_ctrl_set_gain(c, manual ? ivars->gainTenthDb : -1, &g);
        if (st == BRTL_OK) {
            ivars->manualGain = manual;
            ivars->appliedGainTenthDb = manual ? g : -1;
        }
        applied = manual ? g : -1;
        break;
    }
    case kBRTLOpSetGain: {
        int g = 0;
        st = brtl_ctrl_set_gain(c, (int)v, &g);
        if (st == BRTL_OK) {
            ivars->gainTenthDb = (int32_t)v;
            ivars->manualGain = true;
            ivars->appliedGainTenthDb = g;
        }
        applied = g;
        break;
    }
    case kBRTLOpSetFreqCorrection:
        st = brtl_ctrl_set_freq_correction(c, (int)v);
        if (st == BRTL_OK) ivars->ppm = (int32_t)v;
        applied = ivars->ppm;
        break;
    case kBRTLOpSetRtlAgc:
        st = brtl_ctrl_set_rtl_agc(c, v != 0);
        if (st == BRTL_OK) ivars->rtlAgc = v != 0;
        applied = ivars->rtlAgc ? 1 : 0;
        break;
    case kBRTLOpSetDirectSampling:
        st = brtl_ctrl_set_direct_sampling(c, (int)v);
        if (st == BRTL_OK) ivars->directSampling = (uint32_t)v;
        applied = ivars->directSampling;
        break;
    case kBRTLOpSetOffsetTuning:
        st = brtl_ctrl_set_offset_tuning(c, v != 0);
        if (st == BRTL_OK) ivars->offsetTuning = v != 0;
        applied = ivars->offsetTuning ? 1 : 0;
        break;
    case kBRTLOpSetBiasTee:
        st = brtl_ctrl_set_bias_tee(c, v != 0);
        if (st == BRTL_OK) ivars->biasTee = v != 0;
        applied = ivars->biasTee ? 1 : 0;
        break;
    default:
        st = BRTL_ERR_ARGUMENT; /* inaccessible : validé plus haut */
        break;
    }

    const bool ok = st == BRTL_OK ||
                    (cmd->opcode == kBRTLOpSetFrequency && st == BRTL_ERR_PLL_UNLOCKED);
    if (ok) {
        ivars->commandsApplied++;
        ivars->lastCommandStatus = kBRTLClientOK;
        brtl_result(out, kBRTLClientOK, (int32_t)st, applied);
    } else {
        ivars->commandsFailed++;
        ivars->lastCommandStatus = kBRTLClientCommandFailed;
        BRTL_LOG("commande %s(%lld) : échec %s", brtl_opcode_name(cmd->opcode),
                 (long long)v, brtl_status_name(st));
        brtl_result(out, kBRTLClientCommandFailed, (int32_t)st, applied);
    }
    return kIOReturnSuccess;
}

kern_return_t BelksiriRtlSdrDriver::RtlGetDeviceInfo(OSObject *client, BRTLDeviceInfo *out)
{
    if (ivars == nullptr || out == nullptr) return kIOReturnBadArgument;
    BrtlLockGuard guard(ivars->lock);
    memset(out, 0, sizeof(*out));
    out->infoVersion = 1;
    /* Lecture libre : la première fait la détection du tuner si personne ne
     * l'a encore faite (aucun effet sur un flux, qui exige un propriétaire,
     * donc un contrôleur déjà ouvert). */
    RtlEnsureControllerLocked();
    out->tunerStatus = ivars->ctrl != nullptr ? 0 : ivars->tunerStatus;
    out->tunerType   = (uint32_t)ivars->devInfo.tuner_type;
    out->rtlXtalHz   = ivars->devInfo.rtl_xtal_hz;
    out->tunerXtalHz = ivars->devInfo.tuner_xtal_hz;
    if (ivars->ctrl != nullptr) {
        int gains[64] = {};
        const int n = brtl_ctrl_gains(ivars->ctrl, gains, 64);
        const uint32_t k = n < 0 ? 0u : ((uint32_t)n < BRTL_MAX_GAINS ? (uint32_t)n : BRTL_MAX_GAINS);
        for (uint32_t i = 0; i < k; i++) out->gainsTenthDb[i] = gains[i];
        out->gainCount = k;
    }
    out->ownerPresent  = ivars->own.owner != nullptr ? 1u : 0u;
    out->callerIsOwner = brtl_own_is_owner(&ivars->own, client) ? 1u : 0u;
    out->frequencyHz   = ivars->frequencyHz;
    out->sampleRateHz  = ivars->sampleRateHz;
    out->gainTenthDb   = ivars->appliedGainTenthDb;
    out->freqCorrectionPpm = ivars->ppm;
    uint32_t flags = 0;
    if (ivars->rtlAgc)       flags |= BRTL_FLAG_RTL_AGC;
    if (ivars->biasTee)      flags |= BRTL_FLAG_BIAS_TEE;
    if (ivars->offsetTuning) flags |= BRTL_FLAG_OFFSET_TUNING;
    if (ivars->manualGain)   flags |= BRTL_FLAG_MANUAL_GAIN;
    if (bhrf_session_state(&ivars->session) == kBHRFSessionReceiving) flags |= BRTL_FLAG_STREAMING;
    out->flags = flags;
    out->directSampling   = ivars->directSampling;
    out->commandsReceived = ivars->commandsReceived;
    out->commandsApplied  = ivars->commandsApplied;
    out->commandsRejected = ivars->commandsRejected;
    out->commandsFailed   = ivars->commandsFailed;
    out->rawControlDenied = ivars->rawControlDenied;
    out->busyRefusals     = ivars->own.busy_refusals;
    out->lastCommandStatus = ivars->lastCommandStatus;
    brtl_copy_name(out->tunerName, sizeof out->tunerName, ivars->devInfo.tuner_name);
    brtl_copy_name(out->manufacturer, sizeof out->manufacturer, ivars->devInfo.manufacturer);
    brtl_copy_name(out->product, sizeof out->product, ivars->devInfo.product);
    return kIOReturnSuccess;
}

kern_return_t BelksiriRtlSdrDriver::RtlOwnerStartRx(OSObject *client, const BHRFStartRxRequest *req,
                                                    BHRFSimpleStatus *outStatus)
{
    if (ivars == nullptr || req == nullptr || outStatus == nullptr) return kIOReturnBadArgument;
    BrtlLockGuard guard(ivars->lock);
    memset(outStatus, 0, sizeof(*outStatus));
    if (!brtl_own_is_owner(&ivars->own, client)) {
        outStatus->status = (int32_t)BRTL_BHRF_STATUS_NOT_OWNER;
        return kIOReturnSuccess;
    }
    if (RtlEnsureControllerLocked() != kIOReturnSuccess) {
        outStatus->status = kBHRFStatusNotOpen;
        return kIOReturnSuccess;
    }
    /* Vider le FIFO de l'endpoint avant les premiers transferts (rtlsdr). */
    if (bhrf_session_can_start(&ivars->session)) {
        brtl_ctrl_reset_stream(ivars->ctrl);
    }
    return RtlStartRx(req, outStatus);
}

kern_return_t BelksiriRtlSdrDriver::RtlOwnerStopRx(OSObject *client, BHRFSimpleStatus *outStatus)
{
    if (ivars == nullptr || outStatus == nullptr) return kIOReturnBadArgument;
    BrtlLockGuard guard(ivars->lock);
    memset(outStatus, 0, sizeof(*outStatus));
    if (!brtl_own_is_owner(&ivars->own, client)) {
        outStatus->status = (int32_t)BRTL_BHRF_STATUS_NOT_OWNER;
        return kIOReturnSuccess;
    }
    return RtlStopRx(outStatus);
}
