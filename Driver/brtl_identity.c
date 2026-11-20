/* brtl_identity.c — voir brtl_identity.h. */
#include "brtl_identity.h"

#include <stddef.h>
#include <string.h>

#define BRTL_VID_REALTEK 0x0BDAu

bool brtl_identity_usb_id_supported(uint16_t vendor_id, uint16_t product_id)
{
    return vendor_id == BRTL_VID_REALTEK &&
           (product_id == 0x2838u || product_id == 0x2832u);
}

/* Égalité exacte, en ignorant les espaces et NUL de fin que certains
 * descripteurs conservent. */
static bool same_text(const char *got, const char *want)
{
    if (got == NULL) return false;
    size_t n = strlen(got);
    while (n > 0 && (got[n - 1] == ' ' || got[n - 1] == '\t')) n--;
    return n == strlen(want) && memcmp(got, want, n) == 0;
}

BRTLDeviceKind brtl_identity_classify(uint16_t vendor_id, uint16_t product_id,
                                      const char *manufacturer,
                                      const char *product)
{
    if (!brtl_identity_usb_id_supported(vendor_id, product_id)) {
        return kBRTLDeviceUnsupported;
    }
    if (same_text(manufacturer, "RTLSDRBlog")) {
        if (same_text(product, "Blog V4")) return kBRTLDeviceBlogV4;
        if (same_text(product, "Blog V4L")) return kBRTLDeviceBlogV4L;
    }
    return kBRTLDeviceGeneric;
}

BRTLCapabilities brtl_identity_capabilities(BRTLDeviceKind kind)
{
    BRTLCapabilities c = { false, false, false };
    switch (kind) {
    case kBRTLDeviceBlogV4:
    case kBRTLDeviceBlogV4L:
        c.bias_tee_available = true;
        c.hf_upconverter = true;
        c.band_switching = true;
        break;
    case kBRTLDeviceGeneric:      /* V3 compris : non identifiable de façon sûre */
    case kBRTLDeviceUnsupported:
    default:
        break;
    }
    return c;
}

const char *brtl_identity_kind_name(BRTLDeviceKind kind)
{
    switch (kind) {
    case kBRTLDeviceGeneric:  return "generic";
    case kBRTLDeviceBlogV4:   return "blogV4";
    case kBRTLDeviceBlogV4L:  return "blogV4L";
    case kBRTLDeviceUnsupported:
    default:                  return "unsupported";
    }
}
