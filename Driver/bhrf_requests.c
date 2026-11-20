#include "bhrf_requests.h"
#include "bhrf_shared.h"

#include <stdio.h>

/* Valeurs d'IOReturn, recopiées plutôt qu'incluses : ce fichier est compilé
 * dans le dext, dans les applications et dans les tests hôtes, et IOReturn.h
 * n'est pas disponible partout. Calculées depuis IOReturn.h du SDK DriverKit :
 *   iokit_common_err(x) = sys_iokit | sub_iokit_common | x = 0xe0000000 | x
 *   kUSBHostReturnPipeStalled est donné en clair dans
 *   USBDriverKit/IOUSBHostFamilyDefinitions.h. */
#define BHRF_IORETURN_NO_DEVICE        ((int32_t)0xe00002c0)
#define BHRF_IORETURN_BAD_ARGUMENT     ((int32_t)0xe00002c2)
#define BHRF_IORETURN_TIMEOUT          ((int32_t)0xe00002d6)
#define BHRF_IORETURN_UNDERRUN         ((int32_t)0xe00002e7)
#define BHRF_IORETURN_OVERRUN          ((int32_t)0xe00002e8)
#define BHRF_IORETURN_ABORTED          ((int32_t)0xe00002eb)
#define BHRF_IORETURN_NOT_RESPONDING   ((int32_t)0xe00002ed)
#define BHRF_USBHOST_PIPE_STALLED      ((int32_t)0xe0005000)
/* Forme héritée du même STALL, encore rencontrée selon les couches. */
#define BHRF_USB_LEGACY_PIPE_STALLED   ((int32_t)0xe000404f)

struct bhrf_request_entry {
    uint8_t     b_request;
    const char *name;
    uint16_t    min_usb_api;   /* 0 : aucune exigence */
};

static const struct bhrf_request_entry kRequests[] = {
    { BHRF_VR_SET_TRANSCEIVER_MODE,          "SET_TRANSCEIVER_MODE",          0      },
    { BHRF_VR_MAX2837_WRITE,                 "MAX2837_WRITE",                 0      },
    { BHRF_VR_MAX2837_READ,                  "MAX2837_READ",                  0      },
    { BHRF_VR_SI5351C_WRITE,                 "SI5351C_WRITE",                 0      },
    { BHRF_VR_SI5351C_READ,                  "SI5351C_READ",                  0      },
    { BHRF_VR_SAMPLE_RATE_SET,               "SAMPLE_RATE_SET",               0      },
    { BHRF_VR_BASEBAND_FILTER_BANDWIDTH_SET, "BASEBAND_FILTER_BANDWIDTH_SET", 0      },
    { BHRF_VR_RFFC5071_WRITE,                "RFFC5071_WRITE",                0      },
    { BHRF_VR_RFFC5071_READ,                 "RFFC5071_READ",                 0      },
    { BHRF_VR_SPIFLASH_ERASE,                "SPIFLASH_ERASE",                0      },
    { BHRF_VR_SPIFLASH_WRITE,                "SPIFLASH_WRITE",                0      },
    { BHRF_VR_SPIFLASH_READ,                 "SPIFLASH_READ",                 0      },
    { BHRF_VR_BOARD_ID_READ,                 "BOARD_ID_READ",                 0      },
    { BHRF_VR_VERSION_STRING_READ,           "VERSION_STRING_READ",           0      },
    { BHRF_VR_SET_FREQ,                      "SET_FREQ",                      0      },
    { BHRF_VR_AMP_ENABLE,                    "AMP_ENABLE",                    0      },
    { BHRF_VR_BOARD_PARTID_SERIALNO_READ,    "BOARD_PARTID_SERIALNO_READ",    0      },
    { BHRF_VR_SET_LNA_GAIN,                  "SET_LNA_GAIN",                  0      },
    { BHRF_VR_SET_VGA_GAIN,                  "SET_VGA_GAIN",                  0      },
    { BHRF_VR_SET_TXVGA_GAIN,                "SET_TXVGA_GAIN",                0      },
    { BHRF_VR_ANTENNA_ENABLE,                "ANTENNA_ENABLE",                0      },
    { BHRF_VR_SET_FREQ_EXPLICIT,             "SET_FREQ_EXPLICIT",             0      },
    { BHRF_VR_RESET,                         "RESET",                         0x0102 },
    { BHRF_VR_SPIFLASH_STATUS,               "SPIFLASH_STATUS",               0x0103 },
    { BHRF_VR_SPIFLASH_CLEAR_STATUS,         "SPIFLASH_CLEAR_STATUS",         0x0103 },
    { BHRF_VR_CPLD_CHECKSUM,                 "CPLD_CHECKSUM",                 0x0103 },
    { BHRF_VR_BOARD_REV_READ,                "BOARD_REV_READ",                0x0106 },
    { BHRF_VR_SUPPORTED_PLATFORM_READ,       "SUPPORTED_PLATFORM_READ",       0x0106 },
    { BHRF_VR_SET_LEDS,                      "SET_LEDS",                      0x0107 },
    { BHRF_VR_SET_USER_BIAS_T_OPTS,          "SET_USER_BIAS_T_OPTS",          0x0108 }
};

static const unsigned kRequestCount =
    (unsigned)(sizeof(kRequests) / sizeof(kRequests[0]));

static const struct bhrf_request_entry *lookup(uint8_t b_request)
{
    unsigned i;
    for (i = 0; i < kRequestCount; i++) {
        if (kRequests[i].b_request == b_request) {
            return &kRequests[i];
        }
    }
    return 0;
}

const char *bhrf_request_name(uint8_t b_request)
{
    static char unknown[24];
    const struct bhrf_request_entry *e = lookup(b_request);
    if (e != 0) {
        return e->name;
    }
    snprintf(unknown, sizeof(unknown), "INCONNU(%u)", (unsigned)b_request);
    return unknown;
}

uint16_t bhrf_min_usb_api(uint8_t b_request)
{
    const struct bhrf_request_entry *e = lookup(b_request);
    return (e != 0) ? e->min_usb_api : 0u;
}

int bhrf_request_supported_by(uint8_t b_request, uint16_t bcd_device)
{
    const uint16_t required = bhrf_min_usb_api(b_request);
    if (required == 0u) {
        return 1;
    }
    /* bcdDevice nul : version inconnue. Ne PAS conclure à l'incompatibilité —
     * on ne bloquerait alors rien de plus qu'on ne le casserait. */
    if (bcd_device == 0u) {
        return 1;
    }
    return bcd_device >= required;
}

BHRFUsbFailureKind bhrf_classify_usb_failure(int32_t io_return)
{
    if (io_return == 0) {
        return kBHRFUsbOk;
    }
    switch (io_return) {
    case BHRF_USBHOST_PIPE_STALLED:
    case BHRF_USB_LEGACY_PIPE_STALLED: return kBHRFUsbStalled;
    case BHRF_IORETURN_NO_DEVICE:      return kBHRFUsbNoDevice;
    case BHRF_IORETURN_NOT_RESPONDING: return kBHRFUsbNotResponding;
    case BHRF_IORETURN_TIMEOUT:        return kBHRFUsbTimeout;
    case BHRF_IORETURN_UNDERRUN:       return kBHRFUsbUnderrun;
    case BHRF_IORETURN_OVERRUN:        return kBHRFUsbOverrun;
    case BHRF_IORETURN_ABORTED:        return kBHRFUsbAborted;
    case BHRF_IORETURN_BAD_ARGUMENT:   return kBHRFUsbBadArgument;
    default:                           return kBHRFUsbOther;
    }
}

const char *bhrf_usb_failure_kind_name(BHRFUsbFailureKind kind)
{
    switch (kind) {
    case kBHRFUsbOk:            return "ok";
    case kBHRFUsbStalled:       return "stall";
    case kBHRFUsbNoDevice:      return "peripherique_absent";
    case kBHRFUsbNotResponding: return "pas_de_reponse";
    case kBHRFUsbTimeout:       return "delai_depasse";
    case kBHRFUsbUnderrun:      return "underrun";
    case kBHRFUsbOverrun:       return "overrun";
    case kBHRFUsbAborted:       return "annule";
    case kBHRFUsbBadArgument:   return "argument_invalide";
    default:                    return "autre";
    }
}

const char *bhrf_usb_failure_kind_advice(BHRFUsbFailureKind kind)
{
    switch (kind) {
    case kBHRFUsbOk:
        return "Aucune erreur.";
    case kBHRFUsbStalled:
        return "Le HackRF a renvoye un STALL : son firmware ne connait pas cette "
               "requete, ou en refuse les parametres. Verifier la version d'API USB.";
    case kBHRFUsbNoDevice:
        return "Le HackRF n'est plus la. Rebranchez-le.";
    case kBHRFUsbNotResponding:
        return "Le HackRF ne repond plus. Cable de donnees ou alimentation "
               "insuffisante : essayer un hub alimente.";
    case kBHRFUsbTimeout:
        return "Delai depasse. Lien USB en cause, ou appareil occupe.";
    case kBHRFUsbUnderrun:
        return "Moins d'octets transferes que demande : longueur de requete "
               "probablement incorrecte.";
    case kBHRFUsbOverrun:
        return "Plus d'octets que demande : longueur de requete incorrecte.";
    case kBHRFUsbAborted:
        return "Transfert annule, generalement par un arret en cours.";
    case kBHRFUsbBadArgument:
        return "La couche USB a refuse les parametres : direction ou longueur "
               "incoherente avec le type de requete.";
    default:
        return "Erreur USB non classee. Relever le code exact.";
    }
}
