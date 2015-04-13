/* board_util.c */

/*
   Copyright (c) 2013, 2014 HashFast Technologies LLC
*/

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

#include "libusb.h"
#include "hf_protocol.h"
#include "hf_factory.h"
#include "board_util.h"

/* Borrowed from driver-hashfast.c. */

////////////////////////////////////////////////////////////////////////////////
// Support for the CRC's used in header (CRC-8) and packet body (CRC-32)
////////////////////////////////////////////////////////////////////////////////

#define DI8  0x07
unsigned char crc8_table[256];  /* CRC-8 table */

#ifdef BRUTAL_HACK
// Needed for Ubuntu 12.04
static char strerr_a[255];

char *libusb_strerror(int i) {
  snprintf(strerr_a, 255, "%d", i);
  return strerr_a;
}
#endif // BRUTAL_HACK

void hfa_init_crc8(void)
{
  int i,j;
  unsigned char crc;

  for (i = 0; i < 256; i++) {
    crc = i;
    for (j = 0; j < 8; j++)
      crc = (crc << 1) ^ ((crc & 0x80) ? DI8 : 0);
    crc8_table[i] = crc & 0xFF;
  }
}

unsigned char hfa_crc8(unsigned char *h)
{
  int i;
  unsigned char crc;

  h++;	// Preamble not included
  for (i = 1, crc = 0xff; i < 7; i++)
    crc = crc8_table[crc ^ *h++];

  return crc;
}

/* Crude dump function so we don't lose what happened in unexpected error cases. */
void dump_header_and_data (unsigned char *x, int len)
{
  int i;

  if(len > 0)
    fprintf(stderr, "%02x", *(x+0));

  for(i=1; i < len; i++)
    fprintf(stderr, " %02x", *(x+i));
}

/* Set up USB stuff for readserial and writeserial.  Could be
 * generalized, but right now it is specific to the needs of
 * readserial and writeserial. */
void set_up_rw_usb(struct rw_usb_state *s)
{
  int rslt;
  libusb_device *hf_dev;
  unsigned int hf_count;
  libusb_device **dlist;
  struct libusb_device_descriptor hf_desc;
  const struct libusb_interface *interface;
  const struct libusb_interface_descriptor *interface_descriptor;

  rslt = libusb_init(&s->ctx);
  if(rslt != 0) {
    fprintf(stderr, "libusb_init() failed: %s\n", libusb_strerror(rslt));
    exit(1);
  }

  rslt = libusb_get_device_list(s->ctx, &s->device_list);
  if(rslt < 0) {
    fprintf(stderr, "libusb_get_device_list() failed: %s\n", libusb_strerror(rslt));
    exit(1);
  }

  /* Find the single HashFast device we expect. */
  hf_dev = NULL;
  hf_count = 0;
  for(dlist = s->device_list; *dlist != NULL; dlist++) {
    struct libusb_device_descriptor d;

    rslt = libusb_get_device_descriptor(*dlist, &d);
    if(rslt != 0) {
      fprintf(stderr, "Failed to get device descriptor: %s\n", libusb_strerror(rslt));
      exit(1);
    }

    if(d.idVendor == HF_USB_VENDOR_ID && d.idProduct == HF_USB_PRODUCT_ID_G1) {
      hf_dev = *dlist;
      hf_count++;
    }
  }

  if(hf_count == 0) {
    fprintf(stderr, "No HashFast devices found.\n");
    exit(1);
  }

  if(hf_count > 1) {
    fprintf(stderr, "%u HashFast devices found, only one expected.\n", hf_count);
    exit(1);
  }

  /* An assertion, not expected to happen. */
  if(hf_dev == NULL) {
    fprintf(stderr, "Internal error: hf_dev == NULL\n");
    exit(1);
  }

  rslt = libusb_get_device_descriptor(hf_dev, &hf_desc);
  if(rslt != 0) {
    fprintf(stderr, "Failed to get device descriptor: %s\n", libusb_strerror(rslt));
    exit(1);
  }

  /* An assertion, not expected to happen. */
  if(hf_desc.idVendor != HF_USB_VENDOR_ID) {
    fprintf(stderr, "Internal error: HashFast Vendor ID (VID) changed to: %x\n", hf_desc.idVendor);
    exit(1);
  }

  rslt = libusb_open(hf_dev, &s->handle);
  if(rslt != 0) {
    fprintf(stderr, "Error opening HashFast device: %s\n", libusb_strerror(rslt));
    exit(1);
  }

  rslt = libusb_kernel_driver_active(s->handle, HF_BULK_INTERFACE);
  if(rslt != 0) {
    if (rslt == 1) {
      rslt = libusb_detach_kernel_driver(s->handle, HF_BULK_INTERFACE);
      if(rslt != 0) {
	fprintf(stderr, "Failed to detach kernel driver on interface %d: %s.\n",
		HF_BULK_INTERFACE, libusb_strerror(rslt));
	exit(1);
      }
    }
    else {
      fprintf(stderr, "Error checking if kernel driver is active on interface %d: %s",
	      (int) HF_BULK_INTERFACE, libusb_strerror(rslt));
      exit(1);
    }
  }

  /* Fix: It seems to work with the config that we get when we first talk to the device.
   *      Is it possible we need to choose a configuration sometimes, or is it always
   *      the same for any particular device? */
  rslt = libusb_get_active_config_descriptor(hf_dev, &s->config_descriptor);
  if(rslt != 0) {
    fprintf(stderr, "Failed to read active config descriptor: %s\n", libusb_strerror(rslt));
    exit(1);
  }

  /* HF G1 board has two USB "interfaces".  (The physical connection is called
   * the "device" in USB terminology.  The "interface" is a construct which
   * is associated with the "device".  One "device" may have multiple "interfaces". */
  if(s->config_descriptor->bNumInterfaces != HF_EXPECTED_INTERFACES) {
    fprintf(stderr, "Unexpected number of USB interfaces on the device: %d",
	    s->config_descriptor->bNumInterfaces);
    exit(1);
  }

  interface = s->config_descriptor->interface + HF_BULK_INTERFACE;

  /* Assertion, should not happen. */
  if(interface->num_altsetting != 1) {
    fprintf(stderr, "Internal error: Did not expect interface->num_altsetting != 1: %d\n", interface->num_altsetting);
    exit(1);
  }

  interface_descriptor = interface->altsetting;

  /* Assertion, should not happen. */
  if(interface_descriptor->bInterfaceNumber != HF_BULK_INTERFACE) {
    fprintf(stderr, "Internal error: Did not expect interface_descriptor->bInterfaceNumber != %d: %d\n",
	    HF_BULK_INTERFACE, interface_descriptor->bInterfaceNumber);
    exit(1);
  }

  /* Assertion, should not happen. */
  if(interface_descriptor->bNumEndpoints != 2) {
    fprintf(stderr, "Internal error: Did not expect interface_descriptor->bNumEndpoints != 2: %d\n",
	    interface_descriptor->bNumEndpoints);
    exit(1);
  }

  /* We just psychically know which is which, although checking bit 7 would tell us the direction of each. */
  s->receive_endpoint = interface_descriptor->endpoint;
  s->send_endpoint = interface_descriptor->endpoint + 1;

  /* Assertion, should not happen. */
  if(s->receive_endpoint->bEndpointAddress != 0x81) {
    fprintf(stderr, "Internal error: Did not expect s->receive_endpoint->bEndpointAddress != 0x81: %d\n",
	    s->receive_endpoint->bEndpointAddress);
    exit(1);
  }

  /* Assertion, should not happen. */
  if(s->send_endpoint->bEndpointAddress != 0x02) {
    fprintf(stderr, "Internal error: Did not expect s->send_endpoint->bEndpointAddress != 0x02: %d\n",
	    s->send_endpoint->bEndpointAddress);
    exit(1);
  }

  if(s->receive_endpoint->wMaxPacketSize == 0) {
    fprintf(stderr, "Internal error: s->receive_endpoint->wMaxPacketSize == 0\n");
    exit(1);
  }

  if(s->send_endpoint->wMaxPacketSize == 0) {
    fprintf(stderr, "Internal error: s->send_endpoint->wMaxPacketSize == 0\n");
    exit(1);
  }

  /* Assertion, should not happen. */
  if(s->receive_endpoint->wMaxPacketSize != 64) {
    fprintf(stderr, "Internal error: Did not expect s->receive_endpoint->wMaxPacketSize != 64: %d\n",
	    s->receive_endpoint->wMaxPacketSize);
    exit(1);
  }

  /* Assertion, should not happen. */
  if(s->send_endpoint->wMaxPacketSize != 64) {
    fprintf(stderr, "Internal error: Did not expect s->send_endpoint->wMaxPacketSize != 64: %d\n",
	    s->send_endpoint->wMaxPacketSize);
    exit(1);
  }

  /* Need to claim only one interface for a particular handle. */
  rslt = libusb_claim_interface(s->handle, HF_BULK_INTERFACE);
  if(rslt != 0) {
    fprintf(stderr, "Failed to claim interface %d: %s\n", (int) HF_BULK_INTERFACE, libusb_strerror(rslt));
    exit(1);
  }
}

void shut_down_rw_usb(struct rw_usb_state *s)
{
  int rslt;

  rslt = libusb_release_interface(s->handle, HF_BULK_INTERFACE);
  if(rslt != 0) {
    fprintf(stderr, "Failed to release claim to interface %d: %s\n",
	    (int) HF_BULK_INTERFACE, libusb_strerror(rslt));
    exit(1);
  }

  libusb_free_config_descriptor(s->config_descriptor);
  libusb_close(s->handle);

  libusb_free_device_list(s->device_list, 0);
  libusb_exit(s->ctx);
}

