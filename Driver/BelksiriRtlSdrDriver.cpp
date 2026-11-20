/*
 * BelksiriRtlSdrDriver.cpp — Pilote DriverKit générique pour RTL-SDR (RTL2832U).
 *
 * EXPERIMENTAL — DEVELOPMENT ONLY. NOT APPROVED FOR APP STORE DISTRIBUTION.
 *
 * Couche A du prototype : notre code, sans aucun algorithme RTL2832U/R82xx.
 * Il ouvre l'appareil, applique la liste blanche (brtl_policy) à chaque
 * requête de contrôle, et dépose le flux bulk IN dans le ring partagé, avec
 * le même protocole de user client que BelksiriHackRFDriver (bhrf_shared.h).
 * La programmation du démodulateur et du tuner se fait dans l'application,
 * par librtlsdr (GPL), à travers ce pilote.
 *
 * Dérivé de BelksiriHackRFDriver.cpp (notre code) : même séquence de
 * démarrage, même ring zéro-copie, même gestion Stop / Abort / ClearStall.
 * Le binaire est distinct : aucun code n'est partagé à l'exécution avec le
 * pilote HackRF.
 */

#include <os/log.h>

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
#include "bhrf_requests.h"
#include "bhrf_session.h"
#include "brtl_identity.h"
#include "brtl_policy.h"

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

/* Endpoint bulk IN des échantillons du RTL2832U. */
#define BRTL_BULK_IN_ENDPOINT 0x81u

/* Délai maximal d'un transfert bulk IN.
 *
 * Zéro — « sans délai » — a été essayé et retiré : quand le HackRF repasse en
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
};

bool BelksiriRtlSdrDriver::init(void)
{
    if (!super::init()) {
        return false;
    }
    ivars = IONewZero(BelksiriRtlSdrDriver_IVars, 1);
    if (ivars == nullptr) {
        return false;
    }
    bhrf_session_init(&ivars->session);
    return true;
}

void BelksiriRtlSdrDriver::free(void)
{
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

    BRTL_LOG("Start: protocole v%u, build %u", BHRF_PROTOCOL_VERSION,
             (unsigned)BRTL_BUILD_ID);

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
        /* Ordre obligatoire : arrêter les transferts AVANT de relâcher le pipe
         * et l'interface. Une complétion qui arriverait après la libération du
         * ring écrirait dans de la mémoire rendue. */
        BHRFSimpleStatus st = {};
        RtlStopRx(&st);

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
    outInfo->protocolVersion = BHRF_PROTOCOL_VERSION;
    outInfo->driverBuildId   = (uint32_t)BRTL_BUILD_ID;
    outInfo->bcdDevice       = ivars->bcdDevice;
    outInfo->idVendor        = ivars->idVendor;
    outInfo->idProduct       = ivars->idProduct;
    outInfo->deviceState     = (uint16_t)bhrf_session_device_state(&ivars->session);
    outInfo->capabilities    = 0;
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
     * qu'on remet le HackRF en mode OFF juste après avoir abandonné les
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
    ivars->ringHeader->layoutVersion = BHRF_PROTOCOL_VERSION;
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
         * jamais lieu : les transferts restaient en vol, le HackRF passait
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
         * `withRequest = false` : c'est nous qui avons abandonné, le HackRF n'a
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
     * n'était JAMAIS révoqué. Il fallait débrancher le HackRF. */
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
