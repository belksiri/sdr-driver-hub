#include "bhrf_diagnostics.h"
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
        return "Le peripherique a renvoye un STALL : il ne connait pas cette requete, "
               "ou en refuse les parametres.";
    case kBHRFUsbNoDevice:
        return "Le peripherique n'est plus la. Rebranchez-le.";
    case kBHRFUsbNotResponding:
        return "Le peripherique ne repond plus. Cable de donnees ou alimentation "
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

const char *bhrf_status_name(int32_t status)
{
    switch (status) {
    case kBHRFStatusOK:                  return "ok";
    case kBHRFStatusDenied:              return "refuse";
    case kBHRFStatusBadArgument:         return "argument_invalide";
    case kBHRFStatusNoDevice:            return "peripherique_absent";
    case kBHRFStatusNotOpen:             return "non_ouvert";
    case kBHRFStatusUsbError:            return "erreur_usb";
    case kBHRFStatusAlreadyStreaming:    return "deja_en_reception";
    case kBHRFStatusNotStreaming:        return "pas_en_reception";
    case kBHRFStatusNoMemory:            return "memoire_insuffisante";
    case kBHRFStatusTerminated:          return "termine";
    case kBHRFStatusUnsupportedSelector: return "selecteur_inconnu";
    case kBHRFStatusProtocolMismatch:    return "protocole_incompatible";
    default:                             return "inconnu";
    }
}
