/* hfupdate.c */

/*
   Copyright (c) 2013, 2014 HashFast Technologies LLC
*/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <time.h>
#include <libusb-1.0/libusb.h>

#include "hf_loader.h"


#define TIMEOUT 1000

/* we could look through the descriptors to find this, but there isn't
   any reason to change it so we'll use constants. */
#define HF_LOADER_INTERFACE                          0
#define HF_LOADER_INTERFACE_ALTERNATE_SETTING        1
#define HF_LOADER_EP_OUT                          0x02


static const char banner[] =
    "hfupdate v0.1";

static const char usage[] =
#if defined(LIBUSBX_API_VERSION) && (LIBUSBX_API_VERSION >= 0x01000102)
    "usage: %s [-a<addr>] [-b<n>] [-d] [-e|E] [-m<n>] [-p<n>] [-r|R] [file]\n"
#else
    "usage: %s [-a<addr>] [-b<n>] [-d] [-e|E] [-m<n>] [-r|R] [file]\n"
#endif
    "    -a<addr>       select device with specified address\n"
    "    -b<bus>        select device on specified bus\n"
    "    -d             show device debug buffer\n"
    "    -e             reenumerate slaves\n"
    "    -E             reboot slaves into loader and reenumerate them\n"
    "    -m<module>     update specified module; module 0 has usb connection\n"
#if defined(LIBUSBX_API_VERSION) && (LIBUSBX_API_VERSION >= 0x01000102)
    "    -p<port>       select device on specified port\n"
#endif
    "    -r[<module>]   reboot modules into app\n"
    "    -R[<module>]   reboot modules into loader\n";


static void progress(void) {
    static time_t old = 0;
    static int state = 0;
    time_t new;

    new = time(NULL);
    if (new != old) {
        old = new;
        switch (state++) {
        case 0:
            putchar('/');
            break;
        case 1:
            putchar('-');
            break;
        case 2:
            putchar('\\');
            break;
        case 3:
            putchar('|');
            state = 0;
            break;
        }
        putchar('\b');
        fflush(stdout);
    }
}

static void dumpBytes(unsigned char *ptr, int count, int hexOnly) {
    char *charPtr;
    int oldCount;
    int ch;
    int i;

    while (count) {
        charPtr = (char *) ptr;
        oldCount = count;

        for (i = 0; count && i < 16; i++) {
            printf("%02x ", (int) *ptr++);
            count--;
        }
        if (!hexOnly) {
            for (; i < 16; i++)
                fputs("   ", stdout);

            fputs(" ", stdout);
            for (i = 0; oldCount && i < 16; i++, oldCount--) {
                ch = *charPtr++;
                printf("%c",
                       (isascii(ch) && isprint(ch) &&
                        (ch == ' ' || !isspace(ch))) ?  ch : '.');
            }
        }

        fputs("\n", stdout);
    }
}

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
    int interfaceClaimed;
    char *fileName;
    int targetModule;
    int reenumerateSlaves;
    int dumpDebug;
    FILE *s;
    int count;
    unsigned char usbBuffer[64];
    unsigned char *ptr;
    int transferred;
    unsigned int version;
    unsigned int mainVersion;
    unsigned int crc;
    unsigned int status;
    int master;
    int slaveCount;
    int reboot;
    int rebootModule;
    int i, j;
    int retries;

    result = 0;
    usbInitialized = 0;
    bus = -1;
#if defined(LIBUSBX_API_VERSION) && (LIBUSBX_API_VERSION >= 0x01000102)
    port = -1;
#endif
    addr = -1;
    interfaceClaimed = 0;
    dumpDebug = 0;
    reenumerateSlaves = 0;
    reboot = 0;
    rebootModule = 0xff;
    targetModule = 0;
    dev = NULL;
    s = NULL;
    fileName = NULL;
    mainVersion = 0;
    puts(banner);
    for (i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            switch (argv[i][1]) {
            case 'a':
                addr = atoi(&argv[i][2]);
                break;
            case 'b':
                bus = atoi(&argv[i][2]);
                break;
            case 'd':
                dumpDebug = 1;
                break;
            case 'e':
                reenumerateSlaves = 1;
                break;
            case 'E':
                reenumerateSlaves = 2;
                break;
            case 'm':
                targetModule = atoi(&argv[i][2]);
                break;
#if defined(LIBUSBX_API_VERSION) && (LIBUSBX_API_VERSION >= 0x01000102)
            case 'p':
                port = atoi(&argv[i][2]);
                break;
#endif
            case 'r':
                reboot = 1;
                if (isdigit(argv[i][2]))
                    rebootModule = atoi(&argv[i][2]);
                break;
            case 'R':
                reboot = 2;
                if (isdigit(argv[i][2]))
                    rebootModule = atoi(&argv[i][2]);
                break;
            default:
                result = 1;
                break;
            }
        } else {
            if (fileName == NULL)
                fileName = argv[i];
            else
                result = 1;
        }
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
                if (desc.idVendor == HF_LOADER_USB_VENDOR_ID &&
                    desc.idProduct == HF_LOADER_USB_PRODUCT_ID) {
                    if (j >= 0) {
                        fprintf(stderr,
                                "more than one device found; aborting\n");
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

    if (result == 0 && reenumerateSlaves) {
        result = libusb_control_transfer(dev,
                                         LIBUSB_ENDPOINT_OUT |
                                         LIBUSB_REQUEST_TYPE_VENDOR |
                                         LIBUSB_RECIPIENT_INTERFACE,
                                         HF_LOADER_USB_RESTART_ADDR,
                                         (reenumerateSlaves > 1) ?
                                         0x0001 : 0x0000, /* value */
                                         0x0000, /* index */
                                         NULL,
                                         0,
                                         TIMEOUT);
        if (result) {
            fprintf(stderr, "restart addressing failed %d\n", result);
        } else
            sleep(3);
    }

    if (result == 0 && dumpDebug) {
        count = libusb_control_transfer(dev,
                                        LIBUSB_ENDPOINT_IN |
                                        LIBUSB_REQUEST_TYPE_VENDOR |
                                        LIBUSB_RECIPIENT_INTERFACE,
                                        HF_LOADER_USB_DEBUG, /* request */
                                        0x0000, /* value */
                                        0x0000, /* index */
                                        usbBuffer,
                                        64,
                                        TIMEOUT);
        if (count > 0) {
            printf("debug:\n");
            dumpBytes(usbBuffer, count, 0);
        }
    }

    retries = 0;
    while (result == 0) {
        count = libusb_control_transfer(dev,
                                         LIBUSB_ENDPOINT_IN |
                                         LIBUSB_REQUEST_TYPE_VENDOR |
                                         LIBUSB_RECIPIENT_INTERFACE,
                                         HF_LOADER_USB_CONFIG, /* request */
                                         0x0000, /* value */
                                         0x0000, /* index */
                                         usbBuffer,
                                         4,
                                         TIMEOUT);
        if (count >= 3) {
            if (usbBuffer[2] != HF_LOADER_CHAIN_UNCONFIGURED &&
                (count == 3 || !usbBuffer[0] || usbBuffer[3])) {
                master = (int) usbBuffer[0];
                slaveCount = (int) usbBuffer[1];
                printf("module chain config %u master %u slaves %u\n",
                       (unsigned int) usbBuffer[2],
                       (unsigned int) master,
                       (unsigned int) slaveCount);
                break;
            } else if (++retries > 3) {
                fprintf(stderr,
                        "module chain still not configured, giving up\n");
                result = 1;
            } else
                sleep(1);
        } else {
            fprintf(stderr, "HF_LOADER_USB_CONFIG failed (%d)\n", count);
            result = 1;
        }
    }

    if (result == 0 && targetModule && !master) {
        fprintf(stderr, "cannot update remote module through non-master\n");
        result = 1;
    }

    if (result == 0 && targetModule > slaveCount) {
        fprintf(stderr, "target module %d not found\n", targetModule);
        result = 1;
    }

    for (i = 0; i <= slaveCount && result == 0; i++) {
        retries = 0;
        do {
            count = libusb_control_transfer(dev,
                                            LIBUSB_ENDPOINT_IN |
                                            LIBUSB_REQUEST_TYPE_VENDOR |
                                            LIBUSB_RECIPIENT_INTERFACE,
                                            HF_LOADER_USB_VERSION,
                                            0x0000, /* value */
                                            i, /* index */
                                            usbBuffer,
                                            9,
                                            TIMEOUT);
            if (count >= 4) {
                version = usbBuffer[0] |
                          ((unsigned int) usbBuffer[1] << 8) |
                          ((unsigned int) usbBuffer[2] << 16) |
                          ((unsigned int) usbBuffer[3] << 24);
                if (version == 0)
                    sleep(1);
                else {
                    if (i == 0)
                        mainVersion = version;
                    if (count >= 9 && usbBuffer[4]) {
                        crc = usbBuffer[5] |
                              ((unsigned int) usbBuffer[6] << 8) |
                              ((unsigned int) usbBuffer[7] << 16) |
                              ((unsigned int) usbBuffer[8] << 24);
                        printf("module %d version 0x%08x crc 0x%08x\n",
                               i, (unsigned int) version, (unsigned int) crc);
                    } else
                        printf("module %d version 0x%08x\n",
                               i, (unsigned int) version);
                }
            } else {
                fprintf(stderr, "HF_LOADER_USB_VERSION failed (%d)\n", count);
                result = 1;
            }
        } while (result == 0 && version == 0 && ++retries <= 3);
        if (!(version & 0x80000000) && targetModule == i) {
            fprintf(stderr, "target module not in loader mode\n");
            result = 1;
        }
    }

    if ((mainVersion & 0xffff) >= 0x0003) {
        for (i = 0; i <= slaveCount && result == 0; i++) {
            count = libusb_control_transfer(dev,
                                            LIBUSB_ENDPOINT_IN |
                                            LIBUSB_REQUEST_TYPE_VENDOR |
                                            LIBUSB_RECIPIENT_INTERFACE,
                                            HF_LOADER_USB_SERIAL,
                                            0x0000, /* value */
                                            i, /* index */
                                            usbBuffer,
                                            28,
                                            TIMEOUT);
            if (count >= 28) {
                if (usbBuffer[0] == 0x00 && usbBuffer[1] == 0x00 &&
                    usbBuffer[2] == 0x42 && usbBuffer[3] == 0xaa &&
                    usbBuffer[4] == 'H' && usbBuffer[5] == 'F' &&
                    usbBuffer[6] == ':' && usbBuffer[7] == ':' &&
                    usbBuffer[24] == ':' && usbBuffer[25] == ':' &&
                    usbBuffer[26] == 'F' && usbBuffer[27] == 'H') {
                    printf("module %d serial number: ", i);
                    dumpBytes(&usbBuffer[8], 16, 1);
                } else {
                    printf("module %d invalid serial number:\n", i);
                    dumpBytes(usbBuffer, 28, 1);
                }
            } else
                break;
        }
    }


    if (result == 0) {
        libusb_detach_kernel_driver(dev, HF_LOADER_INTERFACE);
        result = libusb_claim_interface(dev, HF_LOADER_INTERFACE);
        if (result == 0)
            interfaceClaimed = 1;
        else
            fprintf(stderr, "failed to claim interface (%d)\n", result);
    }

#ifdef HF_LOADER_INTERFACE_ALTERNATE_SETTING
    if (result == 0) {
        result = libusb_set_interface_alt_setting(
                     dev, HF_LOADER_INTERFACE,
                     HF_LOADER_INTERFACE_ALTERNATE_SETTING);
        if (result)
            fprintf(stderr, "failed to set interface alternate setting\n");
    }
#endif

    if (result == 0 && fileName) {
        s = fopen(fileName, "rb");
        if (s == NULL) {
            fprintf(stderr, "failed to open file %s\n", fileName);
            result = 1;
        }
    }

    if (result == 0 && s) {
        if ((count = libusb_control_transfer(dev,
                                             LIBUSB_ENDPOINT_OUT |
                                             LIBUSB_REQUEST_TYPE_VENDOR |
                                             LIBUSB_RECIPIENT_INTERFACE,
                                             HF_LOADER_USB_START, /* request */
                                             0x0000, /* value */
                                             targetModule, /* index */
                                             NULL,
                                             0,
                                             TIMEOUT)) != 0) {
            fprintf(stderr, "start download failed (%d)\n", count);
            result = 1;
        }
    }

    while (result == 0 && s &&
           (count = fread(usbBuffer, 1, sizeof(usbBuffer), s)) > 0) {
        ptr = usbBuffer;
        while (count && result == 0) {
            result = libusb_bulk_transfer(dev,
                                          HF_LOADER_EP_OUT,
                                          ptr,
                                          count,
                                          &transferred,
                                          TIMEOUT);
            if (result)
                fprintf(stderr, "bulk transfer result %d\n", result);
            else {
                ptr += transferred;
                count -= transferred;
                if (transferred)
                    progress();
            }
        }
    }
    if (result == 0 && s) {
        /* work around Atmel ASF USB code holding data back until it
           receives a short packet or fills all of its internal buffer
           space. */
        result = libusb_bulk_transfer(dev,
                                      HF_LOADER_EP_OUT,
                                      NULL,
                                      0,
                                      &transferred,
                                      TIMEOUT);
        if (result)
            fprintf(stderr, "bulk transfer result %d\n", result);
    }

    if (result == 0 && s) {
        if ((count = libusb_control_transfer(dev,
                                             LIBUSB_ENDPOINT_OUT |
                                             LIBUSB_REQUEST_TYPE_VENDOR |
                                             LIBUSB_RECIPIENT_INTERFACE,
                                             HF_LOADER_USB_FINISH,
                                             0x0000, /* value */
                                             0x0000, /* index */
                                             NULL,
                                             0,
                                             TIMEOUT)) != 0) {
            fprintf(stderr, "finish download failed (%d)\n", count);
            result = 1;
        }
    }

    status = HF_LOADER_STATUS_BUSY;
    retries = 0;
    while (result == 0 && s && status == HF_LOADER_STATUS_BUSY &&
           ++retries < 10) {
        if ((count = libusb_control_transfer(dev,
                                             LIBUSB_ENDPOINT_IN |
                                             LIBUSB_REQUEST_TYPE_VENDOR |
                                             LIBUSB_RECIPIENT_INTERFACE,
                                             HF_LOADER_USB_STATUS, /* request */
                                             0x0000, /* value */
                                             targetModule, /* index */
                                             usbBuffer,
                                             4,
                                             TIMEOUT)) == 4) {
            status = usbBuffer[0] |
                     ((unsigned int) usbBuffer[1] << 8) |
                     ((unsigned int) usbBuffer[2] << 16) |
                     ((unsigned int) usbBuffer[3] << 24);
            if (status == HF_LOADER_STATUS_BUSY)
                sleep(1);
            else if (status != HF_LOADER_STATUS_OK) {
                fprintf(stderr, "update error %u\n", status);
                result = 1;
            }
        } else {
            fprintf(stderr, "get status failed (%d)\n", count);
            result = 1;
        }
    }

    if (s)
        fclose(s);

    if (result == 0 && reboot) {
        if ((count = libusb_control_transfer(dev,
                                             LIBUSB_ENDPOINT_OUT |
                                             LIBUSB_REQUEST_TYPE_VENDOR |
                                             LIBUSB_RECIPIENT_INTERFACE,
                                             HF_LOADER_USB_REBOOT,
                                             (reboot == 2) ? 1 : 0, /* value */
                                             (unsigned int) rebootModule,
                                             NULL,
                                             0,
                                             TIMEOUT)) != 0) {
            fprintf(stderr, "reboot request failed (%d)\n", count);
            result = 1;
        }
    }

    if (usbInitialized) {
        if (interfaceClaimed)
            libusb_release_interface(dev, 0);
        if (dev)
            libusb_close(dev);
        libusb_exit(NULL);
    }
    if (result == 0) {
        puts("done");
    }

    return result;
}

