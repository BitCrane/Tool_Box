/* da2stest.c */

/*
   Copyright (c) 2014 HashFast Technologies LLC
*/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <pthread.h>
#include <time.h>
#include <libusb-1.0/libusb.h>

#include "swab.h"
#include "crc.h"
#include "hf_protocol.h"
#include "hf_usbctrl.h"
#include "hfusb.h"
#include "hfparse.h"

#define TIMEOUT 100


static const char usage[] =
#if defined(LIBUSBX_API_VERSION) && (LIBUSBX_API_VERSION >= 0x01000102)
    "usage: %s [-a<addr>] [-b<bus>] [-p<port>] [--v]\n"
#else
    "usage: %s [-a<addr>] [-b<bus>] [-p<port>] [--v]\n"
    "usage: %s [-a<addr>] [-b<bus>]\n"
#endif
    "    -a<addr>                       select device with specified address\n"
    "    -b<bus>                        select device on specified bus\n"
#if defined(LIBUSBX_API_VERSION) && (LIBUSBX_API_VERSION >= 0x01000102)
    "    -p<port>                       select device on specified port\n"
#endif
    "    --v                            verbose\n"
    ;


static struct {
    hfparseT *parser;
    int verbose;
    int enableParser;
    int resetReceived;
    int addressReceived;
    int configReceived;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} da2stest;

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

static void parserCallback(void *user, hfPacketT *packet) {

    switch (packet->h.h.operation_code) {
    case OP_RESET:
        printf("op_reset\n");
        pthread_mutex_lock(&da2stest.mutex);
        da2stest.resetReceived = 1;
        pthread_cond_signal(&da2stest.cond);
        pthread_mutex_unlock(&da2stest.mutex);
        break;
    case OP_ADDRESS:
        printf("op_address: %u die, %u cores each, ref clock %u, dev id %u\n",
               (unsigned int) packet->h.h.chip_address,
               (unsigned int) packet->h.h.core_address,
               (unsigned int) LEToNativeUChawmp(packet->h.h.hdata) >> 8,
               (unsigned int) LEToNativeUChawmp(packet->h.h.hdata) & 0xff);
        pthread_mutex_lock(&da2stest.mutex);
        da2stest.addressReceived = 1;
        pthread_cond_signal(&da2stest.cond);
        pthread_mutex_unlock(&da2stest.mutex);
        break;
    case OP_CONFIG:
        printf("op_config\n");
        pthread_mutex_lock(&da2stest.mutex);
        da2stest.configReceived = 1;
        pthread_cond_signal(&da2stest.cond);
        pthread_mutex_unlock(&da2stest.mutex);
        break;
    case OP_STATUS:
        printf("op_status die %u\n", (unsigned int) packet->h.h.chip_address);
        printf("  temperature %5.1f voltage %5.3f\n",
               (double) GN_DIE_TEMPERATURE(
                            packet->d.opStatus.monitor.die_temperature),
               (double) GN_CORE_VOLTAGE(
                            packet->d.opStatus.monitor.core_voltage[0]));
        break;
    case OP_NONCE:
        printf("op_nonce die %u\n", (unsigned int) packet->h.h.chip_address);
        printf("  nonce 0x%08x seq 0x%04x ntime 0x%04x hdata 0x%04x\n",
               (unsigned int) LEToNativeUGawble(packet->d.opNonce.nonce),
               (unsigned int) LEToNativeUChawmp(packet->d.opNonce.sequence),
               (unsigned int) LEToNativeUChawmp(packet->d.opNonce.ntime),
               (unsigned int) LEToNativeUChawmp(packet->h.h.hdata));
        break;
    case OP_ABORT:
        printf("op_abort\n");
        break;
    case OP_USB_INIT:
        printf("op_usb_init\n");
        break;
    case OP_GWQ_STATUS:
        printf("op_gwq_status\n");
        break;
    case OP_DIE_STATUS:
        printf("op_die_status: die %2u\n",
               (unsigned int) packet->h.h.chip_address);
        printf("  temperature %5.1f voltage %5.3f\n",
               (double) GN_DIE_TEMPERATURE(
                            packet->d.opDieStatus.die.die_temperature),
               (double) GN_CORE_VOLTAGE(
                            packet->d.opDieStatus.die.core_voltage[0]));
        break;
    case OP_USB_NOTICE:
        packet->d.b[packet->h.h.data_length * 4] = '\0';
        printf("op_usb_notice: \"%s\"\n", &packet->d.b[4]);
        break;
    default:
        puts("packet rx:");
        dumpBytes(4, &packet->h.b[0], 8 + packet->h.h.data_length * 4, 0);
        break;
    }
}

static void usbCallback(hfusbCallbackEventT *event) {
    int enable;

    flockfile(stdout);
    switch(event->type) {
    case hfusbDataEvent:
        if (da2stest.verbose)
            printf("callback: data %u\n", event->v.data.length);
        if (event->v.data.length) {
            pthread_mutex_lock(&da2stest.mutex);
            enable = da2stest.enableParser;
            pthread_mutex_unlock(&da2stest.mutex);
            if (da2stest.verbose) {
                printf("parser %s\n", enable ? "enabled" : "disabled");
                dumpBytes(4, event->v.data.buffer, event->v.data.length, 1);
            }
            if (enable)
                hfparseRun(da2stest.parser,
                           event->v.data.buffer, event->v.data.length);
        }
        break;
    case hfusbRemovedEvent:
        printf("callback: device gone\n");
        break;
    case hfusbTransferErrorEvent:
        printf("callback: transfer error %d\n", (int) event->v.error);
        break;
    case hfusbHotplugEvent:
        printf("callback: hotplug %s bus %d addr %d port %d\n",
               event->v.hotplug.arrived ? "added" : "removed",
               event->v.hotplug.bus, event->v.hotplug.addr,
               event->v.hotplug.port);
        break;
    }
    funlockfile(stdout);
}

static int waitNotPending(hfusbDevT *dev, unsigned char *payload) {
    int result;
    int count;
    unsigned int u;

    result = 0;
    while (result == 0) {
        count = hfusbControl(dev,
                             LIBUSB_ENDPOINT_IN |
                             LIBUSB_REQUEST_TYPE_VENDOR |
                             LIBUSB_RECIPIENT_INTERFACE,
                             HF_USBCTRL_STATUS, /* request */
                             0, /* value */
                             0, /* index */
                             payload,
                             64);
        if (count >= 6) {
            u = payload[0] |
                ((unsigned int) payload[1] << 8) |
                ((unsigned int) payload[2] << 16) |
                ((unsigned int) payload[3] << 24);
            if (u == 0 || payload[4])
                break;
        } else {
            fprintf(stderr, "status request failed (%d)\n", count);
            result = 1;
        }
    }

    return result;
}

int main(int argc, char *argv[]) {
    int result;
    int bus;
    int port;
    int addr;
    int mutexInitialized;
    int condInitialized;
    struct timespec sleepTime;
    struct timespec timeout;
    hfusbT *usb;
    hfusbDevT *dev;
    hfparseOptsT parseOpts;
    hfparseStatsT stats;
    int count;
    int modules;
    unsigned char usbBuffer[64];
    unsigned int refClock;
    hfPacketT packet;
    unsigned int baud;
    int retries;
    int done;
    int i;

    result = 0;
    bus = -1;
    port = -1;
    addr = -1;
    modules = 0;
    da2stest.verbose = 0;
    usb = NULL;
    dev = NULL;
    mutexInitialized = 0;
    condInitialized = 0;

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
            case '-':
                switch (argv[i][2]) {
                case 'v':
                    da2stest.verbose++;
                    break;
                default:
                    result = 1;
                    break;
                }
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

    if (result == 0) {
        memset(&parseOpts, 0, sizeof(parseOpts));
        parseOpts.includeDataCRC = 1;
        parseOpts.packet = parserCallback;
        da2stest.parser = hfparseCreate(&parseOpts);
        if (da2stest.parser == NULL) {
            fprintf(stderr, "failed to create parser\n");
            result = 1;
        }
    }

    if (result == 0) {
        if (pthread_mutex_init(&da2stest.mutex, NULL)) {
            fprintf(stderr, "failed to init mutex\n");
            result = 1;
        } else
            mutexInitialized = 1;
    }

    if (result == 0) {
        if (pthread_cond_init(&da2stest.cond, NULL)) {
            fprintf(stderr, "failed to init cond\n");
            result = 1;
        } else
            condInitialized = 1;
    }

    if (result == 0) {
        result = hfusbInit(&usb, usbCallback, NULL);
        if (result)
            fprintf(stderr, "failed to initialize hfusb %d\n", result);
    }

    if (result == 0) {
        result = hfusbOpen(usb, &dev, addr, bus, port);
        if (result)
            fprintf(stderr, "failed to open dev %d\n", result);
    }

    if (result == 0) {
        /* power up */
        count = hfusbControl(dev,
                             LIBUSB_ENDPOINT_OUT |
                             LIBUSB_REQUEST_TYPE_VENDOR |
                             LIBUSB_RECIPIENT_INTERFACE,
                             HF_USBCTRL_POWER, /* request */
                             1, /* value */
                             0, /* index */
                             NULL,
                             0);
        if (count) {
            fprintf(stderr, "powerup request failed %d\n", count);
            result = 1;
        }
    }

    if (result == 0)
        result = waitNotPending(dev, usbBuffer);

    if (result == 0) {
        if (usbBuffer[4]) {
            fprintf(stderr, "powerup fault code 0x%02x extra 0x%02x\n",
                    (unsigned int) usbBuffer[4],
                    (unsigned int) usbBuffer[5]);
            result = 1;
        } else {
#if 0 /* we're not reading those voltages in this test so no need to delay */
            /* wait a bit more for voltage readings to happen */
            sleepTime.tv_sec = 0;
            sleepTime.tv_nsec = 300000000;
            nanosleep(&sleepTime, NULL);
#endif
        }
    }

    if (result == 0) {
        /* get module configuration */
        count = hfusbControl(dev,
                             LIBUSB_ENDPOINT_IN |
                             LIBUSB_REQUEST_TYPE_VENDOR |
                             LIBUSB_RECIPIENT_INTERFACE,
                             HF_USBCTRL_CONFIG, /* request */
                             0, /* value */
                             0, /* index */
                             usbBuffer,
                             64);
        if (count >= 4) {
            switch (usbBuffer[0]) {
            case 0x00:
                printf("slave\n");
                break;
            case 0x01:
                modules = usbBuffer[1] + 1;
                printf("master, %d modules\n", modules);
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

    for (i = 0; i < modules && result == 0; i++) {
        printf("module %d:\n", i);
        count = hfusbControl(dev,
                             LIBUSB_ENDPOINT_IN |
                             LIBUSB_REQUEST_TYPE_VENDOR |
                             LIBUSB_RECIPIENT_INTERFACE,
                             HF_USBCTRL_ASIC_PARMS, /* request */
                             0, /* value */
                             i, /* index */
                             usbBuffer,
                             64);
        if (count >= 20) {
            printf("  ref clock %uMHz\n", (unsigned int) usbBuffer[2]);
            if (i == 0)
                refClock = (unsigned int) usbBuffer[2];
            else if (refClock != (unsigned int) usbBuffer[2]) {
                fprintf(stderr, "cannot handle mixed ref clocks\n");
                result = 1;
            }
        } else {
            fprintf(stderr, "error retrieving asic parms (%d)\n", count);
            result = 1;
        }
    }

    if (result == 0) {
        /* select da2s mode */
        baud = ((unsigned long) BAUD_RATE_PWRUP_7 * refClock + 125 / 2) / 125;
        usbBuffer[0] = baud & 0xff;
        usbBuffer[1] = (baud >> 8) & 0xff;
        usbBuffer[2] = (baud >> 16) & 0xff;
        usbBuffer[3] = (baud >> 24) & 0xff;
        count = hfusbControl(dev,
                             LIBUSB_ENDPOINT_OUT |
                             LIBUSB_REQUEST_TYPE_VENDOR |
                             LIBUSB_RECIPIENT_INTERFACE,
                             HF_USBCTRL_MODE, /* request */
                             1, /* value */
                             0, /* index */
                             usbBuffer,
                             4);
        if (count == 4)
            printf("da2s mode set, baud %u\n", baud);
        else {
            fprintf(stderr, "mode set failed %d\n", count);
            result = 1;
        }
    }

    if (result == 0)
        result = waitNotPending(dev, usbBuffer);

    if (result == 0) {
        /* reset asic */
        count = hfusbControl(dev,
                             LIBUSB_ENDPOINT_OUT |
                             LIBUSB_REQUEST_TYPE_VENDOR |
                             LIBUSB_RECIPIENT_INTERFACE,
                             HF_USBCTRL_ASIC_CTRL, /* request */
                             HF_USBCTRL_ASIC_CTRL_VALUE_RESET |
                             HF_USBCTRL_ASIC_CTRL_VALUE_PLL_BYPASS |
                             7, /* value */
                             0xff, /* index */
                             NULL,
                             0);
        if (count) {
            fprintf(stderr, "asic ctrl failed %d\n", count);
            result = 1;
        }
    }

    if (result == 0)
        result = waitNotPending(dev, usbBuffer);

    if (result == 0) {
        /* take asic out of reset */
        count = hfusbControl(dev,
                             LIBUSB_ENDPOINT_OUT |
                             LIBUSB_REQUEST_TYPE_VENDOR |
                             LIBUSB_RECIPIENT_INTERFACE,
                             HF_USBCTRL_ASIC_CTRL, /* request */
                             HF_USBCTRL_ASIC_CTRL_VALUE_PLL_BYPASS |
                             7, /* value */
                             0xff, /* index */
                             NULL,
                             0);
        if (count) {
            fprintf(stderr, "asic ctrl failed %d\n", count);
            result = 1;
        }
    }

    if (result == 0)
        result = waitNotPending(dev, usbBuffer);

    if (result == 0) {
        pthread_mutex_lock(&da2stest.mutex);
        da2stest.enableParser = 1;
        pthread_mutex_unlock(&da2stest.mutex);
    }

    retries = 0;
    done = 0;
    while (result == 0 && !done) {
        if (result == 0) {
            pthread_mutex_lock(&da2stest.mutex);
            da2stest.resetReceived = 0;
            pthread_mutex_unlock(&da2stest.mutex);
            packet.h.h.preamble = HF_PREAMBLE;
            packet.h.h.operation_code = OP_RESET;
            packet.h.h.chip_address = 0xff;
            packet.h.h.core_address = 0;
            packet.h.h.hdata = nativeToLEUChawmp(0);
            packet.h.h.data_length = 0;
            packet.h.h.crc8 = crc8Accumulate(CRC8_INITIAL, &packet.h.b[1], 6);
            if (da2stest.verbose)
                printf("sending op_reset\n");
            count = hfusbWrite(dev, packet.h.b, 8);
            if (count != 8) {
                fprintf(stderr, "error sending opreset %d\n", count);
                result = 1;
            }
        }

        if (result == 0) {
            clock_gettime(CLOCK_REALTIME, &timeout);
            timeout.tv_sec += 1;
            pthread_mutex_lock(&da2stest.mutex);
            while (!da2stest.resetReceived &&
                   pthread_cond_timedwait(&da2stest.cond, &da2stest.mutex,
                                          &timeout) == 0)
                ;
            pthread_mutex_unlock(&da2stest.mutex);
            if (da2stest.resetReceived) {
                done = 1;
                /* wait 100uS after reset, per gn protocol guide */
                sleepTime.tv_sec = 0;
                sleepTime.tv_nsec = 100000;
                nanosleep(&sleepTime, NULL);
            } else {
                fprintf(stderr, "timed out waiting for op_reset response\n");
                if (++retries > 3)
                    result = 1;
            }
        }
    }

    if (result == 0) {
        /* i have a module that starts spewing nonces at high speed as
           soon as it comes out of reset (even before the op_address,
           so it starts out claiming they're coming from die 0xff).
           maybe a broadcast op_abort will settle things down. */
        /* it does not appear that this helps.  i'll leave it here
           for the now, for possible use in future testing. */
        packet.h.h.preamble = HF_PREAMBLE;
        packet.h.h.operation_code = OP_ABORT;
        packet.h.h.chip_address = 0xff;
        packet.h.h.core_address = 0xff;
        packet.h.h.hdata = nativeToLEUChawmp(3);
        packet.h.h.data_length = 0;
        packet.h.h.crc8 = crc8Accumulate(CRC8_INITIAL, &packet.h.b[1], 6);
        if (da2stest.verbose)
            printf("sending op_abort\n");
        count = hfusbWrite(dev, packet.h.b, 8);
        if (count != 8) {
            fprintf(stderr, "error sending opabort %d\n", count);
            result = 1;
        }
    }

    retries = 0;
    done = 0;
    while (result == 0 && !done) {
        if (result == 0) {
            pthread_mutex_lock(&da2stest.mutex);
            da2stest.addressReceived = 0;
            pthread_mutex_unlock(&da2stest.mutex);
            packet.h.h.preamble = HF_PREAMBLE;
            packet.h.h.operation_code = OP_ADDRESS;
            packet.h.h.chip_address = 0;
            packet.h.h.core_address = 0;
            packet.h.h.hdata = nativeToLEUChawmp(0);
            packet.h.h.data_length = 0;
            packet.h.h.crc8 = crc8Accumulate(CRC8_INITIAL, &packet.h.b[1], 6);
            if (da2stest.verbose)
                printf("sending op_address\n");
            count = hfusbWrite(dev, packet.h.b, 8);
            if (count != 8) {
                fprintf(stderr, "error sending opaddress %d\n", count);
                result = 1;
            }
        }

        if (result == 0) {
            clock_gettime(CLOCK_REALTIME, &timeout);
            timeout.tv_sec += 1;
            pthread_mutex_lock(&da2stest.mutex);
            while (!da2stest.addressReceived &&
                   pthread_cond_timedwait(&da2stest.cond, &da2stest.mutex,
                                          &timeout) == 0)
                ;
            pthread_mutex_unlock(&da2stest.mutex);
            if (da2stest.addressReceived)
                done = 1;
            else {
                fprintf(stderr, "timed out waiting for op_address response\n");
                if (++retries > 3)
                    result = 1;
            }
        }
    }

    done = 0;
    retries = 0;
    while (result == 0 && !done) {
        if (result == 0) {
            pthread_mutex_lock(&da2stest.mutex);
            da2stest.configReceived = 0;
            pthread_mutex_unlock(&da2stest.mutex);
            packet.h.h.preamble = HF_PREAMBLE;
            packet.h.h.operation_code = OP_CONFIG;
            packet.h.h.chip_address = 0xff;
            packet.h.h.core_address = 0;
            packet.h.h.hdata = nativeToLEUChawmp(0x8000 | 0x4000 |
                                                 GN_THERMAL_CUTOFF(105.0));
            packet.h.h.data_length = 4;
            packet.h.h.crc8 = crc8Accumulate(CRC8_INITIAL, &packet.h.b[1], 6);
            memset(&packet.d.opConfig, 0, sizeof(packet.d.opConfig));
            packet.d.opConfig.status_period = 500;
            packet.d.opConfig.enable_periodic_status = 1;
            packet.d.opConfig.rx_header_timeout = 20;
            packet.d.opConfig.rx_data_timeout = 20;
            packet.d.opConfig.stats_interval = 10;
            packet.d.opConfig.measure_interval = 100;
            packet.d.opConfig.one_usec = refClock;
            packet.d.opConfig.max_nonces_per_frame = 1;
            packet.d.opConfig.voltage_sample_points = 0x1f;
            packet.d.g[packet.h.h.data_length] =
                nativeToLEUGawble(crc32Accumulate(CRC32_INITIAL,
                                                  &packet.d.b[0],
                                                  packet.h.h.data_length * 4));
            if (da2stest.verbose)
                printf("sending op_config\n");
            count = hfusbWrite(dev, packet.h.b,
                               8 + packet.h.h.data_length * 4 + 4);
            if (count != 8 + packet.h.h.data_length * 4 + 4) {
                fprintf(stderr, "error sending opconfig %d\n", count);
                result = 1;
            }
        }

        if (result == 0) {
            clock_gettime(CLOCK_REALTIME, &timeout);
            timeout.tv_sec += 1;
            pthread_mutex_lock(&da2stest.mutex);
            while (!da2stest.configReceived &&
                   pthread_cond_timedwait(&da2stest.cond, &da2stest.mutex,
                                          &timeout) == 0)
                ;
            pthread_mutex_unlock(&da2stest.mutex);
            if (da2stest.configReceived)
                done = 1;
            else {
                fprintf(stderr, "timed out waiting for op_config response\n");
                if (++retries > 3)
                    result = 1;
            }
        }
    }

    if (result == 0) {
        printf("done, sleeping a bit to listen for incoming messages\n");
        sleepTime.tv_sec = 15;
        sleepTime.tv_nsec = 0;
        nanosleep(&sleepTime, NULL);
    }

    if (dev)
        hfusbClose(dev);
    if (usb)
        hfusbFinish(usb);

    if (result == 0) {
        hfparseStats(da2stest.parser, &stats);
        printf("syncloss %lu bytesdiscarded %lu datacrcerrors %lu\n",
               stats.syncLoss, stats.bytesDiscarded, stats.dataCRCErrors);
    }

    if (da2stest.parser)
        hfparseDestroy(da2stest.parser);

    if (condInitialized)
        pthread_cond_destroy(&da2stest.cond);
    if (mutexInitialized)
        pthread_mutex_destroy(&da2stest.mutex);

    return result;
}

