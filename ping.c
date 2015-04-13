/* ping.c */

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

#define RND "/dev/urandom"

void usage(int argc, char *argv[]);

int main(int argc, char *argv[])
{
  struct rw_usb_state s;
  struct hf_header a;
  unsigned char a_send[64];
  int rslt;
  int sent;
  int received;
  int i;
  unsigned char b[64];
  struct hf_header *b_ptr;
  unsigned int quartets;
  int rnd;
  ssize_t ssrslt;

  if(argc != 1 && argc != 2)
    usage(argc, argv);

  if(argc == 2) {
    rslt = sscanf(argv[1], "%u", &quartets);
    if(rslt != 1)
      usage(argc, argv);
  }
  else {
    quartets = 0;
  }

  /* Fix: Write the code that makes this low limit unnecessary. */
  /* Fix: Packets of 64 bytes exactly seem to mess up the microcontroller! */
  if(quartets * 4 >= 64 - 8) {
    fprintf(stderr, "%d quartets is too large to send.\n", quartets);
    exit(1);
  }

  /* Just needs to run once. */
  hfa_init_crc8();

  set_up_rw_usb(&s);

  memset(&a, 0, sizeof(a));

  a.preamble = 0xaa; /* Always this value. */
  a.operation_code = 140; /* Value for OP_PING */
  a.chip_address = 0x00;
  a.core_address = 0;
  a.hdata = 0; 
  a.data_length = quartets;
  a.crc8 = hfa_crc8((unsigned char *) &a);

  memset(a_send, 0, sizeof(a_send));

  memcpy(a_send, &a, 8);

  rnd = open(RND, O_RDONLY);
  if(rnd == -1) {
    perror("open()");
    exit(1);
  }

  ssrslt = read(rnd, a_send + 8, (size_t) 4*quartets);
  if(ssrslt == -1) {
    perror("read()");
    exit(1);
  }

  rslt = close(rnd);
  if(rslt == -1) {
    perror("close()");
    exit(1);
  }

  rslt = libusb_bulk_transfer(s.handle, s.send_endpoint->bEndpointAddress, a_send, (int) 4*quartets + 8, &sent, 100);
  if(rslt != 0) {
    fprintf(stderr, "libusb_bulk_transfer() failed while sending: %s\n", libusb_strerror(rslt));
    exit(1);
  }

  /* Fix: libusb_bulk_transfer() is not required to send everything at once.  We expect it to
   *      here because of the short size, but it is not guaranteed.  This should be fine for
   *      the moment, be we should replace it with a generic sending routine that sends
   *      everything once it is written. */
  if(sent != 4*quartets + 8) {
    fprintf(stderr, "libusb_bulk_transfer() was asked to send %ld bytes and only sent %d.\n",
	    (long) sizeof(a), sent);
    exit(1);
  }

  /* Fix: libusb_bulk_transfer() seems to have to be called four times
   *      no matter how much delay we introduce.  This suggests that
   *      the EVK board has some bug where needs four asks before it
   *      will reply. */
  for(received=0, i=0; received == 0 && i < 100; i++) {
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
  }

  if(received == 0) {
    fprintf(stderr, "Failed to receive a serial number reply.\n");
    exit(1);
  }

  b_ptr = (struct hf_header *) b;

  if(b_ptr->preamble != 0xaa ||
     b_ptr->operation_code != 140 ||
     b_ptr->chip_address != 0x00 ||
     b_ptr->core_address != 0 ||
     b_ptr->hdata != 0) {
    fprintf(stderr, "Unexpected header: ");
    dump_header_and_data(b, received);
    fprintf(stderr, "\n");
    exit(1);
  }

  if(b_ptr->data_length*4 + 8 != 4*quartets + 8) {
    fprintf(stderr, "Expected %d bytes but received %d: ", (int) (4*quartets + 8), (int) (b_ptr->data_length*4 + 8));
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

  if(memcmp(a_send, b, received) != 0) {
    fprintf(stderr, "Ping did not get back what it sent: ");
    dump_header_and_data(b, received);
    fprintf(stderr, "\n");
    exit(1);
  }

  printf("Ping!\n");

  shut_down_rw_usb(&s);

  return 0;
}

void usage(int argc, char *argv[])
{
  fprintf(stderr, "usage: %s [number of quartets of data to send]\n", argv[0]);
  exit(1);
}
