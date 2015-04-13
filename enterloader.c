/* enterloader.c */

/*
   Copyright (c) 2013, 2014 HashFast Technologies LLC
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

int main(int argc, char *argv[])
{
  struct rw_usb_state s;
  struct hf_header a;
  int rslt;
  int sent;


  /* Just needs to run once. */
  hfa_init_crc8();

  set_up_rw_usb(&s);

  memset(&a, 0, sizeof(a));

  a.preamble = HF_PREAMBLE;
  a.operation_code = OP_DFU;
  a.chip_address = 0x00;
  a.core_address = 0;
  a.hdata = 0;
  a.data_length = 0;
  a.crc8 = hfa_crc8((unsigned char *) &a);

  rslt = libusb_bulk_transfer(s.handle, s.send_endpoint->bEndpointAddress, (unsigned char *) &a, 8, &sent, TIMEOUT);
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

  shut_down_rw_usb(&s);

  return 0;
}

