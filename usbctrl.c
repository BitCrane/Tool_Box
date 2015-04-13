
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <libusb-1.0/libusb.h>

#include "hf_protocol.h"
#include "hf_usbctrl.h"
#include "usbctrl.h"


#define TIMEOUT 100


int hfctrl_core_overview(hf_usbctrl_core_overview *overview, libusb_device_handle *dev) {
    int count;
    unsigned char usbBuffer[64];
    count = libusb_control_transfer(dev,
                                    LIBUSB_ENDPOINT_IN |
                                    LIBUSB_REQUEST_TYPE_VENDOR |
                                    LIBUSB_RECIPIENT_INTERFACE,
                                    HF_USBCTRL_CORE_OVERVIEW, /* request */
                                    0x0000, /* value */
                                    0x0000, /* index */
                                    usbBuffer,
                                    64,
                                    TIMEOUT);
    if (count >= 18) {
        overview->die_count = usbBuffer[0];
        overview->core_count = usbBuffer[1];
        overview->total_cores = usbBuffer[2] | (usbBuffer[3] << 8);
        overview->total_good_cores = usbBuffer[4] | (usbBuffer[5] << 8);
        overview->shed_supported = usbBuffer[6];
        overview->groups = usbBuffer[7] | (usbBuffer[8] << 8);
        overview->cores_per_group = usbBuffer[9];
        overview->cores_per_group_cycle = usbBuffer[10] | (usbBuffer[11] << 8);
        overview->groups_per_group_cycle = usbBuffer[12];
        overview->group_core_offset = usbBuffer[13];
        overview->inflight = usbBuffer[14] | (usbBuffer[15] << 8);
        overview->active_jobs = usbBuffer[16] | (usbBuffer[17] << 8);
        if(count >= 21) {
            overview->group_mask = usbBuffer[18] | (usbBuffer[19] << 8);
            overview->group_shift = usbBuffer[20];
        }
        return 1;
    } else {
        return 0;
    }
}

int hfctrl_core_enable(libusb_device_handle *dev, int core, int persist) {
    int count;
    count = libusb_control_transfer(dev,
                                    LIBUSB_ENDPOINT_OUT |
                                    LIBUSB_REQUEST_TYPE_VENDOR |
                                    LIBUSB_RECIPIENT_INTERFACE,
                                    HF_USBCTRL_CORE_ENABLE, /* request */
                                    persist, /* value */
                                    core, /* index */
                                    NULL,
                                    0,
                                    TIMEOUT);
    if(count) {
        return 1;
    } else {
        return 1; // TODO
    }
}

int hfctrl_core_disable(libusb_device_handle *dev, int core, int persist) {
    int count;
    count = libusb_control_transfer(dev,
                                    LIBUSB_ENDPOINT_OUT |
                                    LIBUSB_REQUEST_TYPE_VENDOR |
                                    LIBUSB_RECIPIENT_INTERFACE,
                                    HF_USBCTRL_CORE_DISABLE, /* request */
                                    persist, /* value */
                                    core, /* index */
                                    NULL,
                                    0,
                                    TIMEOUT);
    if(count) {
        return 1;
    } else {
        return 1; // TODO
    }
}

int hfctrl_core_clear(libusb_device_handle *dev, int persist) {
    int count;
    count = libusb_control_transfer(dev,
                                    LIBUSB_ENDPOINT_OUT |
                                    LIBUSB_REQUEST_TYPE_VENDOR |
                                    LIBUSB_RECIPIENT_INTERFACE,
                                    HF_USBCTRL_CORE_CLEAR, /* request */
                                    persist, /* value */
                                    0, /* index */
                                    NULL,
                                    0,
                                    TIMEOUT);
    if(count) {
        return 1;
    } else {
        return 1; // TODO
    }
}

int hfctrl_core_status(hf_usbctrl_core_status *status, libusb_device_handle *dev, int core) {
    int count;
    unsigned char usbBuffer[64];
    count = libusb_control_transfer(dev,
                                    LIBUSB_ENDPOINT_IN |
                                    LIBUSB_REQUEST_TYPE_VENDOR |
                                    LIBUSB_RECIPIENT_INTERFACE,
                                    HF_USBCTRL_CORE_STATUS, /* request */
                                    0x0000, /* value */
                                    core, /* index */
                                    usbBuffer,
                                    64,
                                    TIMEOUT);
    if (count >= 1) {
        status->core_good = usbBuffer[0];
        return 1;
    } else {
        return 0;
    }
}

int hfctrl_core_die_status(hf_usbctrl_core_die_status *status, libusb_device_handle *dev, int die) {
    int count, i;
    unsigned char usbBuffer[64];
    count = libusb_control_transfer(dev,
                                    LIBUSB_ENDPOINT_IN |
                                    LIBUSB_REQUEST_TYPE_VENDOR |
                                    LIBUSB_RECIPIENT_INTERFACE,
                                    HF_USBCTRL_CORE_DIE_STATUS, /* request */
                                    0x0000, /* value */
                                    die, /* index */
                                    usbBuffer,
                                    64,
                                    TIMEOUT);
    if (count >= 48) {
        for(i = 0; i < 12; i++) {
            status->core_good[i] = usbBuffer[i];
        }
        for(i = 0; i < 12; i++) {
            status->core_persist[i] = usbBuffer[12 + i];
        }
        for(i = 0; i < 12; i++) {
            status->core_pending[i] = usbBuffer[24 + i];
        }
        for(i = 0; i < 12; i++) {
            status->core_active[i] = usbBuffer[36 + i];
        }
        if(count >= 64) {
            for(i = 0; i < 8; i++) {
                status->die_hashes |= (usbBuffer[48 + i] << 8*i);
            }
            for(i = 0; i < 8; i++) {
                status->die_nonces |= (usbBuffer[56 + i] << 8*i);
            }
        }
        return 1;
    } else {
        return 0;
    }
}

int hfctrl_core_asic_status(hf_usbctrl_core_asic_status *status, libusb_device_handle *dev, int asic) {
    int count, i;
    unsigned char usbBuffer[64];
    count = libusb_control_transfer(dev,
                                    LIBUSB_ENDPOINT_IN |
                                    LIBUSB_REQUEST_TYPE_VENDOR |
                                    LIBUSB_RECIPIENT_INTERFACE,
                                    HF_USBCTRL_CORE_ASIC_STATUS, /* request */
                                    0x0000, /* value */
                                    asic, /* index */
                                    usbBuffer,
                                    64,
                                    TIMEOUT);
    if (count >= 48) {
        for(i = 0; i < 48; i++) {
            status->core_good[i] = usbBuffer[i];
        }
        if(count >= 64) {
            for(i = 0; i < 8; i++) {
                status->asic_hashes |= (usbBuffer[48 + i] << 8*i);
            }
            for(i = 0; i < 8; i++) {
                status->asic_nonces |= (usbBuffer[56 + i] << 8*i);
            }
        }
        return 1;
    } else {
        return 0;
    }
}

