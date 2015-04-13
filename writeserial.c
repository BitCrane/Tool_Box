/* writeserial.c */

/*
   Copyright (c) 2013, 2014 HashFast Technologies LLC
*/

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#include "libusb.h"
#include "hf_protocol.h"
#include "hf_factory.h"
#include "board_util.h"

#define TIMEOUT 1000

void usage(int argc, char *argv[]);
void convert_hex_to_16_bytes(unsigned char *buf, char *hexstring);
int convert_hex_to_int(char c);

int main(int argc, char *argv[])
{
  struct rw_usb_state s;
  int rslt;
  int i;
  size_t len;
  unsigned char newserial_arg[16];
  struct hf_header a;
  int sent;
  int received;
  unsigned char b[64];
  struct hf_header *b_ptr;
  /* 8 header bytes, 4 magic, 4 start barrier, 16 random, 4 stop barrier = 36 bytes */
  unsigned char newserial[36];

  if(argc != 2)
    usage(argc, argv);

  len = strlen(argv[1]);
  if(len != 40)
    usage(argc, argv);

  /* Check for start barrier. */
  if(*(argv[1] + 0) != 'H' || 
     *(argv[1] + 1) != 'F' || 
     *(argv[1] + 2) != ':' || 
     *(argv[1] + 3) != ':')
    usage(argc, argv);

  /* Check hex digits */
  for(i=4; i < 36; i++)
    if(!isxdigit(*(argv[1] + i)))
      usage(argc, argv);

  /* Check for stop barrier. */
  if(*(argv[1] + 36) != ':' || 
     *(argv[1] + 37) != ':' || 
     *(argv[1] + 38) != 'F' || 
     *(argv[1] + 39) != 'H')
    usage(argc, argv);

  convert_hex_to_16_bytes(newserial_arg, argv[1] + 4);

  /* Just needs to run once. */
  hfa_init_crc8();

  set_up_rw_usb(&s);

  memset(&a, 0, sizeof(a));

  a.preamble = 0xaa; /* Always this value. */
  a.operation_code = OP_SERIAL; /* Read/write serial number. */
  a.chip_address = 0x00; /* Does this matter? */
  a.core_address = 1; /* > 0 says to write the serial number. */
  a.hdata = 0x42aa; /* Magic number. */
  a.data_length = 7; /* 7 quadwords = 28 bytes */ 
  a.crc8 = hfa_crc8((unsigned char *) &a);

  memcpy(newserial, &a, 8);
  *(newserial+8) = 0x00;
  *(newserial+9) = 0x00;
  *(newserial+10) = 0x42;
  *(newserial+11) = 0xaa;
  memcpy(newserial+12, "HF::", 4);
  memcpy(newserial+16, newserial_arg, 16);
  memcpy(newserial+32, "::FH", 4);

  rslt = libusb_bulk_transfer(s.handle, s.send_endpoint->bEndpointAddress,
			      (unsigned char *) newserial, (int) sizeof(newserial), &sent, TIMEOUT);
  if(rslt != 0) {
    fprintf(stderr, "libusb_bulk_transfer() failed while sending.\n");
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
     b_ptr->core_address != 1 ||
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

  if(memcmp(newserial, b, 36) != 0) {
    fprintf(stderr, "Did not get back identical packet to what was sent.\n");
    exit(1);
  }

  shut_down_rw_usb(&s);

  return 0;
}

void usage(int argc, char *argv[])
{
  fprintf(stderr, "%s: HF::<16 bytes in hex>::FH\n", argv[0]);
  exit(1);
}

/* Converts string of 32 hex digits to 16 byte array. */
void convert_hex_to_16_bytes(unsigned char *buf, char *hexstring)
{
  int i;

  for(i=0; i < 16; i++) {
    char a = *(hexstring + 2*i);
    char b = *(hexstring + 2*i + 1);
    int a_digit;
    int b_digit;

    if(!isxdigit(a) || !isxdigit(b)) {
      fprintf(stderr, "convert_hex_to_16_bytes(): Got a bad hex digit.\n");
      exit(1);
    }

    a_digit = convert_hex_to_int(a);
    b_digit = convert_hex_to_int(b);

    *(buf+i) = a_digit * 16 + b_digit;
  }
}

int convert_hex_to_int(char c)
{
  if(('0' <= c) && (c <= '9')) {
    return c - '0';
  }
  else if(('a' <= c) && (c <= 'f')) {
    return c - 'a' + 10;
  }
  else if(('A' <= c) && (c <= 'F')) {
    return c - 'A' + 10;
  }
  else {
    fprintf(stderr, "convert_hex_to_int(): Bad hex digit.\n");
    exit(1);
  }
}
