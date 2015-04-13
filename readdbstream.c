/* readdbstream.c */

/*
   Copyright (c) 2014 HashFast Technologies LLC
*/


#include <stdio.h>
#include <stdlib.h>
#include <libusb-1.0/libusb.h>
#include <time.h>

#include "hf_protocol.h"


#define TIMEOUT 1000


int main(int argc, char *argv[]) {
    int result;
    int usbInitialized;
    libusb_device **devices;
    struct libusb_device_descriptor desc;
    libusb_device_handle *dev;
    int count;
    unsigned char usbBuffer[64 + 1];
    struct timespec sleepTime;
    int i, j;

    result = 0;
    usbInitialized = 0;
    dev = NULL;

    if (result == 0 && libusb_init(NULL)) {
        fprintf(stderr, "failed to initialize libusb\n");
        result = 1;
    }

    if (result == 0) {
        usbInitialized = 1;
        libusb_set_debug(NULL, 0);
        count = libusb_get_device_list(NULL, &devices);
        if (count < 0) {
            fprintf(stderr, "failed to get device list\n");
            result = 1;
        }
    }
    if (result == 0) {
        for (i = 0, j = -1; i < count && result == 0; i++) {
            if (libusb_get_device_descriptor(devices[i], &desc) ==
                LIBUSB_SUCCESS) {
                if ((desc.idVendor == HF_USB_VENDOR_ID &&
                     desc.idProduct == HF_USB_PRODUCT_ID_G1)) {
                    if (j >= 0) {
                        fprintf(stderr, "more than one device found; aborting");
                        result = 1;
                    } else
                        j = i;
                }
            }
        }
        if (j < 0) {
            fprintf(stderr, "failed to find device\n");
            result = 1;
        }
        if (result == 0) {
            if (libusb_open(devices[j], &dev)) {
                fprintf(stderr, "failed to open device\n");
                result = 1;
            }
        }
        libusb_free_device_list(devices, 1);
    }

    while (result == 0) {
        count = libusb_control_transfer(dev,
                                        LIBUSB_ENDPOINT_IN |
                                        LIBUSB_REQUEST_TYPE_VENDOR |
                                        LIBUSB_RECIPIENT_INTERFACE,
                                        0xd1, /* request */
                                        0x0000, /* value */
                                        0x0000, /* index */
                                        usbBuffer,
                                        64,
                                        TIMEOUT);
        if (count > 0 && !(count == 1 && usbBuffer[0] == '\0')) {
            usbBuffer[count] = '\0';
            fputs((char *) usbBuffer, stdout);
        } else if (count < 0) {
            fflush(stdout);
            fprintf(stderr, "error reading debug stream %d\n", count);
            result = 1;
        } else {
            fflush(stdout);
            sleepTime.tv_sec = 0;
            sleepTime.tv_nsec = 25000000;
            nanosleep(&sleepTime, NULL);
        }
    }

    if (usbInitialized) {
        if (dev)
            libusb_close(dev);
        libusb_exit(NULL);
    }

    return result;
}

