/* usbctrl.h */

#ifndef _USBCTRL_H
#define _USBCTRL_H

#ifdef __cplusplus
extern "C" {
#endif


/**
 * structure returned by HF_USBCTRL_CORE_OVERVIEW
 */
typedef struct {
    uint8_t  die_count;
    uint8_t  core_count;            // cores
    uint16_t total_cores;
    uint16_t total_good_cores;
    uint8_t  shed_supported;
    uint16_t groups;
    uint8_t  cores_per_group;
    uint16_t cores_per_group_cycle;
    uint8_t  groups_per_group_cycle;
    uint8_t  group_core_offset;
    uint16_t inflight;
    uint16_t active_jobs;
    uint16_t group_mask;
    uint8_t  group_shift;
} hf_usbctrl_core_overview;

int hfctrl_core_overview(hf_usbctrl_core_overview *overview, libusb_device_handle *dev);

/*
 * HF_USBCTRL_CORE_ENABLE
 */
int hfctrl_core_enable(libusb_device_handle *dev, int core, int persist);

/*
 * HF_USBCTRL_CORE_DISABLE
 */
int hfctrl_core_disable(libusb_device_handle *dev, int core, int persist);

/*
 * HF_USBCTRL_CORE_CLEAR
 */
int hfctrl_core_clear(libusb_device_handle *dev, int persist);

/**
 * structure returned by HF_USBCTRL_CORE_STATUS
 */
typedef struct {
    uint8_t  core_good;
    uint8_t  core_persist;
    uint32_t core_ranges;
    uint32_t core_nonces;
} hf_usbctrl_core_status;

int hfctrl_core_status(hf_usbctrl_core_status *status, libusb_device_handle *dev, int core);

/**
 * structure returned by HF_USBCTRL_CORE_DIE_STATUS
 */
typedef struct {
    uint8_t  core_good[12];
    uint8_t  core_persist[12];
    uint8_t  core_pending[12];
    uint8_t  core_active[12];
    uint64_t die_hashes;
    uint64_t die_nonces;
} hf_usbctrl_core_die_status;

int hfctrl_core_die_status(hf_usbctrl_core_die_status *status, libusb_device_handle *dev, int die);

/**
 * structure returned by HF_USBCTRL_CORE_ASIC_STATUS
 */
typedef struct {
    uint8_t  core_good[48];
    uint64_t asic_hashes;
    uint64_t asic_nonces;
} hf_usbctrl_core_asic_status;

int hfctrl_core_asic_status(hf_usbctrl_core_asic_status *status, libusb_device_handle *dev, int asic);


#ifdef __cplusplus
}
#endif
#endif /* _USBCTRL_H */
