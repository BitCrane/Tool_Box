/* version.c */

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

#define BUFSIZE 1024

// Fix: These enums need to be kept in sync with uc/uc3/src/main.h.
enum ver_fail {not_primary_board = 0, app_magic_bad, unknown_ver_type,
	       ver_string_too_large, bad_header};
enum ver_type {interface_version = 0, firmware_version, time_compiled,
	       firmware_crc32, version_fail};

void report_version_info(const char *prefix, uint8_t board, uint8_t type);

int main(int argc, char *argv[])
{
  hfa_init_crc8();

  report_version_info("Interface version", 0, interface_version);
  report_version_info("Firmware version", 0, firmware_version);
  report_version_info("When compiled", 0, time_compiled);
  report_version_info("CRC32 of firmware", 0, firmware_crc32);

  return 0;
}

void report_version_info(const char *prefix, uint8_t board, uint8_t type)
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
  unsigned char output[BUFSIZE];
  uint8_t len;

  set_up_rw_usb(&s);

  memset(&a, 0, sizeof(a));

  a.preamble = 0xaa; /* Always this value. */
  a.operation_code = OP_VERSION;
  a.chip_address = board;
  a.core_address = type;
  a.hdata = 0;
  a.data_length = 0;
  a.crc8 = hfa_crc8((unsigned char *) &a);

  memset(a_send, 0, sizeof(a_send));

  memcpy(a_send, &a, 8);

  rslt = libusb_bulk_transfer(s.handle, s.send_endpoint->bEndpointAddress, a_send, (int) 8, &sent, 1000);
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
    rslt = libusb_bulk_transfer(s.handle, s.receive_endpoint->bEndpointAddress, b, (int) sizeof(b), &received, 1000);
    if(rslt != 0) {
      fprintf(stderr, "libusb_bulk_transfer() failed while receiving: %s\n", libusb_strerror(rslt));
      exit(1);
    }
  }

  if(received == 0) {
    fprintf(stderr, "Failed to receive a version reply.\n");
    exit(1);
  }

  b_ptr = (struct hf_header *) b;

  if(b_ptr->crc8 != hfa_crc8(b)) {
    fprintf(stderr, "Bad CRC8 checksum on header: ");
    dump_header_and_data(b, received);
    fprintf(stderr, "\n");
    exit(1);
  }

  if(b_ptr->core_address != 4) {
    if(b_ptr->preamble != 0xaa ||
       b_ptr->operation_code != 142 || /* Fix: OP_VERSION */
       b_ptr->chip_address != board ||
       b_ptr->core_address != type ||
       b_ptr->hdata != 0) {
      fprintf(stderr, "Unexpected header: ");
      dump_header_and_data(b, received);
      fprintf(stderr, "\n");
      exit(1);
    }

    if(b_ptr->data_length*4 + 8 != received) {
      fprintf(stderr, "Got back packet with mismatched length.  ");
      fprintf(stderr, "Received: %ld Expected: %ld\n",
	      (long) received,
	      (long) b_ptr->data_length*4 + 8);
      exit(1);
    }

    if(b_ptr->data_length > 0) {
      len = *(b + 8); /* Length byte of data. */
      if(len > (4*b_ptr->data_length-1)) {
	fprintf(stderr, "Length byte exceeds available data space.  ");
	fprintf(stderr, "Length byte: %ld  Data space: %ld\n",
		(long) len, (long) 4*b_ptr->data_length-1);
	exit(1);
      }

      for(i=len + 1; i < b_ptr->data_length*4; i++)
	if(*(b + 8 + i) != '\0') {
	  fprintf(stderr, "Extra data is not NUL.\n");
	  exit(1);
	}

      memset(output, 0, sizeof(output));
      memcpy(output, b + 8 + 1, len);
      *(output + 8 + 1 + len) = '\0';

      printf("%s: %s\n", prefix, output);
    }
    else {
      printf("%s, not defined\n", prefix);
    }
  }
  else {
    /* Fail return. */
    if(b_ptr->crc8 != hfa_crc8(b)) {
      fprintf(stderr, "Bad CRC8 checksum on header: ");
      dump_header_and_data(b, received);
      fprintf(stderr, "\n");
      exit(1);
    }

    if(b_ptr->preamble != 0xaa ||
       b_ptr->operation_code != 142 || /* Fix: OP_VERSION */
       b_ptr->chip_address != board ||
       b_ptr->core_address != 4) {
      fprintf(stderr, "Unexpected fail header: ");
      dump_header_and_data(b, received);
      fprintf(stderr, "\n");
      exit(1);
    }

    printf("%s, fail value %d\n", prefix, (int) b_ptr->hdata);
  }

  shut_down_rw_usb(&s);
}
