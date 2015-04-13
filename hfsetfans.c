/* setfans.c */

/*
   Copyright (c) 2014 HashFast Technologies LLC
*/

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include "libusb.h"
#include "hf_protocol.h"
#include "hf_factory.h"

#include "board_util.h"

#define TIMEOUT 1000


static const char usage[] =
    "usage: %s j9speed j11speed\n"
    "  speeds are 0-100\n";


int main(int argc, char *argv[]) {
  struct rw_usb_state s;
  struct {
    struct hf_header h;
    uint8_t d[12];
  } op;
  int rslt;
  int sent;
  int j9, j11;
  int parm;
  int i;

  j9 = 100;
  j11 = 33;

  rslt = 0;
  parm = 0;
  for (i = 1; i < argc; i++) {
    if (*argv[i] == '-') {
      rslt = 1;
    } else {
      switch (parm++) {
      case 0:
        j9 = atoi(argv[i]);
        break;
      case 1:
        j11 = atoi(argv[i]);
        break;
      }
    }
  }
  if (j9 < 0 || j9 > 100 || j11 < 0 || j11 > 100)
    rslt = 1;
  if (rslt) {
    fprintf(stderr, usage, argv[0]);
    return rslt;
  }

  /* Just needs to run once. */
  hfa_init_crc8();

  set_up_rw_usb(&s);

  memset(&op, 0, sizeof(op));

  op.h.preamble = HF_PREAMBLE;
  op.h.operation_code = OP_FAN_SETTINGS;
  op.h.chip_address = 0x00;
  op.h.core_address = 1;
  op.h.hdata = U_MAGIC;
  op.h.data_length = sizeof(op.d) / 4;
  op.h.crc8 = hfa_crc8((unsigned char *) &op.h);
  op.d[0] = 0;
  op.d[1] = 0;
  op.d[2] = 0;
  op.d[3] = 0;
  op.d[4] = 1;
  op.d[5] = (uint8_t) (j9 * 255 / 100);
  op.d[6] = 0;
  op.d[7] = 0;
  op.d[8] = 1;
  op.d[9] = (uint8_t) (j11 * 255 / 100);
  op.d[10] = 0;
  op.d[11] = 0;
  rslt = libusb_bulk_transfer(s.handle, s.send_endpoint->bEndpointAddress, (unsigned char *) &op, sizeof(op), &sent, TIMEOUT);
  if(rslt != 0) {
    fprintf(stderr, "libusb_bulk_transfer() failed while sending: %s\n", libusb_strerror(rslt));
    exit(1);
  }

  /* Fix: libusb_bulk_transfer() is not required to send everything at once.  We expect it to
   *      here because of the short size, but it is not guaranteed.  This should be fine for
   *      the moment, be we should replace it with a generic sending routine that sends
   *      everything once it is written. */
  if(sent != sizeof(op)) {
    fprintf(stderr, "libusb_bulk_transfer() was asked to send %ld bytes and only sent %d.\n",
	    (long) sizeof(op), sent);
    exit(1);
  }

  shut_down_rw_usb(&s);

  return 0;
}

