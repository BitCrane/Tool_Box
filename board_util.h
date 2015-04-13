/* board_util.h */

/*
   Copyright (c) 2013, 2014 HashFast Technologies LLC
*/

#ifndef _BOARD_TEST_H_
#define _BOARD_TEST_H_

#include "libusb.h"

#define HF_BULK_INTERFACE (1)
#define HF_EXPECTED_INTERFACES (2)

#ifdef BRUTAL_HACK
extern char *libusb_strerror(int);
#endif // BRUTAL_HACK

extern unsigned char crc8_table[256];  /* CRC-8 table */

struct rw_usb_state {
  libusb_context *ctx;
  libusb_device **device_list;
  libusb_device_handle *handle;
  struct libusb_config_descriptor *config_descriptor;
  const struct libusb_endpoint_descriptor *receive_endpoint;
  const struct libusb_endpoint_descriptor *send_endpoint;
};

void hfa_init_crc8(void);
unsigned char hfa_crc8(unsigned char *h);
void dump_header_and_data (unsigned char *x, int len);

void set_up_rw_usb(struct rw_usb_state *s);
void shut_down_rw_usb(struct rw_usb_state *s);

#endif /* _BOARD_TEST_H_ */
