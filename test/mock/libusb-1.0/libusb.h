// Just enough of the libusb-1.0 API for libpinedio-usb.c, backed by fake_usb.c
#ifndef MOCK_LIBUSB_H
#define MOCK_LIBUSB_H
#include <stdint.h>
#include <sys/time.h>

#define LIBUSB_CALL
typedef struct libusb_context libusb_context;
typedef struct libusb_device libusb_device;
typedef struct libusb_device_handle libusb_device_handle;

enum { LIBUSB_SUCCESS = 0, LIBUSB_ERROR_IO = -1, LIBUSB_ERROR_NOT_FOUND = -5, LIBUSB_ERROR_OVERFLOW = -8 };
enum libusb_transfer_status {
  LIBUSB_TRANSFER_COMPLETED, LIBUSB_TRANSFER_ERROR, LIBUSB_TRANSFER_TIMED_OUT,
  LIBUSB_TRANSFER_CANCELLED, LIBUSB_TRANSFER_STALL, LIBUSB_TRANSFER_NO_DEVICE, LIBUSB_TRANSFER_OVERFLOW
};
enum libusb_option { LIBUSB_OPTION_LOG_LEVEL = 0 };
enum libusb_log_level { LIBUSB_LOG_LEVEL_NONE = 0, LIBUSB_LOG_LEVEL_ERROR, LIBUSB_LOG_LEVEL_WARNING, LIBUSB_LOG_LEVEL_INFO };

struct libusb_device_descriptor {
  uint8_t bLength, bDescriptorType; uint16_t bcdUSB;
  uint8_t bDeviceClass, bDeviceSubClass, bDeviceProtocol, bMaxPacketSize0;
  uint16_t idVendor, idProduct, bcdDevice;
  uint8_t iManufacturer, iProduct, iSerialNumber, bNumConfigurations;
};

struct libusb_transfer;
typedef void (*libusb_transfer_cb_fn)(struct libusb_transfer *transfer);
struct libusb_transfer {
  libusb_device_handle *dev_handle;
  uint8_t flags;
  unsigned char endpoint;
  unsigned char type;
  unsigned int timeout;
  enum libusb_transfer_status status;
  int length;
  int actual_length;
  libusb_transfer_cb_fn callback;
  void *user_data;
  unsigned char *buffer;
};

int libusb_init(libusb_context **ctx);
int libusb_set_option(libusb_context *ctx, enum libusb_option option, ...);
ssize_t libusb_get_device_list(libusb_context *ctx, libusb_device ***list);
void libusb_free_device_list(libusb_device **list, int unref_devices);
int libusb_get_device_descriptor(libusb_device *dev, struct libusb_device_descriptor *desc);
int libusb_open(libusb_device *dev, libusb_device_handle **handle);
void libusb_close(libusb_device_handle *handle);
int libusb_detach_kernel_driver(libusb_device_handle *h, int itf);
int libusb_attach_kernel_driver(libusb_device_handle *h, int itf);
int libusb_claim_interface(libusb_device_handle *h, int itf);
int libusb_release_interface(libusb_device_handle *h, int itf);
int libusb_get_string_descriptor_ascii(libusb_device_handle *h, uint8_t idx, unsigned char *data, int length);
int libusb_get_string_descriptor(libusb_device_handle *h, uint8_t idx, uint16_t langid, unsigned char *data, int length);
struct libusb_transfer *libusb_alloc_transfer(int iso_packets);
void libusb_free_transfer(struct libusb_transfer *t);
int libusb_submit_transfer(struct libusb_transfer *t);
int libusb_cancel_transfer(struct libusb_transfer *t);
int libusb_handle_events_timeout(libusb_context *ctx, struct timeval *tv);
const char *libusb_error_name(int code);

static inline void libusb_fill_bulk_transfer(struct libusb_transfer *t, libusb_device_handle *h, unsigned char ep,
                                             unsigned char *buf, int len, libusb_transfer_cb_fn cb, void *ud,
                                             unsigned int timeout) {
  t->dev_handle = h; t->endpoint = ep; t->type = 2; t->timeout = timeout;
  t->buffer = buf; t->length = len; t->user_data = ud; t->callback = cb;
}
#endif
