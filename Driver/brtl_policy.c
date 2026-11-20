/* brtl_policy.c — voir brtl_policy.h. */
#include "brtl_policy.h"

#define BLOCK_DEMOD 0u
#define BLOCK_USB   1u
#define BLOCK_SYS   2u
#define BLOCK_IIC   6u
#define WRITE_FLAG  0x10u

static int block_allowed(uint8_t block)
{
    return block == BLOCK_DEMOD || block == BLOCK_USB ||
           block == BLOCK_SYS || block == BLOCK_IIC;
}

BRTLDenyReason brtl_policy_evaluate(uint8_t bmRequestType, uint8_t bRequest,
                                    uint16_t wValue, uint16_t wIndex,
                                    uint16_t wLength)
{
    if (wLength > BRTL_CTRL_MAX_DATA) return kBRTLDenyLengthTooLarge;

    if (bmRequestType == BRTL_BMREQ_IN_STANDARD) {
        if (bRequest != BRTL_REQ_GET_DESCRIPTOR) return kBRTLDenyUnknownRequest;
        const uint8_t type = (uint8_t)(wValue >> 8);
        if (type != 1u && type != 2u && type != 3u) return kBRTLDenyDescriptorType;
        return kBRTLDenyNone;
    }

    const int isIn  = bmRequestType == BRTL_BMREQ_IN_VENDOR;
    const int isOut = bmRequestType == BRTL_BMREQ_OUT_VENDOR;
    if (!isIn && !isOut) return kBRTLDenyRequestType;
    if (bRequest != 0u) return kBRTLDenyUnknownRequest;

    const uint8_t block = (uint8_t)(wIndex >> 8);
    const uint8_t low = (uint8_t)(wIndex & 0xFFu);
    if (!block_allowed(block)) return kBRTLDenyBlock;

    /* Le drapeau d'écriture doit correspondre au sens du transfert : une
     * écriture déguisée en lecture (ou l'inverse) est refusée. */
    const int writeFlag = (low & WRITE_FLAG) != 0u;
    if (isOut != writeFlag) return kBRTLDenyWriteFlag;
    /* Hors démodulateur, l'octet bas ne porte que le drapeau. */
    if (block != BLOCK_DEMOD && (low & (uint8_t)~WRITE_FLAG) != 0u) {
        return kBRTLDenyWriteFlag;
    }

    if (isOut && block == BLOCK_IIC &&
        (uint8_t)(wValue & 0xFFu) == BRTL_EEPROM_I2C_ADDR && wLength >= 2u) {
        return kBRTLDenyEepromWrite;
    }
    return kBRTLDenyNone;
}

const char *brtl_deny_reason_name(BRTLDenyReason reason)
{
    switch (reason) {
    case kBRTLDenyNone:           return "none";
    case kBRTLDenyRequestType:    return "requestType";
    case kBRTLDenyUnknownRequest: return "unknownRequest";
    case kBRTLDenyBlock:          return "block";
    case kBRTLDenyEepromWrite:    return "eepromWrite";
    case kBRTLDenyLengthTooLarge: return "lengthTooLarge";
    case kBRTLDenyDescriptorType: return "descriptorType";
    case kBRTLDenyWriteFlag:      return "writeFlag";
    }
    return "unknown";
}
