/* hfterm.c */

/*
   Copyright (c) 2014 HashFast Technologies LLC
*/


#include <stdio.h>
#include <stdlib.h>
#include <libusb-1.0/libusb.h>
#include <time.h>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>

#include "hf_protocol.h"


#define TIMEOUT 100


static const char usage[] =
#if defined(LIBUSBX_API_VERSION) && (LIBUSBX_API_VERSION >= 0x01000102)
    "usage: %s [-a<addr>] [-b<bus>] [-p<port>]\n"
#else
    "usage: %s [-a<addr>] [-b<bus>]\n"
#endif
    "    -a<addr>      select device with specified address\n"
    "    -b<bus>       select device on specified bus\n"
#if defined(LIBUSBX_API_VERSION) && (LIBUSBX_API_VERSION >= 0x01000102)
    "    -p<port>      select device on specified port\n"
#endif
    ;


int main(int argc, char *argv[]) {
    int result;
    int usbInitialized;
    libusb_device **devices;
    struct libusb_device_descriptor desc;
    libusb_device_handle *dev;
    int bus;
#if defined(LIBUSBX_API_VERSION) && (LIBUSBX_API_VERSION >= 0x01000102)
    int port;
#endif
    int addr;
    struct termios oldt, newt;
    int oldf;
    int count;
    unsigned char usbBuffer[64 + 1];
    struct timeval tv;
    fd_set rdfs;
    struct timespec sleepTime;
    int busy;
    int i, j;
    int c;

    result = 0;
    usbInitialized = 0;
    bus = -1;
#if defined(LIBUSBX_API_VERSION) && (LIBUSBX_API_VERSION >= 0x01000102)
    port = -1;
#endif
    addr = -1;
    dev = NULL;

    for (i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            switch (argv[i][1]) {
            case 'a':
                addr = atoi(&argv[i][2]);
                break;
            case 'b':
                bus = atoi(&argv[i][2]);
                break;
#if defined(LIBUSBX_API_VERSION) && (LIBUSBX_API_VERSION >= 0x01000102)
            case 'p':
                port = atoi(&argv[i][2]);
                break;
#endif
            default:
                result = 1;
                break;
            }
        } else
            result = 1;
    }
    if (result)
        fprintf(stderr, usage, argv[0]);

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
            if ((bus < 0 || bus == libusb_get_bus_number(devices[i])) &&
#if defined(LIBUSBX_API_VERSION) && (LIBUSBX_API_VERSION >= 0x01000102)
                (port < 0 || port == libusb_get_port_number(devices[i])) &&
#endif
                (addr < 0 || addr == libusb_get_device_address(devices[i])) &&
                libusb_get_device_descriptor(devices[i], &desc) ==
                LIBUSB_SUCCESS) {
                if ((desc.idVendor == HF_USB_VENDOR_ID &&
                     desc.idProduct == HF_USB_PRODUCT_ID_G1)) {
                    if (j >= 0) {
                        fprintf(stderr, "more than one device found; "
                                        "aborting\n");
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

    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    oldf = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, oldf | O_NONBLOCK);

    while (result == 0) {
        busy = 0;
        tv.tv_sec = 0;
        tv.tv_usec = 0;
        FD_ZERO(&rdfs);
        FD_SET(STDIN_FILENO, &rdfs);
        select(STDIN_FILENO + 1, &rdfs, NULL, NULL, &tv);
        if (FD_ISSET(STDIN_FILENO, &rdfs)) {
            c = getchar();
            if (c == EOF || c == '\004')
                break;
            count = libusb_control_transfer(dev,
                                            LIBUSB_ENDPOINT_OUT |
                                            LIBUSB_REQUEST_TYPE_VENDOR |
                                            LIBUSB_RECIPIENT_INTERFACE,
                                            0xd2, /* request */
                                            c, /* value */
                                            0x0000, /* index */
                                            NULL,
                                            0,
                                            TIMEOUT);
            if (count == 0)
                busy = 1;
            else {
                fflush(stdout);
                fprintf(stderr, "error writing term data %d\n", count);
                result = 1;
            }
        }
        if (result == 0) {
            count = libusb_control_transfer(dev,
                                            LIBUSB_ENDPOINT_IN |
                                            LIBUSB_REQUEST_TYPE_VENDOR |
                                            LIBUSB_RECIPIENT_INTERFACE,
                                            0xd2, /* request */
                                            0x0000, /* value */
                                            0x0000, /* index */
                                            usbBuffer,
                                            64,
                                            TIMEOUT);
            if (count > 0 && !(count == 1 && usbBuffer[0] == '\0')) {
                usbBuffer[count] = '\0';
                fputs((char *) usbBuffer, stdout);
                busy = 1;
            } else if (count < 0) {
                fflush(stdout);
                fprintf(stderr, "error reading term data %d\n", count);
                result = 1;
            }
        }
        if (!busy) {
            fflush(stdout);
            sleepTime.tv_sec = 0;
            sleepTime.tv_nsec = 25000000;
            nanosleep(&sleepTime, NULL);
        }
    }

    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    fcntl(STDIN_FILENO, F_SETFL, oldf);

    if (usbInitialized) {
        if (dev)
            libusb_close(dev);
        libusb_exit(NULL);
    }

    return result;
}

