/* hfusb.c */

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <libusb-1.0/libusb.h>

#include "hf_protocol.h"
#include "hfusb.h"



#define HF_CONFIGURATION       1
#define HF_INTERFACE           1
#define HF_EP_BULKOUT       0x02
#define HF_EP_BULKIN        0x81

#define CONTROL_TIMEOUT      100
#define BULKIN_TIMEOUT       100
#define BULKOUT_TIMEOUT      100

#if defined(LIBUSBX_API_VERSION) && (LIBUSBX_API_VERSION >= 0x01000102)
#define CONFIG_HOTPLUG
#endif

struct hfusbS {
    libusb_context *libusbContext;
    pthread_t usbEventThread;
    pthread_mutex_t mutex;
    int die;
#ifdef CONFIG_HOTPLUG
    libusb_hotplug_callback_handle hotplugHandle;
#endif
    void (*callback)(hfusbCallbackEventT *event);
    void *user;
};

struct hfusbDevS {
    hfusbT *context;
    libusb_device_handle *dev;
    pthread_mutex_t mutex;
    pthread_mutex_t controlMutex;
    pthread_mutex_t bulkOutMutex;
    pthread_cond_t cond;
    int controlTransferStatus;
    int controlTransferQueued;
    int bulkOutTransferStatus;
    int bulkOutTransferQueued;
    int bulkInTransferQueued;
    int closing;
    unsigned char controlBuffer[LIBUSB_CONTROL_SETUP_SIZE + 64];
    unsigned char bulkInBuffer[64];
    struct libusb_transfer *control;
    struct libusb_transfer *bulkIn;
    struct libusb_transfer *bulkOut;
};



#ifdef CONFIG_HOTPLUG
static int hotplugCallback(libusb_context *ctx, libusb_device *device,
                           libusb_hotplug_event event, void *user_data) {
    hfusbT *context;
    hfusbCallbackEventT e;

    context = (hfusbT *) user_data;
    if (context->callback) {
        e.user = context->user;
        e.type = hfusbHotplugEvent;
        e.v.hotplug.bus = libusb_get_bus_number(device);
#if defined(LIBUSBX_API_VERSION) && (LIBUSBX_API_VERSION >= 0x01000102)
        e.v.hotplug.port = libusb_get_port_number(device);
#else
        e.v.hotplug.port = -1;
#endif
        e.v.hotplug.addr = libusb_get_device_address(device);
        switch (event) {
        case LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED:
            e.v.hotplug.arrived = 1;
            context->callback(&e);
            break;
        case LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT:
            e.v.hotplug.arrived = 0;
            context->callback(&e);
            break;
        }
    }

    return 0;
}
#endif

static void controlTransferCallback(struct libusb_transfer *transfer) {
    hfusbDevT *dev;

    dev = (hfusbDevT *) transfer->user_data;
    pthread_mutex_lock(&dev->mutex);
    switch (transfer->status) {
    case LIBUSB_TRANSFER_COMPLETED:
        dev->controlTransferStatus = transfer->actual_length;
        break;
    default:
        dev->controlTransferStatus = -transfer->status;
        break;
    }
    dev->controlTransferQueued = 0;
    pthread_cond_signal(&dev->cond);
    pthread_mutex_unlock(&dev->mutex);
}

static void bulkOutTransferCallback(struct libusb_transfer *transfer) {
    hfusbDevT *dev;

    dev = (hfusbDevT *) transfer->user_data;
    pthread_mutex_lock(&dev->mutex);
    switch (transfer->status) {
    case LIBUSB_TRANSFER_COMPLETED:
        dev->bulkOutTransferStatus = transfer->actual_length;
        break;
    default:
        dev->bulkOutTransferStatus = -transfer->status;
        break;
    }
    dev->bulkOutTransferQueued = 0;
    pthread_cond_signal(&dev->cond);
    pthread_mutex_unlock(&dev->mutex);
}

static void bulkInTransferCallback(struct libusb_transfer *transfer) {
    hfusbDevT *dev;
    hfusbCallbackEventT event;
    int resubmit;

    dev = (hfusbDevT *) transfer->user_data;
    resubmit = 0;
    event.user = dev->context->user;
    switch (transfer->status) {
    case LIBUSB_TRANSFER_NO_DEVICE:
        if (dev->context->callback) {
            event.type = hfusbRemovedEvent;
            dev->context->callback(&event);
        }
        break;
    case LIBUSB_TRANSFER_COMPLETED:
    case LIBUSB_TRANSFER_TIMED_OUT:
        if (dev->context->callback && transfer->actual_length) {
            event.type = hfusbDataEvent;
            event.v.data.buffer = transfer->buffer;
            event.v.data.length = transfer->actual_length;
            dev->context->callback(&event);
        }
        resubmit = 1;
        break;
    default:
        if (dev->context->callback) {
            event.type = hfusbTransferErrorEvent;
            event.v.error = transfer->status;
            dev->context->callback(&event);
        }
        break;
    }

    pthread_mutex_lock(&dev->mutex);
    if (!resubmit || dev->closing || libusb_submit_transfer(transfer)) {
        dev->bulkInTransferQueued = 0;
        pthread_cond_signal(&dev->cond);
    }
    pthread_mutex_unlock(&dev->mutex);
}

static void *usbEventLoop(void *ptr) {
    hfusbT *context;
    struct timeval tv;

    context = (hfusbT *) ptr;

    pthread_mutex_lock(&context->mutex);
    while (!context->die) {
        pthread_mutex_unlock(&context->mutex);
        tv.tv_sec = 0;
        tv.tv_usec = 100000;
        libusb_handle_events_timeout(context->libusbContext, &tv);
        pthread_mutex_lock(&context->mutex);
    }
    pthread_mutex_unlock(&context->mutex);

    return NULL;
}

int hfusbInit(hfusbT **context,
              void (*callback)(hfusbCallbackEventT *event), void *user) {
    int result;
    int usbInitialized;
    int mutexInitialized;
    int hotplugRegistered;
    pthread_attr_t attr;

    result = HFUSB_SUCCESS;
    usbInitialized = 0;
    mutexInitialized = 0;
    hotplugRegistered = 0;

    if (context == NULL)
        result = HFUSB_ERROR_INVALID_PARAM;

    if (result == HFUSB_SUCCESS) {
        *context = calloc(1, sizeof(hfusbT));
        if (*context == NULL)
            result = HFUSB_ERROR_NO_MEM;
    }

    if (result == HFUSB_SUCCESS) {
        result = libusb_init(&(*context)->libusbContext);
    }

    if (result == HFUSB_SUCCESS) {
        usbInitialized = 1;
        libusb_set_debug((*context)->libusbContext, 0);
        if (pthread_mutex_init(&(*context)->mutex, NULL))
            result = HFUSB_ERROR_THREAD;
    }

    if (result == HFUSB_SUCCESS) {
        mutexInitialized = 1;
        if (pthread_attr_init(&attr))
            result = HFUSB_ERROR_THREAD;
        else
            pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    }

#ifdef CONFIG_HOTPLUG
    if (result == HFUSB_SUCCESS) {
        result = libusb_hotplug_register_callback(
                     (*context)->libusbContext,
                     LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED |
                     LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT,
                     LIBUSB_HOTPLUG_ENUMERATE,
                     HF_USB_VENDOR_ID,
                     HF_USB_PRODUCT_ID_G1,
                     LIBUSB_HOTPLUG_MATCH_ANY,
                     hotplugCallback,
                     *context,
                     &(*context)->hotplugHandle);
        if (result)
            hotplugRegistered = 1;
    }
#endif

    if (result == HFUSB_SUCCESS) {
        (*context)->die = 0;
        if (pthread_create(&(*context)->usbEventThread, &attr, usbEventLoop,
                           *context))
            result = HFUSB_ERROR_THREAD;
        pthread_attr_destroy(&attr);
    }

    if (result == HFUSB_SUCCESS) {
        (*context)->user = user;
        (*context)->callback = callback;
    }

    if (result && context && *context) {
#ifdef CONFIG_HOTPLUG
        if (hotplugRegistered)
            libusb_hotplug_deregister_callback((*context)->libusbContext,
                                               (*context)->hotplugHandle);
#endif
        if (usbInitialized)
            libusb_exit((*context)->libusbContext);
        if (mutexInitialized)
            pthread_mutex_destroy(&(*context)->mutex);
        free(*context);
        *context = NULL;
    }

    return result;
}

int hfusbOpen(hfusbT *context, hfusbDevT **dev, int addr, int bus, int port) {
    int result;
    libusb_device **devices;
    struct libusb_device_descriptor desc;
    int interfaceClaimed;
    int mutexInitialized;
    int controlMutexInitialized;
    int bulkOutMutexInitialized;
    int condInitialized;
    int count;
    int i, j;

    result = HFUSB_SUCCESS;
    interfaceClaimed = 0;
    mutexInitialized = 0;
    controlMutexInitialized = 0;
    bulkOutMutexInitialized = 0;
    condInitialized = 0;
    if (context == NULL || dev == NULL)
        result = HFUSB_ERROR_INVALID_PARAM;

    if (result == HFUSB_SUCCESS) {
        *dev = (hfusbDevT *) calloc(1, sizeof(hfusbDevT));
        if (*dev == NULL)
            result = HFUSB_ERROR_NO_MEM;
    }

    if (result == HFUSB_SUCCESS) {
        (*dev)->context = context;
        if (pthread_mutex_init(&(*dev)->mutex, NULL))
            result = HFUSB_ERROR_THREAD;
    }

    if (result == HFUSB_SUCCESS) {
        mutexInitialized = 1;
        if (pthread_mutex_init(&(*dev)->controlMutex, NULL))
            result = HFUSB_ERROR_THREAD;
    }

    if (result == HFUSB_SUCCESS) {
        controlMutexInitialized = 1;
        if (pthread_mutex_init(&(*dev)->bulkOutMutex, NULL))
            result = HFUSB_ERROR_THREAD;
    }

    if (result == HFUSB_SUCCESS) {
        bulkOutMutexInitialized = 1;
        if (pthread_cond_init(&(*dev)->cond, NULL))
            result = HFUSB_ERROR_THREAD;
    }

    if (result == HFUSB_SUCCESS) {
        condInitialized = 1;
        (*dev)->control = libusb_alloc_transfer(0);
        if ((*dev)->control == NULL)
            result = HFUSB_ERROR_NO_MEM;
    }

    if (result == HFUSB_SUCCESS) {
        (*dev)->bulkIn = libusb_alloc_transfer(0);
        if ((*dev)->bulkIn == NULL)
            result = HFUSB_ERROR_NO_MEM;
    }

    if (result == HFUSB_SUCCESS) {
        (*dev)->bulkOut = libusb_alloc_transfer(0);
        if ((*dev)->bulkOut == NULL)
            result = HFUSB_ERROR_NO_MEM;
    }

    if (result == HFUSB_SUCCESS) {
        count = libusb_get_device_list(context->libusbContext, &devices);
        if (count < 0)
            result = count;
    }

    if (result == HFUSB_SUCCESS) {
        for (i = 0, j = -1; i < count && result == HFUSB_SUCCESS; i++) {
            if ((bus < 0 || bus == libusb_get_bus_number(devices[i])) &&
#if defined(LIBUSBX_API_VERSION) && (LIBUSBX_API_VERSION >= 0x01000102)
                (port < 0 || port == libusb_get_port_number(devices[i])) &&
#endif
                (addr < 0 || addr == libusb_get_device_address(devices[i])) &&
                libusb_get_device_descriptor(devices[i], &desc) ==
                LIBUSB_SUCCESS) {
                if (desc.idVendor == HF_USB_VENDOR_ID &&
                    desc.idProduct == HF_USB_PRODUCT_ID_G1) {
                    if (j >= 0) {
                        libusb_free_device_list(devices, 1);
                        result = HFUSB_ERROR_AMBIGUOUS_DEVICE;
                    } else
                        j = i;
                }
            }
        }
    }
    if (result == HFUSB_SUCCESS && j < 0) {
        libusb_free_device_list(devices, 1);
        result = HFUSB_ERROR_NO_DEVICE;
    }

    if (result == HFUSB_SUCCESS) {
        result = libusb_open(devices[j], &(*dev)->dev);
        libusb_free_device_list(devices, 1);
    }

    if (result == HFUSB_SUCCESS) {
        libusb_detach_kernel_driver((*dev)->dev, HF_INTERFACE);
        /* BISON this seems to result in the atmel usb stuff resending some
           old data - should investigate to confirm that asf is at fault and
           not some other fw bug, but for the moment we just won't set the
           configuration (it should not be necessary, the device only has
           one config)
        result = libusb_set_configuration((*dev)->dev, HF_CONFIGURATION);
        */
    }

    if (result == HFUSB_SUCCESS) {
        result = libusb_claim_interface((*dev)->dev, HF_INTERFACE);
        if (result == LIBUSB_SUCCESS)
            interfaceClaimed = 1;
    }

    if (result == HFUSB_SUCCESS) {
        libusb_fill_bulk_transfer((*dev)->bulkIn,
                                  (*dev)->dev,
                                  HF_EP_BULKIN,
                                  (*dev)->bulkInBuffer,
                                  sizeof((*dev)->bulkInBuffer),
                                  bulkInTransferCallback,
                                  *dev,
                                  BULKIN_TIMEOUT);
        pthread_mutex_lock(&(*dev)->mutex);
        (*dev)->bulkInTransferQueued = 1;
        pthread_mutex_unlock(&(*dev)->mutex);
        result = libusb_submit_transfer((*dev)->bulkIn);
        if (result) {
            pthread_mutex_lock(&(*dev)->mutex);
            (*dev)->bulkInTransferQueued = 0;
            pthread_cond_signal(&(*dev)->cond);
            pthread_mutex_unlock(&(*dev)->mutex);
        }
    }

    if (result && dev && *dev) {
        if (interfaceClaimed)
            libusb_release_interface((*dev)->dev, HF_INTERFACE);
        if ((*dev)->dev) {
            pthread_mutex_lock(&(*dev)->mutex);
            (*dev)->closing = 1;
            while ((*dev)->bulkInTransferQueued)
                pthread_cond_wait(&(*dev)->cond, &(*dev)->mutex);
            pthread_mutex_unlock(&(*dev)->mutex);
            libusb_close((*dev)->dev);
        }
        if (controlMutexInitialized)
            pthread_mutex_destroy(&(*dev)->controlMutex);
        if (bulkOutMutexInitialized)
            pthread_mutex_destroy(&(*dev)->bulkOutMutex);
        if (condInitialized)
            pthread_cond_destroy(&(*dev)->cond);
        if (mutexInitialized)
            pthread_mutex_destroy(&(*dev)->mutex);
        if ((*dev)->control)
            libusb_free_transfer((*dev)->control);
        if ((*dev)->bulkIn)
            libusb_free_transfer((*dev)->bulkIn);
        if ((*dev)->bulkOut)
            libusb_free_transfer((*dev)->bulkOut);
        free(*dev);
        *dev = NULL;
    }

    return result;
}

int hfusbControl(hfusbDevT *dev, uint8_t type, uint8_t request,
                 uint16_t value, uint16_t index,
                 void *buffer, unsigned int length) {
    int result;

    result = HFUSB_SUCCESS;
    if (dev == NULL || (length && buffer == NULL) ||
        length > sizeof(dev->controlBuffer) - LIBUSB_CONTROL_SETUP_SIZE)
        result = HFUSB_ERROR_INVALID_PARAM;

    if (result == HFUSB_SUCCESS) {
        pthread_mutex_lock(&dev->controlMutex);
        pthread_mutex_lock(&dev->mutex);
        dev->controlTransferQueued = 1;
        pthread_mutex_unlock(&dev->mutex);
        libusb_fill_control_setup(dev->controlBuffer, type, request,
                                  value, index, length);
        if (length &&
            (type & LIBUSB_ENDPOINT_DIR_MASK) == LIBUSB_ENDPOINT_OUT)
            memcpy(&dev->controlBuffer[LIBUSB_CONTROL_SETUP_SIZE], buffer,
                   length);
        libusb_fill_control_transfer(dev->control, dev->dev,
                                     dev->controlBuffer,
                                     controlTransferCallback, dev,
                                     CONTROL_TIMEOUT);
        result = libusb_submit_transfer(dev->control);
        if (result == HFUSB_SUCCESS) {
            pthread_mutex_lock(&dev->mutex);
            while (dev->controlTransferQueued)
                pthread_cond_wait(&dev->cond, &dev->mutex);
            result = dev->controlTransferStatus;
            pthread_mutex_unlock(&dev->mutex);
            if (result > 0 && length &&
                (type & LIBUSB_ENDPOINT_DIR_MASK) == LIBUSB_ENDPOINT_IN)
                memcpy(buffer, &dev->controlBuffer[LIBUSB_CONTROL_SETUP_SIZE],
                       length);
        }
        pthread_mutex_unlock(&dev->controlMutex);
    }

    return result;
}

int hfusbWrite(hfusbDevT *dev, void *buffer, unsigned int length) {
    int result;

    result = HFUSB_SUCCESS;
    if (dev == NULL || (length && buffer == NULL))
        result = HFUSB_ERROR_INVALID_PARAM;

    if (result == HFUSB_SUCCESS) {
        pthread_mutex_lock(&dev->bulkOutMutex);
        pthread_mutex_lock(&dev->mutex);
        dev->bulkOutTransferQueued = 1;
        pthread_mutex_unlock(&dev->mutex);
        libusb_fill_bulk_transfer(dev->bulkOut, dev->dev, HF_EP_BULKOUT,
                                  buffer, length,
                                  bulkOutTransferCallback, dev,
                                  BULKOUT_TIMEOUT);
        result = libusb_submit_transfer(dev->bulkOut);
        if (result == HFUSB_SUCCESS) {
            pthread_mutex_lock(&dev->mutex);
            while (dev->bulkOutTransferQueued)
                pthread_cond_wait(&dev->cond, &dev->mutex);
            result = dev->bulkOutTransferStatus;
            pthread_mutex_unlock(&dev->mutex);
        }
        pthread_mutex_unlock(&dev->bulkOutMutex);
    }

    return result;
}

void hfusbClose(hfusbDevT *dev) {

    if (dev) {
        pthread_mutex_lock(&dev->mutex);
        dev->closing = 1;
        while (dev->bulkInTransferQueued)
            pthread_cond_wait(&dev->cond, &dev->mutex);
        pthread_mutex_unlock(&dev->mutex);
        libusb_release_interface(dev->dev, HF_INTERFACE);
        libusb_close(dev->dev);
        pthread_mutex_destroy(&dev->controlMutex);
        pthread_mutex_destroy(&dev->bulkOutMutex);
        pthread_mutex_destroy(&dev->mutex);
        pthread_cond_destroy(&dev->cond);
        if (dev->control)
            libusb_free_transfer(dev->control);
        if (dev->bulkIn)
            libusb_free_transfer(dev->bulkIn);
        if (dev->bulkOut)
            libusb_free_transfer(dev->bulkOut);

        free(dev);
    }
}

void hfusbFinish(hfusbT *context) {

    if (context) {
#ifdef CONFIG_HOTPLUG
        libusb_hotplug_deregister_callback(context->libusbContext,
                                           context->hotplugHandle);
#endif
        pthread_mutex_lock(&context->mutex);
        context->die = 1;
        pthread_mutex_unlock(&context->mutex);

        pthread_join(context->usbEventThread, NULL);
        libusb_exit(context->libusbContext);
        pthread_mutex_destroy(&context->mutex);
        free(context);
    }
}

