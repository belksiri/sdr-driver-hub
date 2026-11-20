/*
 * bhrf_policy.c — Implémentation de la liste blanche.
 *
 * Constantes et directions relevées dans
 *   android/native/third_party/hackrf/hackrf.c
 * (libhackrf v2024.02.1, commit 18b485e3b6d2031c15a79ba89cdb42b5fa245f24,
 *  BSD-3-Clause). Aucune valeur de ce fichier ne provient d'une autre source.
 */

#include "bhrf_policy.h"

/*
 * Les seules requêtes que le pilote laissera passer.
 *
 * Le critère d'inclusion est strict : la requête doit être NÉCESSAIRE à une
 * acquisition en réception, et non destructive. Tout ce qui touche à
 * l'émission, à la mémoire flash, au CPLD, aux registres des composants ou à
 * l'identité de l'appareil est absent — non pas filtré ailleurs, absent.
 *
 * Les longueurs maximales viennent des appels réels de libhackrf :
 *   SAMPLE_RATE_SET  → struct set_sample_rate_params, 8 octets
 *   SET_FREQ         → struct set_freq_params, 8 octets
 *   BOARD_ID_READ    → 1 octet
 *   SET_LNA/VGA_GAIN → 1 octet d'acquittement (direction IN, voir bhrf_policy.h)
 *   VERSION_STRING   → chaîne ; libhackrf accepte jusqu'à 255, nous plafonnons
 *                      à BHRF_CTRL_MAX_DATA (64), largement suffisant, et une
 *                      demande plus longue est refusée plutôt que tronquée.
 *   SUPPORTED_PLATFORM_READ → 4 octets
 */
const BHRFAllowedRequest kBHRFAllowedRequests[] = {
    /* bRequest                                 direction     maxLength */
    { BHRF_VR_SET_TRANSCEIVER_MODE,             kBHRFDirOut,  0  },
    { BHRF_VR_SAMPLE_RATE_SET,                  kBHRFDirOut,  8  },
    { BHRF_VR_BASEBAND_FILTER_BANDWIDTH_SET,    kBHRFDirOut,  0  },
    { BHRF_VR_BOARD_ID_READ,                    kBHRFDirIn,   1  },
    { BHRF_VR_VERSION_STRING_READ,              kBHRFDirIn,   BHRF_CTRL_MAX_DATA },
    { BHRF_VR_SET_FREQ,                         kBHRFDirOut,  8  },
    { BHRF_VR_AMP_ENABLE,                       kBHRFDirOut,  0  },
    { BHRF_VR_SET_LNA_GAIN,                     kBHRFDirIn,   1  },
    { BHRF_VR_SET_VGA_GAIN,                     kBHRFDirIn,   1  },
    { BHRF_VR_ANTENNA_ENABLE,                   kBHRFDirOut,  0  },
    { BHRF_VR_BOARD_REV_READ,                   kBHRFDirIn,   1  },
    { BHRF_VR_SUPPORTED_PLATFORM_READ,          kBHRFDirIn,   4  },
};

const unsigned kBHRFAllowedRequestCount =
    (unsigned)(sizeof(kBHRFAllowedRequests) / sizeof(kBHRFAllowedRequests[0]));

/*
 * Motif de refus « nommé » pour les requêtes interdites les plus significatives.
 *
 * Une liste blanche suffirait à toutes les bloquer via kBHRFDenyUnknownRequest.
 * Cette table existe pour une autre raison : rendre le refus LISIBLE. Dans le
 * journal du pilote et dans la vidéo destinée à Apple, « refusé : accès flash »
 * démontre une intention, là où « refusé : requête inconnue » ne démontre qu'une
 * omission. La table est donc redondante par conception, et c'est son utilité.
 */
static BHRFDenyReason named_denial_for(uint8_t bRequest)
{
    switch (bRequest) {
    case BHRF_VR_MAX2837_WRITE:
    case BHRF_VR_MAX2837_READ:
    case BHRF_VR_SI5351C_WRITE:
    case BHRF_VR_SI5351C_READ:
    case BHRF_VR_RFFC5071_WRITE:
    case BHRF_VR_RFFC5071_READ:
        return kBHRFDenyChipRegisterAccess;

    case BHRF_VR_SPIFLASH_ERASE:
    case BHRF_VR_SPIFLASH_WRITE:
    case BHRF_VR_SPIFLASH_READ:
    case BHRF_VR_SPIFLASH_STATUS:
    case BHRF_VR_SPIFLASH_CLEAR_STATUS:
    case BHRF_VR_CPLD_CHECKSUM:
        return kBHRFDenyFlashAccess;

    case BHRF_VR_SET_TXVGA_GAIN:
        return kBHRFDenyTxGain;

    case BHRF_VR_RESET:
        return kBHRFDenyDeviceReset;

    case BHRF_VR_BOARD_PARTID_SERIALNO_READ:
        return kBHRFDenyPrivacy;

    default:
        return kBHRFDenyUnknownRequest;
    }
}

BHRFDenyReason bhrf_policy_evaluate(const BHRFControlRequest *req)
{
    unsigned i;
    const BHRFAllowedRequest *entry = 0;

    if (req == 0) {
        return kBHRFDenyUnknownRequest;
    }

    /* 1. Le type de requête. Seuls VENDOR/DEVICE dans les deux sens existent
     *    dans le protocole HackRF. Une requête standard, de classe, ou visant
     *    une interface ou un endpoint n'a aucune raison d'exister ici : la
     *    refuser ferme d'un coup toute une famille d'abus. */
    if (req->bmRequestType != BHRF_BMREQ_OUT_VENDOR_DEVICE &&
        req->bmRequestType != BHRF_BMREQ_IN_VENDOR_DEVICE) {
        return kBHRFDenyNotVendorRequest;
    }

    /* 2. La taille, avant toute autre chose : une longueur hors borne ne doit
     *    jamais atteindre le code qui copie des octets. */
    if (req->wLength > BHRF_CTRL_MAX_DATA) {
        return kBHRFDenyLengthTooLarge;
    }

    /* 3. Le mode transceiver, traité AVANT la liste blanche générale.
     *    SET_TRANSCEIVER_MODE est autorisé, mais seules deux de ses six valeurs
     *    le sont. C'est le seul endroit du pilote où wValue porte une décision
     *    de sécurité, et c'est aussi la barrière qui bloque TRANSMIT et le
     *    flashage du CPLD (lequel n'a pas de requête vendor dédiée : il passe
     *    par ce mode puis par du bulk OUT — voir hackrf.c:1313). */
    if (req->bRequest == BHRF_VR_SET_TRANSCEIVER_MODE) {
        switch (req->wValue) {
        case BHRF_TRANSCEIVER_MODE_OFF:
        case BHRF_TRANSCEIVER_MODE_RECEIVE:
            break; /* les deux seuls modes autorisés */
        case BHRF_TRANSCEIVER_MODE_TRANSMIT:
            return kBHRFDenyTransmitMode;
        case BHRF_TRANSCEIVER_MODE_CPLD_UPDATE:
            return kBHRFDenyCpldUpdateMode;
        default:
            /* SS (3), RX_SWEEP (5) et toute valeur future inconnue. RX_SWEEP
             * n'est pas dangereux en soi, mais il exige INIT_SWEEP et un chemin
             * de données différent : l'autoriser plus tard sera un élargissement
             * explicite, jamais un effet de bord. */
            return kBHRFDenyUnsupportedMode;
        }
    }

    /* 4. La liste blanche proprement dite. */
    for (i = 0; i < kBHRFAllowedRequestCount; i++) {
        if (kBHRFAllowedRequests[i].bRequest == req->bRequest) {
            entry = &kBHRFAllowedRequests[i];
            break;
        }
    }
    if (entry == 0) {
        return named_denial_for(req->bRequest);
    }

    /* 5. La direction doit être exactement celle que le protocole prévoit.
     *    Sans ce contrôle, une requête autorisée pourrait servir de véhicule à
     *    une écriture là où le protocole n'attend qu'une lecture. */
    {
        const uint8_t expected = (entry->direction == kBHRFDirIn)
                                     ? (uint8_t)BHRF_BMREQ_IN_VENDOR_DEVICE
                                     : (uint8_t)BHRF_BMREQ_OUT_VENDOR_DEVICE;
        if (req->bmRequestType != expected) {
            return kBHRFDenyWriteNotAllowed;
        }
    }

    /* 6. La longueur propre à la requête. */
    if (req->wLength > entry->maxLength) {
        return kBHRFDenyLengthTooLarge;
    }

    return kBHRFDenyNone;
}

const char *bhrf_deny_reason_name(BHRFDenyReason reason)
{
    switch (reason) {
    case kBHRFDenyNone:                return "none";
    case kBHRFDenyUnknownRequest:      return "unknown_request";
    case kBHRFDenyTransmitMode:        return "transmit_mode";
    case kBHRFDenyCpldUpdateMode:      return "cpld_update_mode";
    case kBHRFDenyUnsupportedMode:     return "unsupported_mode";
    case kBHRFDenyFlashAccess:         return "flash_access";
    case kBHRFDenyTxGain:              return "tx_gain";
    case kBHRFDenyChipRegisterAccess:  return "chip_register_access";
    case kBHRFDenyDeviceReset:         return "device_reset";
    case kBHRFDenyPrivacy:             return "privacy";
    case kBHRFDenyNotVendorRequest:    return "not_vendor_request";
    case kBHRFDenyLengthTooLarge:      return "length_too_large";
    case kBHRFDenyWriteNotAllowed:     return "write_not_allowed";
    }
    return "unspecified";
}

const char *bhrf_status_name(BHRFStatus status)
{
    switch (status) {
    case kBHRFStatusOK:                  return "ok";
    case kBHRFStatusDenied:              return "denied";
    case kBHRFStatusBadArgument:         return "bad_argument";
    case kBHRFStatusNoDevice:            return "no_device";
    case kBHRFStatusNotOpen:             return "not_open";
    case kBHRFStatusUsbError:            return "usb_error";
    case kBHRFStatusAlreadyStreaming:    return "already_streaming";
    case kBHRFStatusNotStreaming:        return "not_streaming";
    case kBHRFStatusNoMemory:            return "no_memory";
    case kBHRFStatusTerminated:          return "terminated";
    case kBHRFStatusUnsupportedSelector: return "unsupported_selector";
    case kBHRFStatusProtocolMismatch:    return "protocol_mismatch";
    }
    return "unspecified";
}
