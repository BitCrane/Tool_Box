/* readserial.c */

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

#define TIMEOUT 1000

int main(int argc, char *argv[])
{
  struct rw_usb_state s;
  struct hf_header a;
  int rslt;
  int sent;
  int received;
  int i;
  unsigned char b[64];
  struct hf_header *b_ptr;

  /* Just needs to run once. */
  hfa_init_crc8();

  set_up_rw_usb(&s);

  memset(&a, 0, sizeof(a));

  a.preamble = 0xaa; /* Always this value. */
  a.operation_code = OP_SERIAL; /* Read/write serial number. */
  a.chip_address = 0x00;
  a.core_address = 0; /* Says to read the serial number. */
  a.hdata = U_MAGIC; /* op_serial needs this magic number. */
  a.data_length = 0;
  a.crc8 = hfa_crc8((unsigned char *) &a);

  rslt = libusb_bulk_transfer(s.handle, s.send_endpoint->bEndpointAddress, (unsigned char *) &a, (int) sizeof(a), &sent, TIMEOUT);
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
    fprintf(stderr, "Failed to receive a serial number reply.\n");
    exit(1);
  }

  b_ptr = (struct hf_header *) b;

  if(b_ptr->preamble != 0xaa ||
     b_ptr->operation_code != OP_SERIAL ||
     b_ptr->chip_address != 0x00 ||
     b_ptr->core_address != 0 ||
     b_ptr->hdata != U_MAGIC ||
     b_ptr->data_length != 7) { /* 7 * 4 = 28 data bytes */
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

  if(received != 36) {
    fprintf(stderr, "Did not received the expected 36 bytes.\n");
    exit(1);
  }

  if(b[8] != 0x00 || b[9] != 0x00 || b[10] != 0x42 || b[11] != 0xaa) {
    fprintf(stderr, "Did not get valid magic characters: ");
    dump_header_and_data(b, received);
    fprintf(stderr, "\n");
    exit(1);        
  }

  if(b[12] != 'H' || b[13] != 'F' || b[14] != ':' || b[15] != ':') {
    fprintf(stderr, "Starting barrier (i.e. \"HF: \" is missing: ");
    dump_header_and_data(b, received);
    fprintf(stderr, "\n");
    exit(1);    
  }

  if(b[32] != ':' || b[33] != ':' || b[34] != 'F' || b[35] != 'H') {
    fprintf(stderr, "Starting barrier (i.e. \"HF: \" is missing: ");
    dump_header_and_data(b, received);
    fprintf(stderr, "\n");
    exit(1);    
  }

  if (argc == 2) {
      uint32_t short_serial = (b[28] << 24) | (b[29] << 16) | (b[30] << 8) | b[31];
      printf ("%d\n", short_serial);
  } else {

    /* Print the start barrier as ASCII characters. */
    putchar(b[12]);
    putchar(b[13]);
    putchar(b[14]);
    putchar(b[15]);

    /* Print the 16 bytes of unique id in hex. */
    for(i=16; i < 32; i++)
      printf("%02x", b[i]);

    /* Print the stop barrier as ASCII characters. */
    putchar(b[32]);
    putchar(b[33]);
    putchar(b[34]);
    putchar(b[35]);

    printf("\n");
  }

  shut_down_rw_usb(&s);

  return 0;
}
