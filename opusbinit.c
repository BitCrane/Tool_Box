#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include "libusb.h"
#include "hf_protocol.h"
#include "hf_factory.h"
#include "hfparse.h"
#include "board_util.h"


static void dumpBytes(unsigned char *ptr, int count) {
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
        for (; i < 16; i++)
            fputs("   ", stdout);

        fputs(" ", stdout);
        for (i = 0; oldCount && i < 16; i++, oldCount--) {
            ch = *charPtr++;
            printf("%c",
                   (isascii(ch) && isprint(ch) &&
                    (ch == ' ' || !isspace(ch))) ?  ch : '.');
        }

        fputs("\n", stdout);
    }
}

static void packetRx(void *user, hfPacketT *packet) {

    switch (packet->h.h.operation_code) {
    case OP_STATUS:
        printf("op_status die %u\n", (unsigned int) packet->h.h.chip_address);
        printf("  temperature %5.1f voltage %5.3f\n",
               (double) GN_DIE_TEMPERATURE(
                            packet->d.opStatus.monitor.die_temperature),
               (double) GN_CORE_VOLTAGE(
                            packet->d.opStatus.monitor.core_voltage[0]));
        break;
    case OP_CONFIG:
        printf("op_config\n");
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
        dumpBytes(&packet->h.b[0], 8 + packet->h.h.data_length * 4);
        break;
    }
}

int main(int argc, char *argv[])
{
  struct rw_usb_state s;
  unsigned char a[64];
  struct hf_header *a_ptr;
  int rslt;
  int sent;
  int received;
  unsigned char b[64];
  hfparseOptsT parseOpts;
  hfparseT *parse;
  hfparseStatsT stats, oldStats;

  memset(&parseOpts, 0, sizeof(parseOpts));
  parseOpts.includeDataCRC = 0;
  parseOpts.packet = packetRx;
  parse = hfparseCreate(&parseOpts);
  if (parse == NULL) {
    fprintf(stderr, "failed to create parser\n");
    exit(1);
  }
  memset(&oldStats, 0, sizeof(oldStats));

  /* Just needs to run once. */
  hfa_init_crc8();

  set_up_rw_usb(&s);

  memset(&a, 0, sizeof(a));
  a_ptr = (struct hf_header *) a;

  a_ptr->preamble = HF_PREAMBLE;
  a_ptr->operation_code = OP_USB_INIT;
  a_ptr->chip_address = 0x00;
  a_ptr->core_address = 0x10 | 0x01;
  a_ptr->hdata = 0;
  a_ptr->data_length = 0;
  a_ptr->crc8 = hfa_crc8((unsigned char *) &a);

  rslt = libusb_bulk_transfer(s.handle, s.send_endpoint->bEndpointAddress, a, 8, &sent, 100);
  if(rslt != 0) {
    fprintf(stderr, "libusb_bulk_transfer() failed while sending: %s\n", libusb_strerror(rslt));
    exit(1);
  }

  /* Fix: libusb_bulk_transfer() is not required to send everything at once.  We expect it to
   *      here because of the short size, but it is not guaranteed.  This should be fine for
   *      the moment, be we should replace it with a generic sending routine that sends
   *      everything once it is written. */
  if(sent != 8) {
    fprintf(stderr, "libusb_bulk_transfer() was asked to send %ld bytes and only sent %d.\n",
	    (long) sizeof(a), sent);
    exit(1);
  }

  while (1) {
    rslt = libusb_bulk_transfer(s.handle, s.receive_endpoint->bEndpointAddress, b, 64, &received, 100);
    if (rslt == 0) {
      hfparseRun(parse, b, received);
      hfparseStats(parse, &stats);
      if (stats.syncLoss != oldStats.syncLoss)
          printf("sync loss %lu\n", stats.syncLoss);
      if (stats.bytesDiscarded != oldStats.bytesDiscarded)
          printf("bytes discarded %lu\n", stats.bytesDiscarded);
      memcpy(&oldStats, &stats, sizeof(oldStats));
    } else if (rslt != LIBUSB_ERROR_TIMEOUT) {
      fprintf(stderr, "libusb_bulk_transfer returned %d\n", rslt);
      break;
    }
  }

  shut_down_rw_usb(&s);
  hfparseDestroy(parse);

  return 0;
}

