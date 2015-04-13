/* ctrltest.c */

/*
   Copyright (c) 2014 HashFast Technologies LLC
*/


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


static const char usage[] =
#if defined(LIBUSBX_API_VERSION) && (LIBUSBX_API_VERSION >= 0x01000102)
    "usage: %s [-a<addr>] [-b<bus>] [-f<module>,<fan>,<speed>] [-n<name>]\n"
    "       [-o<0/1>] [-p<port>] [-r|R] [-v<module>,<die>,<millivolts>]\n"
#else
    "usage: %s [-a<addr>] [-b<bus>] [-f<module>,<fan>,<speed>] [-n<name>]\n"
    "       [-o<0/1>] [-r|R] [-v<module>,<die>,<millivolts>]\n"
#endif
    "    -a<addr>                       select device with specified address\n"
    "    -b<bus>                        select device on specified bus\n"
    "    -f<module>,<fan>,<speed>       set fan speed\n"
    "    -n<name>                       set device name\n"
    "    -o<0/1>                        turn device power off/on\n"
#if defined(LIBUSBX_API_VERSION) && (LIBUSBX_API_VERSION >= 0x01000102)
    "    -p<port>                       select device on specified port\n"
#endif
    "    -r[<module>]                   reboot into app\n"
    "    -R[<module>]                   reboot into loader\n"
    "    -v<module>,<fan>,<volts>       set die voltage\n"
    "    -c                             core overview\n"
    "    -e<core>,[<persist>]           enable core\n"
    "    -d<core>,[<persist>]           disable core\n"
    "    -C[<persist>]                  clear core map (enable all cores)\n"
    "    -x<core>                       core status\n"
    "    -y<die>                        die status\n"
    "    -z<asic>                       asic status\n"
    ;


static void dumpBytes(unsigned int leadingSpaces, unsigned char *ptr,
                      int count, int hexOnly) {
    char *charPtr;
    int oldCount;
    int ch;
    int i;

    while (count) {
        charPtr = (char *) ptr;
        oldCount = count;

        for (i = 0; i < leadingSpaces; i++)
            putchar(' ');
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
    struct timespec sleepTime;
    int bus;
#if defined(LIBUSBX_API_VERSION) && (LIBUSBX_API_VERSION >= 0x01000102)
    int port;
#endif
    int addr;
    char *name;
    int count;
    unsigned char usbBuffer[64];
    unsigned int u, v;
    unsigned int modules;
    int reboot;
    int rebootModule;
    int powerUp;
    int powerDown;
    int fanModule, fan, fanSpeed;
    int voltageModule, voltageDie, voltage;
    int core_overview;
    int core_idx,  die_idx, asic_idx;
    int core_persist, core_enable, core_disable, core_clear;
    float f;
    int i, j;

    result = 0;
    usbInitialized = 0;
    bus = -1;
#if defined(LIBUSBX_API_VERSION) && (LIBUSBX_API_VERSION >= 0x01000102)
    port = -1;
#endif
    addr = -1;
    dev = NULL;
    modules = 1;

    reboot = 0;
    rebootModule = 0xff;
    fanSpeed = -1;
    voltage = 0;
    name = NULL;
    powerUp = 0;
    powerDown = 0;
    /* Core Control */
    core_overview = 0;
    core_idx      = -1;
    die_idx       = -1;
    asic_idx      = -1;
    core_persist  = 0;
    core_enable   = 0;
    core_disable  = 0;
    core_clear    = 0;
    for (i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            switch (argv[i][1]) {
            case 'a':
                addr = atoi(&argv[i][2]);
                break;
            case 'b':
                bus = atoi(&argv[i][2]);
                break;
            case 'f':
                if (sscanf(&argv[i][2], "%d,%d,%d",
                           &fanModule, &fan, &fanSpeed) != 3)
                    result = 1;
                break;
            case 'n':
                name = &argv[i][2];
                break;
            case 'o':
                if (atoi(&argv[i][2]))
                    powerUp = 1;
                else
                    powerDown = 1;
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
            case 'v':
                if (sscanf(&argv[i][2], "%d,%d,%f",
                           &voltageModule, &voltageDie, &f) == 3)
                    voltage = (int) (f * 1000);
                else
                    result = 1;
                break;
            case 'c':
                core_overview = 1;
                break;
            case 'e':
                if (sscanf(&argv[i][2], "%d,%d",
                           &core_idx, &core_persist) < 1)
                    result = 1;
                core_enable = 1;
                break;
            case 'd':
                if (sscanf(&argv[i][2], "%d,%d",
                           &core_idx, &core_persist) < 1)
                    result = 1;
                core_disable = 1;
                break;
            case 'C':
                sscanf(&argv[i][2], "%d",
                       &core_persist);
                core_clear = 1;
                break;
            case 'x':
                if (sscanf(&argv[i][2], "%d",
                           &core_idx) != 1)
                    result = 1;
                break;
            case 'y':
                if (sscanf(&argv[i][2], "%d",
                           &die_idx) != 1)
                    result = 1;
                break;
            case 'z':
                if (sscanf(&argv[i][2], "%d",
                           &asic_idx) != 1)
                    result = 1;
                break;
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

    if (result == 0 && powerUp) {
        count = libusb_control_transfer(dev,
                                        LIBUSB_ENDPOINT_OUT |
                                        LIBUSB_REQUEST_TYPE_VENDOR |
                                        LIBUSB_RECIPIENT_INTERFACE,
                                        HF_USBCTRL_POWER, /* request */
                                        1, /* value */
                                        0, /* index */
                                        NULL,
                                        0,
                                        TIMEOUT);
        if (count) {
            fprintf(stderr, "powerup request failed (%d)\n", count);
            result = 1;
        }
    }
    while (result == 0 && powerUp) {
        count = libusb_control_transfer(dev,
                                        LIBUSB_ENDPOINT_IN |
                                        LIBUSB_REQUEST_TYPE_VENDOR |
                                        LIBUSB_RECIPIENT_INTERFACE,
                                        HF_USBCTRL_STATUS, /* request */
                                        0, /* value */
                                        0, /* index */
                                        usbBuffer,
                                        64,
                                        TIMEOUT);
        if (count >= 6) {
            u = usbBuffer[0] |
                ((unsigned int) usbBuffer[1] << 8) |
                ((unsigned int) usbBuffer[2] << 16) |
                ((unsigned int) usbBuffer[3] << 24);
            if (usbBuffer[4]) {
                fprintf(stderr, "powerup fault code 0x%02x extra 0x%02x\n",
                        (unsigned int) usbBuffer[4],
                        (unsigned int) usbBuffer[5]);
                result = 1;
            } else if (u == 0) {
                powerUp = 0;
                /* allow time for voltage readings to update */
                sleepTime.tv_sec = 0;
                sleepTime.tv_nsec = 300000000;
                nanosleep(&sleepTime, NULL);
            }
        } else {
            fprintf(stderr, "status request failed (%d)\n", count);
            result = 1;
        }
    }

    if (result == 0) {
        count = libusb_control_transfer(dev,
                                        LIBUSB_ENDPOINT_IN |
                                        LIBUSB_REQUEST_TYPE_VENDOR |
                                        LIBUSB_RECIPIENT_INTERFACE,
                                        HF_USBCTRL_CONFIG, /* request */
                                        0x0000, /* value */
                                        0x0000, /* index */
                                        usbBuffer,
                                        64,
                                        TIMEOUT);
        if (count >= 4) {
            switch (usbBuffer[0]) {
            case 0x00:
                printf("slave\n");
                break;
            case 0x01:
                printf("master\n");
                modules = usbBuffer[1] + 1;
                break;
            case 0xff:
                printf("chain not configured\n");
                break;
            }
            if (usbBuffer[3])
                printf("initialized\n");
            else
                printf("not initialized\n");
        } else {
            fprintf(stderr, "error retrieving config %d\n", count);
            result = 1;
        }
    }

    if (result == 0) {
        count = libusb_control_transfer(dev,
                                        LIBUSB_ENDPOINT_IN |
                                        LIBUSB_REQUEST_TYPE_VENDOR |
                                        LIBUSB_RECIPIENT_INTERFACE,
                                        HF_USBCTRL_NAME, /* request */
                                        0, /* value */
                                        0, /* index */
                                        usbBuffer,
                                        64,
                                        TIMEOUT);
        if (count > 0) {
            usbBuffer[sizeof(usbBuffer) - 1] = '\0';
            if (count < sizeof(usbBuffer))
                usbBuffer[count] = '\0';
            if (usbBuffer[0] && usbBuffer[0] != 0xff)
                printf("name: \"%s\"\n", usbBuffer);
        } else {
            fprintf(stderr, "error retrieving name %d\n", count);
            result = 1;
        }
    }

    for (i = 0; i < modules && result == 0; i++) {
        printf("module %d:\n", i);
        count = libusb_control_transfer(dev,
                                        LIBUSB_ENDPOINT_IN |
                                        LIBUSB_REQUEST_TYPE_VENDOR |
                                        LIBUSB_RECIPIENT_INTERFACE,
                                        HF_USBCTRL_VERSION, /* request */
                                        0x0000, /* value */
                                        i, /* index */
                                        usbBuffer,
                                        64,
                                        TIMEOUT);
        if (count >= 4) {
            u = usbBuffer[0] |
                ((unsigned int) usbBuffer[1] << 8) |
                ((unsigned int) usbBuffer[2] << 16) |
                ((unsigned int) usbBuffer[3] << 24);
            printf("  fw version 0x%08x\n", u);
            if (count >= 9 && usbBuffer[4]) {
                u = usbBuffer[5] |
                    ((unsigned int) usbBuffer[6] << 8) |
                    ((unsigned int) usbBuffer[7] << 16) |
                    ((unsigned int) usbBuffer[8] << 24);
                printf("  fw crc 0x%08x\n", u);
            }
            if (count >= 10)
                printf("  module type %u\n", (unsigned int) usbBuffer[9]);
        } else {
            fprintf(stderr, "error retrieving version %d\n", count);
            result = 1;
        }
        count = libusb_control_transfer(dev,
                                        LIBUSB_ENDPOINT_IN |
                                        LIBUSB_REQUEST_TYPE_VENDOR |
                                        LIBUSB_RECIPIENT_INTERFACE,
                                        HF_USBCTRL_ASIC_PARMS, /* request */
                                        0x0000, /* value */
                                        i, /* index */
                                        usbBuffer,
                                        64,
                                        TIMEOUT);
        if (count >= 20) {
            printf("  ref clock %uMHz\n", (unsigned int) usbBuffer[2]);
            printf("  die settings:");
            for (j = 0; j < 4; j++) {
                u = usbBuffer[j * 4 + 4] |
                    ((unsigned int) usbBuffer[j * 4 + 5] << 8);
                v = usbBuffer[j * 4 + 6] |
                    ((unsigned int) usbBuffer[j * 4 + 7] << 8);
                if (v & 0x8000)
                    printf(" %4uMHz@%4uDAC", u, v & 0x7fff);
                else
                    printf(" %4uMHz@%5.3fV", u, (double) v / 1000.0);
            }
            putchar('\n');
        } else {
            fprintf(stderr, "error retrieving asic parms %d\n", count);
            result = 1;
        }
        count = libusb_control_transfer(dev,
                                        LIBUSB_ENDPOINT_IN |
                                        LIBUSB_REQUEST_TYPE_VENDOR |
                                        LIBUSB_RECIPIENT_INTERFACE,
                                        HF_USBCTRL_SERIAL, /* request */
                                        0x0000, /* value */
                                        i, /* index */
                                        usbBuffer,
                                        64,
                                        TIMEOUT);
        if (count >= 28) {
            if (usbBuffer[0] == 0x00 && usbBuffer[1] == 0x00 &&
                usbBuffer[2] == 0x42 && usbBuffer[3] == 0xaa &&
                usbBuffer[4] == 'H' && usbBuffer[5] == 'F' &&
                usbBuffer[6] == ':' && usbBuffer[7] == ':' &&
                usbBuffer[24] == ':' && usbBuffer[25] == ':' &&
                usbBuffer[26] == 'F' && usbBuffer[27] == 'H') {
                printf("  serial number: ");
                dumpBytes(0, &usbBuffer[8], 16, 1);
            } else {
                printf("  invalid serial number:\n");
                dumpBytes(4, usbBuffer, 28, 1);
            }
        } else {
            fprintf(stderr, "error retrieving serial number %d\n", count);
            result = 1;
        }
        count = libusb_control_transfer(dev,
                                        LIBUSB_ENDPOINT_IN |
                                        LIBUSB_REQUEST_TYPE_VENDOR |
                                        LIBUSB_RECIPIENT_INTERFACE,
                                        HF_USBCTRL_POWER, /* request */
                                        0x0000, /* value */
                                        i, /* index */
                                        usbBuffer,
                                        64,
                                        TIMEOUT);
        if (count >= 26) {
            printf("  input voltages:");
            for (j = 0; j < 4; j++) {
                u = usbBuffer[j * 6 + 2];
                u |= (unsigned int) usbBuffer[j * 6 + 3] << 8;
                printf(" %5.2f", u / 1000.0);
            }
            putchar('\n');
            printf("  output voltages:");
            for (j = 0; j < 4; j++) {
                u = usbBuffer[j * 6 + 4];
                u |= (unsigned int) usbBuffer[j * 6 + 5] << 8;
                printf(" %5.3f", u / 1000.0);
            }
            putchar('\n');
            printf("  board temperatures:");
            for (j = 0; j < 4; j++) {
                u = usbBuffer[j * 6 + 6];
                u |= (unsigned int) usbBuffer[j * 6 + 7] << 8;
                printf(" 0x%04x", (unsigned int) u);
            }
            putchar('\n');
        } else {
            fprintf(stderr, "error retrieving power status %d\n", count);
            result = 1;
        }
        count = libusb_control_transfer(dev,
                                        LIBUSB_ENDPOINT_IN |
                                        LIBUSB_REQUEST_TYPE_VENDOR |
                                        LIBUSB_RECIPIENT_INTERFACE,
                                        HF_USBCTRL_FAN, /* request */
                                        0x0000, /* value */
                                        i, /* index */
                                        usbBuffer,
                                        64,
                                        TIMEOUT);
        if (count >= 10) {
            printf("  tachometers:");
            for (j = 0; j < 4; j++) {
                u = usbBuffer[j * 2 + 2];
                u |= (unsigned int) usbBuffer[j * 2 + 3] << 8;
                printf(" 0x%04x", (unsigned int) u);
            }
            putchar('\n');
        } else {
            fprintf(stderr, "error retrieving tachs %d\n", count);
            result = 1;
        }
    } /* for (i = 0; i < modules && result == 0; i++) */

    if (result == 0 && name) {
        count = libusb_control_transfer(dev,
                                        LIBUSB_ENDPOINT_OUT |
                                        LIBUSB_REQUEST_TYPE_VENDOR |
                                        LIBUSB_RECIPIENT_INTERFACE,
                                        HF_USBCTRL_NAME, /* request */
                                        0, /* value */
                                        0, /* index */
                                        (unsigned char *) name,
                                        strlen(name),
                                        TIMEOUT);
        if (count != strlen(name)) {
            fprintf(stderr, "name set failed (%d)\n", count);
            result = 1;
        }
    }

    if (result == 0 && fanSpeed >= 0) {
        count = libusb_control_transfer(dev,
                                        LIBUSB_ENDPOINT_OUT |
                                        LIBUSB_REQUEST_TYPE_VENDOR |
                                        LIBUSB_RECIPIENT_INTERFACE,
                                        HF_USBCTRL_FAN, /* request */
                                        fanSpeed, /* value */
                                        (fan << 8) | fanModule, /* index */
                                        NULL,
                                        0,
                                        TIMEOUT);
        if (count) {
            fprintf(stderr, "fan set failed (%d)\n", count);
            result = 1;
        }
    }

    if (result == 0 && voltage >= 0) {
        count = libusb_control_transfer(dev,
                                        LIBUSB_ENDPOINT_OUT |
                                        LIBUSB_REQUEST_TYPE_VENDOR |
                                        LIBUSB_RECIPIENT_INTERFACE,
                                        HF_USBCTRL_VOLTAGE, /* request */
                                        voltage, /* value */
                                        (voltageDie << 8) | voltageModule,
                                        NULL,
                                        0,
                                        TIMEOUT);
        if (count) {
            fprintf(stderr, "voltage set failed (%d)\n", count);
            result = 1;
        }
    }

    if (result == 0 && powerDown) {
        count = libusb_control_transfer(dev,
                                        LIBUSB_ENDPOINT_OUT |
                                        LIBUSB_REQUEST_TYPE_VENDOR |
                                        LIBUSB_RECIPIENT_INTERFACE,
                                        HF_USBCTRL_POWER, /* request */
                                        0, /* value */
                                        0, /* index */
                                        NULL,
                                        0,
                                        TIMEOUT);
        if (count) {
            fprintf(stderr, "powerdown request failed (%d)\n", count);
            result = 1;
        }
    }

    if (result == 0 && reboot) {
        count = libusb_control_transfer(dev,
                                        LIBUSB_ENDPOINT_OUT |
                                        LIBUSB_REQUEST_TYPE_VENDOR |
                                        LIBUSB_RECIPIENT_INTERFACE,
                                        HF_USBCTRL_REBOOT, /* request */
                                        (reboot == 2) ? 1 : 0, /* value */
                                        rebootModule, /* index */
                                        NULL,
                                        0,
                                        TIMEOUT);
        if (count) {
            fprintf(stderr, "reboot request failed (%d)\n", count);
            result = 1;
        }
    }
    /*
     * Core Overview
     */
    if (result == 0 && core_overview) {
        hf_usbctrl_core_overview *co;
        co = (hf_usbctrl_core_overview *) malloc(sizeof(hf_usbctrl_core_overview));
        if (hfctrl_core_overview(co, dev)) {
            printf("die count %3i core count %3i shed? %d\n", co->die_count, co->core_count, co->shed_supported);
            printf("total cores:      %3i\n", co->total_cores);
            printf("total cores good: %3i\n", co->total_good_cores);
            printf("  groups | cores per group | cores per group cycle | groups per group cycle\n");
            printf("    %3i  |             %3i |                   %3i |                    %3i\n",
                    co->groups, co->cores_per_group, co->cores_per_group, co->cores_per_group_cycle);
            printf("inflight | active jobs\n");
            printf("     %3i |         %3i\n", co->inflight, co->active_jobs);
        } else {
            fprintf(stderr, "error retrieving cores\n");
            result = 1;
        }
    }
    /*
     * Core Enable
     */
    if(result == 0 && core_idx > -1 && core_enable) {
        if(hfctrl_core_enable(dev, core_idx, core_persist)) {
            // good
        } else {
            fprintf(stderr, "error enabling core\n");
            result = 1;
        }
    }
    /*
     * Core Disable
     */
    if(result == 0 && core_idx > -1 && core_disable) {
        if(hfctrl_core_disable(dev, core_idx, core_persist)) {
            // good
        } else {
            fprintf(stderr, "error disabling core\n");
            result = 1;
        }
    }
    /*
     * Core Clear
     */
    if(result == 0 && core_clear) {
        if(hfctrl_core_clear(dev, core_persist)) {
            // good
        } else {
            fprintf(stderr, "error clearing core\n");
            result = 1;
        }
    }
    /*
     * Core Status
     */
    if (result == 0 && core_idx > -1) {
        hf_usbctrl_core_status *cs;
        cs = (hf_usbctrl_core_status *) malloc(sizeof(hf_usbctrl_core_status));
        if (hfctrl_core_status(cs, dev, core_idx)) {
            dumpBytes(0, (unsigned char *) cs, 1, 1);
        } else {
            fprintf(stderr, "error retrieving core status\n");
            result = 1;
        }
    }
    /*
     * Die Status
     */
    if (result == 0 && die_idx > -1) {
        hf_usbctrl_core_die_status *ds;
        ds = (hf_usbctrl_core_die_status *) malloc(sizeof(hf_usbctrl_core_die_status));
        if (hfctrl_core_die_status(ds, dev, die_idx)) {
            dumpBytes(0, (unsigned char *) ds, 12, 1);
        } else {
            fprintf(stderr, "error retrieving die status\n");
            result = 1;
        }
    }
    /*
     * ASIC Status
     */
    if (result == 0 && asic_idx > -1) {
        hf_usbctrl_core_asic_status *as;
        as = (hf_usbctrl_core_asic_status *) malloc(sizeof(hf_usbctrl_core_asic_status));
        if (hfctrl_core_asic_status(as, dev, asic_idx)) {
            dumpBytes(0, (unsigned char *) as, 48, 1);
        } else {
            fprintf(stderr, "error retrieving asic status\n");
            result = 1;
        }
    }


    if (usbInitialized) {
        if (dev)
            libusb_close(dev);
        libusb_exit(NULL);
    }

    return result;
}

