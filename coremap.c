/* coremap.c */

/*
   Copyright (c) 2013, 2014 HashFast Technologies LLC
*/

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "libusb.h"
#include "hf_protocol.h"
#include "hf_factory.h"

#include "board_util.h"

/* Fix: In theory, OP_CORE_MAP can send a packet of more than 64 bytes, so this code
 *      needs to be prepared for getting the data in repeated calls. */

int main()
{
  struct rw_usb_state s;
  struct hf_header a;
  int rslt;
  int sent;
  int received;
  int i;
  unsigned char b[64];
  struct hf_header *b_ptr;
  int total_cores;
  int total_bits;

  /* Just needs to run once. */
  hfa_init_crc8();

  set_up_rw_usb(&s);

  memset(&a, 0, sizeof(a));

  a.preamble = 0xaa; /* Always this value. */
  a.operation_code = 141; /* OP_CORE_MAP */ /* Fix: Get from hf_protocol.h. */
  a.chip_address = 0x00;
  a.core_address = 0;
  a.hdata = 0;
  a.data_length = 0;
  a.crc8 = hfa_crc8((unsigned char *) &a);

  rslt = libusb_bulk_transfer(s.handle, s.send_endpoint->bEndpointAddress, (unsigned char *) &a, (int) sizeof(a), &sent, 100);
  if(rslt != 0) {
    fprintf(stderr, "libusb_bulk_transfer() failed while sending: %s\n", libusb_strerror(rslt));
    exit(1);
  }

  /* Fix: libusb_bulk_transfer() is not required to send everything at once.  We expect it to
   *      here because of the short size, but it is not guaranteed.  This should be fine for
   *      the moment, be we should replace it with a generic sending routine that sends
   *      everything once it is written. */
  if(sent != sizeof(a)) {
    fprintf(stderr, "libusb_bulk_transfer() was asked to send %ld bytes and only sent %d.\n",
	    (long) sizeof(a), sent);
    exit(1);
  }

  /* Fix: libusb_bulk_transfer() seems to have to be called four times
   *      no matter how much delay we introduce.  This suggests that
   *      the EVK board has some bug where needs four asks before it
   *      will reply. */
  for(received=0, i=0; received == 0 && i < 100; i++) {
    struct timespec aa;

    memset(b, 0, sizeof(b));
    /* Fix: We may receive partial reads as libusb_bulk_transfer() is
     *      not required to send everything at once.  We expect it to
     *      here because of the short size, but it is not guaranteed.
     *      This should be fine for the moment, be we should replace
     *      it with a generic sending routine that sends everything
     *      once it is written. */
    rslt = libusb_bulk_transfer(s.handle, s.receive_endpoint->bEndpointAddress, b, (int) sizeof(b), &received, 100);
    if(rslt != 0) {
      fprintf(stderr, "libusb_bulk_transfer() failed while receiving: %s\n", libusb_strerror(rslt));
      exit(1);
    }

    aa.tv_sec = 0;
    aa.tv_nsec = 50 * 1000 * 1000; /* 50 ms */
    nanosleep(&aa, NULL);
  }

  if(received == 0) {
    fprintf(stderr, "Failed to receive a core map reply.\n");
    exit(1);
  }

  b_ptr = (struct hf_header *) b;

  if(b_ptr->preamble != 0xaa ||
     b_ptr->operation_code != 141 || /* OP_CORE_MAP */
     b_ptr->chip_address != 0x00 ||
     b_ptr->core_address != 0) {
    fprintf(stderr, "Unexpected header: ");
    dump_header_and_data(b, received);
    fprintf(stderr, "\n");
    exit(1);
  }

  if(b_ptr->crc8 != hfa_crc8(b)) {
    fprintf(stderr, "Bad CRC8 checksum on header: ");
    dump_header_and_data(b, received);
    fprintf(stderr, "\n");
    exit(1);
  }

  if(4*b_ptr->data_length + 8 != received) {
    fprintf(stderr, "Received %d bytes, but packet claims to be 8 + %d bytes: ",
	    received, (int) b_ptr->data_length);
    dump_header_and_data(b, received);
    fprintf(stderr, "\n");    
  }

  total_cores = b_ptr->hdata;
  total_bits = 4*b_ptr->data_length * 8;
    
  if(total_cores <= total_bits - 32 || total_cores > total_bits) {
    fprintf(stderr, "Core data of %d bits is not consistent with total cores of %d: ",
	    total_bits, total_cores);
    dump_header_and_data(b, received);
    fprintf(stderr, "\n");
    exit(1);
  }

  printf("%d cores: ", total_cores);
  for(i=0; i < b_ptr->data_length * 4; i++)
    printf("%02x", b[8 + i]);
  printf("\n");

  shut_down_rw_usb(&s);

  return 0;
}
