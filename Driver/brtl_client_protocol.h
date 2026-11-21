/*
 * brtl_client_protocol.h — Protocole client du pilote RTL-SDR de SDR Driver Hub
 * (BelksiriRtlSdrDriver), version 2 : accès DIRECT d'une application cliente.
 *
 * Copyright (C) 2026 Belksiri.
 *
 * En-tête d'INTERFACE seulement : constantes et structures échangées à travers
 * le user client. Il ne contient aucun code ni aucune donnée de librtlsdr, et
 * c'est ce qui permet à Spectrum Analyzer One d'en garder une copie identique
 * (ios/Runner/RtlSdr/brtl_client_protocol.h, vérifiée par test) sans embarquer
 * le moindre code RTL-SDR.
 *
 * Ce qui change par rapport à la version 1 :
 *   • le pilote règle lui-même le RTL2832U et le tuner (librtlsdr compilée
 *     dans le dext) ; le client envoie des COMMANDES de haut niveau ;
 *   • les requêtes USB brutes (kBHRFSelControlRequest) sont refusées à tout
 *     client : seul le pilote parle au bus, par sa liste blanche ;
 *   • un seul client « propriétaire » à la fois (Acquire / Release) ; les
 *     autres ne peuvent que lire l'identité, l'état et les compteurs ;
 *   • la disparition du propriétaire (fermeture, plantage, fin du processus)
 *     arrête la réception dans le pilote lui-même.
 *
 * Ce qui NE change pas : sélecteurs 0 à 5 et structures de bhrf_shared.h
 * (identité, compteurs, ring, démarrage, arrêt), disposition du ring
 * (layoutVersion 1), ring mappé en lecture seule.
 *
 * Contraintes de style : celles de bhrf_shared.h (C pur, largeurs fixes,
 * pack(1)) — l'en-tête est compilé dans le dext, dans deux applications et
 * dans les tests hôtes.
 */

#ifndef BRTL_CLIENT_PROTOCOL_H
#define BRTL_CLIENT_PROTOCOL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Valeur de BHRFProtocolInfo.protocolVersion renvoyée par le pilote RTL-SDR. */
#define BRTL_CLIENT_PROTOCOL_VERSION 2u

/* Disposition du ring : inchangée depuis la version 1. */
#define BRTL_RING_LAYOUT_VERSION 1u

/* Bits de BHRFProtocolInfo.capabilities. */
#define BRTL_CAP_TUNER_CONTROL  (1u << 0)  /* commandes de haut niveau     */
#define BRTL_CAP_DEVICE_INFO    (1u << 1)  /* sélecteur kBRTLSelDeviceInfo  */
#define BRTL_CAP_OWNERSHIP      (1u << 2)  /* Acquire / Release             */

/* Sélecteurs ajoutés (0 à 5 : bhrf_shared.h). */
enum {
    kBRTLSelAcquire    = 6,
    kBRTLSelRelease    = 7,
    kBRTLSelCommand    = 8,
    kBRTLSelDeviceInfo = 9,
    kBRTLSelCount
};

/* Statut des sélecteurs du protocole client (champ `status`). */
typedef enum {
    kBRTLClientOK               = 0,
    kBRTLClientBusy             = 1, /* un autre client possède la radio        */
    kBRTLClientNotOwner         = 2, /* opération réservée au propriétaire      */
    kBRTLClientTunerUnavailable = 3, /* tuner absent, non pris en charge, ou    */
                                     /* initialisation en échec                 */
    kBRTLClientBadArgument      = 4, /* valeur hors bornes : RIEN n'est émis    */
    kBRTLClientCommandFailed    = 5, /* le contrôleur a échoué (controllerStatus)*/
    kBRTLClientTerminated       = 6, /* le périphérique a disparu               */
    kBRTLClientUnknownOpcode    = 7,
    kBRTLClientNotAuthorized    = 8, /* client sans entitlement ou autre équipe */
    kBRTLClientRawControlDenied = 9  /* requête USB brute : refusée en v2       */
} BRTLClientStatus;

/* BHRFSimpleStatus.status des sélecteurs 3 à 5 (ring, StartRx, StopRx)
 * appelés par un client qui ne possède pas la radio. Valeur choisie hors de
 * l'énumération BHRFStatus (0 à 11) de bhrf_shared.h. */
#define BRTL_BHRF_STATUS_NOT_OWNER 12

/* Commandes. `value` est un entier signé 64 bits dans l'unité indiquée. */
typedef enum {
    kBRTLOpSetFrequency      = 1, /* Hz                                          */
    kBRTLOpSetSampleRate     = 2, /* éch./s : 225001–300000 ou 900001–3200000    */
    kBRTLOpSetGainMode       = 3, /* 0 automatique (AGC du tuner), 1 manuel      */
    kBRTLOpSetGain           = 4, /* dixièmes de dB, palier le plus proche ;     */
                                  /* passe le tuner en gain manuel               */
    kBRTLOpSetFreqCorrection = 5, /* ppm, −1000 à 1000                           */
    kBRTLOpSetRtlAgc         = 6, /* AGC numérique du RTL2832U : 0 / 1           */
    kBRTLOpSetDirectSampling = 7, /* 0 non, 1 branche I, 2 branche Q             */
    kBRTLOpSetOffsetTuning   = 8, /* 0 / 1                                       */
    kBRTLOpSetBiasTee        = 9  /* 0 / 1 — décision de l'utilisateur           */
} BRTLOpcode;

/* Bornes vérifiées par le pilote AVANT tout accès au bus. */
#define BRTL_FREQ_MIN_HZ        1000000LL      /* direct sampling inclus        */
#define BRTL_FREQ_MAX_HZ        2200000000LL
#define BRTL_RATE_LOW_MIN       225001LL
#define BRTL_RATE_LOW_MAX       300000LL
#define BRTL_RATE_HIGH_MIN      900001LL
#define BRTL_RATE_HIGH_MAX      3200000LL
#define BRTL_GAIN_MAX_TENTH_DB  1000LL
#define BRTL_PPM_LIMIT          1000LL

#define BRTL_MAX_GAINS          32u

#pragma pack(push, 1)

typedef struct BRTLCommand {
    uint32_t opcode;    /* BRTLOpcode                                          */
    uint32_t reserved0;
    int64_t  value;
} BRTLCommand;

/* Réponse d'Acquire, de Release et de Command. */
typedef struct BRTLClientResult {
    int32_t  status;           /* BRTLClientStatus                            */
    int32_t  controllerStatus; /* code du contrôleur (0 succès), si pertinent */
    int64_t  applied;          /* valeur réellement appliquée (Hz, dixièmes   */
                               /* de dB…), ou 0                               */
} BRTLClientResult;

/* Lecture libre (tout client autorisé). */
typedef struct BRTLDeviceInfo {
    uint32_t infoVersion;        /* 1                                         */
    int32_t  tunerStatus;        /* 0 tuner prêt ; sinon code du contrôleur   */
    uint32_t tunerType;          /* 1 E4000, 2 FC0012, 3 FC0013, 4 FC2580,    */
                                 /* 5 R820T/R820T2/R860, 6 R828D, 0 inconnu   */
    uint32_t rtlXtalHz;
    uint32_t tunerXtalHz;
    uint32_t gainCount;
    int32_t  gainsTenthDb[BRTL_MAX_GAINS];

    uint32_t ownerPresent;       /* 1 si un client possède la radio           */
    uint32_t callerIsOwner;      /* 1 si c'est l'appelant                     */

    /* Réglages en place (dernières valeurs appliquées par le pilote). */
    uint32_t frequencyHz;
    uint32_t sampleRateHz;
    int32_t  gainTenthDb;        /* −1 : automatique                          */
    int32_t  freqCorrectionPpm;
    uint32_t flags;              /* BRTL_FLAG_*                               */
    uint32_t directSampling;

    /* Compteurs des commandes, depuis le chargement du pilote. Ils ne se
     * mélangent ni entre eux ni avec ceux du ring. */
    uint64_t commandsReceived;
    uint64_t commandsApplied;
    uint64_t commandsRejected;   /* bornes, propriété, opcode                  */
    uint64_t commandsFailed;     /* échec du contrôleur                        */
    uint64_t rawControlDenied;   /* requêtes USB brutes refusées               */
    uint64_t busyRefusals;       /* Acquire refusés (radio déjà prise)         */
    int32_t  lastCommandStatus;  /* BRTLClientStatus de la dernière commande   */
    uint32_t reserved0;

    char     tunerName[24];
    char     manufacturer[32];
    char     product[32];
} BRTLDeviceInfo;

#pragma pack(pop)

#define BRTL_FLAG_RTL_AGC        (1u << 0)
#define BRTL_FLAG_BIAS_TEE       (1u << 1)
#define BRTL_FLAG_OFFSET_TUNING  (1u << 2)
#define BRTL_FLAG_MANUAL_GAIN    (1u << 3)
#define BRTL_FLAG_STREAMING      (1u << 4)

/* Tailles figées : une divergence entre les copies est une erreur de build. */
#ifdef __cplusplus
static_assert(sizeof(BRTLCommand) == 16, "BRTLCommand");
static_assert(sizeof(BRTLClientResult) == 16, "BRTLClientResult");
static_assert(sizeof(BRTLDeviceInfo) == 328, "BRTLDeviceInfo");
#else
_Static_assert(sizeof(BRTLCommand) == 16, "BRTLCommand");
_Static_assert(sizeof(BRTLClientResult) == 16, "BRTLClientResult");
_Static_assert(sizeof(BRTLDeviceInfo) == 328, "BRTLDeviceInfo");
#endif

#ifdef __cplusplus
}
#endif

#endif /* BRTL_CLIENT_PROTOCOL_H */
