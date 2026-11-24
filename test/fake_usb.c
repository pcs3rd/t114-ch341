// A fake USB bus: the libusb API that libpinedio calls on one side, the TinyUSB device API that
// ch341_emu.c calls on the other. OUT transfers are cut into wMaxPacketSize packets and handed to
// the class driver one at a time, only while it has an OUT transfer armed; IN packets the device
// queues complete the host's IN transfers with real short-packet semantics.

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tusb.h"
#include "libusb-1.0/libusb.h"
#include "nrf.h"

mock_ficr_t mock_ficr = {{0x12345678u, 0x9ABCDEF0u}};

static usbd_class_driver_t const *drv;
static uint16_t mps[256]; // wMaxPacketSize by endpoint address
static bool configured;

// device-side endpoint state
static uint8_t *dev_out_buf; static uint16_t dev_out_len; static bool dev_out_armed;
static uint8_t dev_in_pkt[64]; static uint16_t dev_in_len; static bool dev_in_busy;

// host-side queues
#define QMAX 64
static struct libusb_transfer *outq[QMAX]; static int outq_n; static int out_off;
static struct libusb_transfer *inq[QMAX]; static int inq_n;

static pthread_mutex_t bus = PTHREAD_MUTEX_INITIALIZER;
unsigned long stat_out_pkts, stat_in_pkts;

void ch341_emu_task(void);

//--------------------------------------------------------------------+ device side
bool usbd_edpt_open(uint8_t rhport, tusb_desc_endpoint_t const *ep) {
  (void)rhport;
  mps[ep->bEndpointAddress] = ep->wMaxPacketSize;
  return true;
}
bool usbd_edpt_xfer(uint8_t rhport, uint8_t ep, uint8_t *buf, uint16_t n, bool isr) {
  (void)rhport; (void)isr;
  if (ep & 0x80) {
    if (dev_in_busy) { fprintf(stderr, "FAKE: IN xfer while busy\n"); abort(); }
    if (n > mps[ep]) { fprintf(stderr, "FAKE: multi-packet IN not modelled (%u)\n", n); abort(); }
    memcpy(dev_in_pkt, buf, n); dev_in_len = n; dev_in_busy = true;
  } else {
    if (dev_out_armed) { fprintf(stderr, "FAKE: OUT re-armed while armed\n"); abort(); }
    dev_out_buf = buf; dev_out_len = n; dev_out_armed = true;
  }
  return true;
}
bool usbd_edpt_busy(uint8_t rhport, uint8_t ep) { (void)rhport; return (ep & 0x80) ? dev_in_busy : dev_out_armed; }
bool tud_control_status(uint8_t r, tusb_control_request_t const *q) { (void)r; (void)q; return true; }
bool tud_control_xfer(uint8_t r, tusb_control_request_t const *q, void *b, uint16_t l) { (void)r; (void)q; (void)b; (void)l; return true; }
bool tud_mounted(void) { return configured; }

static void enumerate(void) {
  uint8_t n;
  drv = usbd_app_driver_get_cb(&n);
  drv->init();
  drv->reset(0);
  uint8_t const *cfg = tud_descriptor_configuration_cb(0);
  uint16_t total = cfg[2] | (cfg[3] << 8);
  uint16_t used = drv->open(0, (tusb_desc_interface_t const *)(cfg + 9), total - 9);
  if (used != total - 9) { fprintf(stderr, "FAKE: driver claimed %u of %u descriptor bytes\n", used, total - 9); abort(); }
  if (mps[0x02] != 32 || mps[0x82] != 32) { fprintf(stderr, "FAKE: bulk MPS must be 32 (got %u/%u)\n", mps[0x02], mps[0x82]); abort(); }
  configured = true;
}

//--------------------------------------------------------------------+ the bus
static void finish(struct libusb_transfer *t, enum libusb_transfer_status st) {
  t->status = st;
  t->callback(t);
}

static bool pump_once(void) {
  bool progress = false;

  // host OUT -> device, one packet at a time, only when the device has room
  while (outq_n && dev_out_armed) {
    struct libusb_transfer *t = outq[0];
    int chunk = TU_MIN(t->length - out_off, (int)mps[0x02]);
    if (chunk > dev_out_len) { fprintf(stderr, "FAKE: OUT packet larger than armed buffer\n"); abort(); }
    memcpy(dev_out_buf, t->buffer + out_off, chunk);
    out_off += chunk;
    dev_out_armed = false;
    stat_out_pkts++;
    drv->xfer_cb(0, 0x02, XFER_RESULT_SUCCESS, chunk);
    if (out_off >= t->length) {
      memmove(outq, outq + 1, sizeof(outq[0]) * --outq_n);
      out_off = 0;
      t->actual_length = t->length;
      finish(t, LIBUSB_TRANSFER_COMPLETED);
    }
    progress = true;
  }

  ch341_emu_task();

  // device IN -> host
  if (dev_in_busy && inq_n) {
    struct libusb_transfer *t = inq[0];
    if (t->actual_length + dev_in_len > t->length) {
      fprintf(stderr, "FAKE: babble - device sent %u bytes into an IN transfer with %d left\n", dev_in_len,
              t->length - t->actual_length);
      abort();
    }
    memcpy(t->buffer + t->actual_length, dev_in_pkt, dev_in_len);
    t->actual_length += dev_in_len;
    bool short_pkt = dev_in_len < mps[0x82];
    dev_in_busy = false;
    stat_in_pkts++;
    if (short_pkt || t->actual_length == t->length) {
      memmove(inq, inq + 1, sizeof(inq[0]) * --inq_n);
      finish(t, LIBUSB_TRANSFER_COMPLETED);
    }
    drv->xfer_cb(0, 0x82, XFER_RESULT_SUCCESS, dev_in_len);
    progress = true;
  }
  return progress;
}

int libusb_handle_events_timeout(libusb_context *ctx, struct timeval *tv) {
  (void)ctx; (void)tv;
  pthread_mutex_lock(&bus);
  static unsigned long idle;
  if (pump_once()) idle = 0;
  else if (++idle > 2000000) {
    fprintf(stderr, "FAKE: bus stalled. outq=%d inq=%d dev_out_armed=%d dev_in_busy=%d\n", outq_n, inq_n,
            dev_out_armed, dev_in_busy);
    abort();
  }
  pthread_mutex_unlock(&bus);
  return 0;
}

//--------------------------------------------------------------------+ libusb API
static int the_device;
static libusb_device *devlist[2] = {(libusb_device *)&the_device, NULL};

int libusb_init(libusb_context **ctx) { (void)ctx; enumerate(); return 0; }
int libusb_set_option(libusb_context *ctx, enum libusb_option o, ...) { (void)ctx; (void)o; return 0; }
ssize_t libusb_get_device_list(libusb_context *ctx, libusb_device ***list) { (void)ctx; *list = devlist; return 1; }
void libusb_free_device_list(libusb_device **l, int u) { (void)l; (void)u; }
int libusb_get_device_descriptor(libusb_device *dev, struct libusb_device_descriptor *d) {
  (void)dev;
  tusb_desc_device_t const *s = (tusb_desc_device_t const *)tud_descriptor_device_cb();
  memset(d, 0, sizeof(*d));
  d->bLength = s->bLength; d->bDescriptorType = s->bDescriptorType; d->idVendor = s->idVendor;
  d->idProduct = s->idProduct; d->iProduct = s->iProduct; d->iSerialNumber = s->iSerialNumber;
  d->iManufacturer = s->iManufacturer;
  return 0;
}
int libusb_open(libusb_device *dev, libusb_device_handle **h) { (void)dev; *h = (libusb_device_handle *)&the_device; return 0; }
void libusb_close(libusb_device_handle *h) { (void)h; }
int libusb_detach_kernel_driver(libusb_device_handle *h, int i) { (void)h; (void)i; return LIBUSB_ERROR_NOT_FOUND; }
int libusb_attach_kernel_driver(libusb_device_handle *h, int i) { (void)h; (void)i; return 0; }
int libusb_claim_interface(libusb_device_handle *h, int i) { (void)h; return i == 0 ? 0 : -1; }
int libusb_release_interface(libusb_device_handle *h, int i) { (void)h; (void)i; return 0; }

int libusb_get_string_descriptor(libusb_device_handle *h, uint8_t idx, uint16_t langid, unsigned char *data, int length) {
  (void)h;
  uint16_t const *s = tud_descriptor_string_cb(idx, langid);
  if (!s) return -1;
  int n = TU_MIN(length, (int)(s[0] & 0xff));
  memcpy(data, s, n);
  return n;
}
int libusb_get_string_descriptor_ascii(libusb_device_handle *h, uint8_t idx, unsigned char *data, int length) {
  (void)h;
  uint16_t const *s = tud_descriptor_string_cb(idx, 0x0409);
  if (!s) return -1;
  int chars = ((s[0] & 0xff) - 2) / 2, n = 0;
  for (; n < chars && n < length - 1; n++) data[n] = (unsigned char)s[1 + n];
  data[n] = 0;
  return n;
}

struct libusb_transfer *libusb_alloc_transfer(int iso) { (void)iso; return calloc(1, sizeof(struct libusb_transfer)); }
void libusb_free_transfer(struct libusb_transfer *t) { free(t); }

int libusb_submit_transfer(struct libusb_transfer *t) {
  pthread_mutex_lock(&bus);
  t->actual_length = 0;
  if (t->endpoint & 0x80) {
    if (inq_n == QMAX) abort();
    inq[inq_n++] = t;
  } else {
    if (outq_n == QMAX) abort();
    outq[outq_n++] = t;
  }
  pthread_mutex_unlock(&bus);
  return 0;
}
int libusb_cancel_transfer(struct libusb_transfer *t) { (void)t; fprintf(stderr, "FAKE: cancel\n"); abort(); }
const char *libusb_error_name(int c) { (void)c; return "FAKE_ERROR"; }
