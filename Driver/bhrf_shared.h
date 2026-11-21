/*
 * bhrf_shared.h — Contrat partagé entre le pilote DriverKit et l'application
 * SDR Driver Hub (préfixe historique « bhrf » : Belksiri hardware RF).
 *
 * Protocole de user client GÉNÉRIQUE, indépendant du matériel : identité du
 * pilote, requête de contrôle filtrée par la liste blanche PROPRE à chaque
 * pilote, ring partagé de blocs IQ, démarrage et arrêt de la réception. Aucune
 * valeur propre à un équipement ici : le pilote RTL-SDR porte les siennes
 * (brtl_identity.h, brtl_policy.h).
 *
 * Ce fichier est compilé dans TROIS contextes différents, et c'est la raison de
 * toutes ses contraintes de style :
 *   1. le dext DriverKit (C++ gnu++20, SDK driverkit) ;
 *   2. les applications macOS et iPadOS (Swift, via un bridging header) ;
 *   3. les tests C hôtes (clang C11, sans matériel).
 * Il reste donc du C pur : aucun type C++, aucun champ de bits, uniquement des
 * entiers de largeur fixe, et un empaquetage explicite. Une divergence de
 * disposition entre le pilote et l'application se traduirait par des octets lus
 * de travers à travers la frontière IOUserClient — un bug muet et coûteux.
 *
 * Versionnage : le pilote ne se recharge qu'au DÉBRANCHEMENT du périphérique
 * (sur iPadOS c'est une règle du système, pas un choix). L'application peut donc
 * parfaitement dialoguer avec un pilote plus ancien qu'elle. `kSelProtocolVersion`
 * est pour cette raison le PREMIER appel obligatoire : l'application refuse de
 * streamer face à une version qu'elle ne connaît pas, plutôt que d'interpréter
 * des structures dont la disposition a changé.
 */

#ifndef BHRF_SHARED_H
#define BHRF_SHARED_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ───────────────────────── Version du protocole ─────────────────────────── */

/* Incrémenter à CHAQUE changement de disposition d'une structure de ce fichier
 * ou de la sémantique d'un sélecteur. Jamais pour un changement interne au
 * pilote qui ne traverse pas la frontière. */
#define BHRF_PROTOCOL_VERSION 1u

/* 'B','H','R','F' relus comme un uint32 little-endian. */
#define BHRF_RING_MAGIC 0x46524842u

/* ─────────────────────────── Sélecteurs user client ─────────────────────── */

enum {
    /* Premier appel obligatoire. Renvoie aussi l'identité USB (bcdDevice,
     * idVendor, idProduct), lue dans le DESCRIPTEUR de périphérique, auquel une
     * app iPadOS n'a pas accès : c'est donc au pilote de la fournir. */
    kBHRFSelProtocolVersion = 0,

    /* Requête de contrôle vendor, filtrée par la liste blanche. */
    kBHRFSelControlRequest = 1,

    /* Statistiques du ring et état du périphérique. Disponible même hors RX. */
    kBHRFSelRingStats = 2,

    /* Étape 5 — allocation du ring ; l'application appelle ensuite
     * IOConnectMapMemory64 avec kBHRFMemoryTypeRing. */
    kBHRFSelMapRing = 3,

    kBHRFSelStartRx = 4,
    kBHRFSelStopRx  = 5,

    kBHRFSelCount
};

/* Type mémoire passé à IOConnectMapMemory64. */
enum { kBHRFMemoryTypeRing = 0 };

/* ──────────────────────────── Codes de retour ───────────────────────────── */

typedef enum {
    kBHRFStatusOK = 0,

    /* La requête a été REFUSÉE par la liste blanche. Aucun octet n'a atteint le
     * bus USB. `denyReason` précise laquelle des règles a joué. */
    kBHRFStatusDenied = 1,

    kBHRFStatusBadArgument     = 2,
    kBHRFStatusNoDevice        = 3,
    kBHRFStatusNotOpen         = 4,
    kBHRFStatusUsbError        = 5,
    kBHRFStatusAlreadyStreaming = 6,
    kBHRFStatusNotStreaming    = 7,
    kBHRFStatusNoMemory        = 8,
    kBHRFStatusTerminated      = 9,
    kBHRFStatusUnsupportedSelector = 10,
    kBHRFStatusProtocolMismatch    = 11
} BHRFStatus;

/* Motif exact d'un refus : `BHRFControlResponse.denyReason` porte la valeur
 * de l'énumération PROPRE au pilote qui a refusé (pilote RTL-SDR :
 * BRTLDenyReason, brtl_policy.h). 0 signifie toujours « aucun refus ». */
#define BHRF_DENY_NONE 0u

/* État du périphérique, tel que publié par le pilote. */
typedef enum {
    kBHRFDeviceStateIdle       = 0,
    kBHRFDeviceStateReceiving  = 1,
    kBHRFDeviceStateTerminated = 2,
    kBHRFDeviceStateError      = 3
} BHRFDeviceState;

/* ───────────────────────────── Structures ───────────────────────────────── */

/* Charge utile maximale d'une requête de contrôle : 64 octets couvrent toutes
 * les requêtes des listes blanches. Une requête qui demanderait davantage est
 * refusée par le pilote plutôt que tronquée silencieusement. */
#define BHRF_CTRL_MAX_DATA 64u

#pragma pack(push, 1)

typedef struct BHRFProtocolInfo {
    uint32_t protocolVersion; /* BHRF_PROTOCOL_VERSION du pilote               */
    uint32_t driverBuildId;   /* horodatage de build, pour distinguer deux dext */
    uint16_t bcdDevice;       /* version d'API USB, lue dans le descripteur     */
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t deviceState;     /* BHRFDeviceState                                */
    uint32_t capabilities;    /* réservé — 0 pour l'instant                     */
} BHRFProtocolInfo;

typedef struct BHRFControlRequest {
    uint8_t  bmRequestType;
    uint8_t  bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
    uint8_t  data[BHRF_CTRL_MAX_DATA]; /* phase données sortante, si applicable */
} BHRFControlRequest;

typedef struct BHRFControlResponse {
    int32_t  status;        /* BHRFStatus                                       */
    int32_t  ioReturn;      /* kern_return_t de la couche USB.                  */
                            /* TOUJOURS 0 sur un refus : la preuve qu'aucun     */
                            /* transfert n'a été tenté.                         */
    uint32_t denyReason;    /* motif propre au pilote (BRTLDenyReason…)         */
    uint16_t actualLength;
    uint16_t reserved0;
    uint8_t  data[BHRF_CTRL_MAX_DATA];
} BHRFControlResponse;

/* Statistiques lisibles à tout moment, y compris hors RX. Chaque compteur
 * appartient à UN étage du pipeline et ne doit jamais être additionné à un
 * autre : `overflowBlocks` dit que l'application n'a pas drainé assez vite,
 * `usbErrors` dit que le bus a fauté. Ce sont deux problèmes différents. */
typedef struct BHRFRingStats {
    uint64_t writeIndex;      /* index d'écriture publié par le pilote          */
    uint64_t blocksProduced;
    uint64_t bytesProduced;
    uint64_t overflowBlocks;  /* blocs écrasés faute de consommateur assez vif  */
    uint64_t usbErrors;       /* complétions AsyncIO en erreur                  */
    uint64_t lastCompletionNs;
    uint32_t deviceState;     /* BHRFDeviceState                                */
    uint32_t lastUsbStatus;   /* IOReturn de la dernière complétion             */
    uint32_t blockCount;      /* géométrie du ring, 0 si non alloué             */
    uint32_t blockBytes;
    uint32_t inFlight;        /* transferts AsyncIO actuellement soumis         */
    uint32_t reserved0;
} BHRFRingStats;

typedef struct BHRFStartRxRequest {
    uint32_t buffersInFlight; /* 8 ou 16 ; borné par le pilote                  */
    uint32_t bufferBytes;     /* 256 KiB par défaut                             */
    uint32_t reserved0;
    uint32_t reserved1;
} BHRFStartRxRequest;

typedef struct BHRFSimpleStatus {
    int32_t  status;
    int32_t  ioReturn;
    uint32_t blockCount;
    uint32_t blockBytes;
} BHRFSimpleStatus;

/* ─────────────────────── Disposition du ring partagé ────────────────────── */
/* Publié par le pilote, lu par l'application, dans une IOBufferMemoryDescriptor
 * mappée par IOConnectMapMemory64.
 *
 * Discipline de publication, côté pilote, dans cet ordre strict :
 *   1. copier la charge utile dans le slot ;
 *   2. écrire slots[i].validBytes et slots[i].captureNs ;
 *   3. publier writeIndex en sémantique RELEASE.
 * Côté application :
 *   1. lire writeIndex en sémantique ACQUIRE ;
 *   2. lire la charge utile.
 * L'application ne détient AUCUN curseur dans cette structure : son index de
 * lecture lui est local. Le pilote n'a donc jamais à faire confiance à une
 * valeur écrite par l'espace utilisateur. */

#define BHRF_RING_HEADER_BYTES 4096u
#define BHRF_RING_BLOCK_BYTES  262144u /* 256 KiB */
#define BHRF_RING_BLOCK_COUNT  64u     /* 16 Mio = 0,4 s d'absorption à 40 Mo/s */
#define BHRF_RING_MAX_SLOTS    128u    /* borne de la table, pour figer la taille */

typedef struct BHRFRingSlot {
    uint32_t validBytes;
    uint32_t reserved0;
    uint64_t captureNs;
} BHRFRingSlot;

typedef struct BHRFRingHeader {
    uint32_t magic;         /* BHRF_RING_MAGIC                                 */
    uint32_t layoutVersion; /* = BHRF_PROTOCOL_VERSION                         */
    uint32_t blockCount;
    uint32_t blockBytes;
    uint32_t headerBytes;   /* décalage du premier bloc de données             */
    uint32_t reserved0;

    /* Champs publiés par le pilote. Déclarés en uint64_t plutôt qu'en _Atomic :
     * _Atomic ne traverse pas proprement les trois contextes de compilation de
     * ce fichier (notamment l'import Swift). Les accès atomiques se font aux
     * points d'usage, via les intrinsèques de chaque côté, sur ces adresses. */
    uint64_t writeIndex;
    uint64_t blocksProduced;
    uint64_t bytesProduced;
    uint64_t overflowBlocks;
    uint64_t usbErrors;
    uint64_t lastCompletionNs;

    uint32_t deviceState;
    uint32_t lastUsbStatus;

    BHRFRingSlot slots[BHRF_RING_MAX_SLOTS];
} BHRFRingHeader;

#pragma pack(pop)

/* L'en-tête DOIT tenir dans la zone réservée : le premier bloc de données
 * commence à BHRF_RING_HEADER_BYTES. Une structure qui déborderait serait
 * écrasée par les premiers octets d'IQ — un bug silencieux et catastrophique.
 * Vérifié à la compilation, dans les trois contextes. */
/* writeIndex est lu et écrit ATOMIQUEMENT, sur 64 bits, des deux côtés de la
 * frontière. Or la structure est `pack(1)` — nécessaire pour que sa disposition
 * soit identique dans les trois contextes de compilation — et le compilateur en
 * déduit un alignement de 1 octet. Un store atomique 64 bits sur une adresse
 * non alignée n'est PAS garanti atomique sur arm64 : il peut être découpé, et
 * l'application lirait alors un writeIndex à moitié mis à jour, donc un bloc
 * qui n'existe pas encore.
 *
 * L'en-tête étant alloué aligné sur 4096, il suffit que le DÉCALAGE du champ
 * soit multiple de 8 pour que l'adresse le soit. Ce static_assert en fait une
 * garantie du contrat plutôt qu'une coïncidence de disposition : ajouter un
 * champ de 4 octets avant writeIndex casserait la compilation, et non le
 * comportement en production. */
#define BHRF_ASSERT_ALIGNED8(field) \
    ((offsetof(BHRFRingHeader, field) % 8u) == 0u)

#if defined(__cplusplus)
static_assert(sizeof(BHRFRingHeader) <= BHRF_RING_HEADER_BYTES,
              "BHRFRingHeader deborde la zone d'en-tete du ring");
static_assert(BHRF_RING_BLOCK_COUNT <= BHRF_RING_MAX_SLOTS,
              "BHRF_RING_BLOCK_COUNT depasse la table de slots");
static_assert(BHRF_ASSERT_ALIGNED8(writeIndex),
              "writeIndex doit etre a un decalage multiple de 8");
static_assert(BHRF_ASSERT_ALIGNED8(blocksProduced),
              "blocksProduced doit etre a un decalage multiple de 8");
static_assert(BHRF_ASSERT_ALIGNED8(bytesProduced),
              "bytesProduced doit etre a un decalage multiple de 8");
static_assert((BHRF_RING_HEADER_BYTES % 8u) == 0u,
              "l'en-tete doit preserver l'alignement des blocs de donnees");
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Static_assert(sizeof(BHRFRingHeader) <= BHRF_RING_HEADER_BYTES,
               "BHRFRingHeader deborde la zone d'en-tete du ring");
_Static_assert(BHRF_RING_BLOCK_COUNT <= BHRF_RING_MAX_SLOTS,
               "BHRF_RING_BLOCK_COUNT depasse la table de slots");
_Static_assert(BHRF_ASSERT_ALIGNED8(writeIndex),
               "writeIndex doit etre a un decalage multiple de 8");
_Static_assert(BHRF_ASSERT_ALIGNED8(blocksProduced),
               "blocksProduced doit etre a un decalage multiple de 8");
_Static_assert(BHRF_ASSERT_ALIGNED8(bytesProduced),
               "bytesProduced doit etre a un decalage multiple de 8");
_Static_assert((BHRF_RING_HEADER_BYTES % 8u) == 0u,
               "l'en-tete doit preserver l'alignement des blocs de donnees");
#endif

#ifdef __cplusplus
}
#endif

#endif /* BHRF_SHARED_H */
