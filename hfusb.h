/* hfusb.h */

#ifndef _hfusb_h
#define _hfusb_h

#ifdef __cplusplus
extern "C" {
#endif



#define HFUSB_SUCCESS                                 0
#define HFUSB_ERROR_INVALID_PARAM                -18052
#define HFUSB_ERROR_NO_MEM                       -18053
#define HFUSB_ERROR_THREAD                       -18054
#define HFUSB_ERROR_AMBIGUOUS_DEVICE             -18055
#define HFUSB_ERROR_NO_DEVICE                    -18056


typedef struct hfusbS hfusbT;

typedef struct hfusbDevS hfusbDevT;

typedef struct {
    enum {hfusbDataEvent, hfusbRemovedEvent, hfusbTransferErrorEvent,
          hfusbHotplugEvent} type;
    void *user;
    union {
        struct {
            void *buffer;
            unsigned int length;
        } data;
        enum libusb_transfer_status error;
        struct {
            int bus;
            int port;
            int addr;
            int arrived;
        } hotplug;
    } v;
} hfusbCallbackEventT;



int hfusbInit(hfusbT **context,
              void (*callback)(hfusbCallbackEventT *event), void *user);

int hfusbOpen(hfusbT *context, hfusbDevT **dev, int addr, int bus, int port);

int hfusbControl(hfusbDevT *dev, uint8_t type, uint8_t request,
                 uint16_t value, uint16_t index,
                 void *buffer, unsigned int length);

int hfusbWrite(hfusbDevT *dev, void *buffer, unsigned int length);

void hfusbClose(hfusbDevT *dev);

void hfusbFinish(hfusbT *context);


#ifdef __cplusplus
}
#endif

#endif /* _hfusb_h */

