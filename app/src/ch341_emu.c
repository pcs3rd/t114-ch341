// CH341A (1a86:5512, synchronous serial mode) emulation for the nRF52840 on a Heltec T114.
//
// The host side is meshtasticd's libpinedio-usb (github.com/meshtastic/libch341-spi-userspace)
// over libusb. It speaks the CH341 "stream" protocol on bulk EP 0x02 / 0x82 with one command
// per 32-byte USB packet:
//
//   0xA8 d1..dN     SPI stream: clock N (<=31) bytes out LSB-first, return N bytes read
//   0xAB ...  0x20  UIO stream: 0x80|v = set D0-D5 levels, 0x40|v = set D0-D5 directions,
//                   0x00 = read D0-D7 (1 byte back), 0xC0|us = delay, 0x20 = end of stream.
//                   Bytes after 0x20 in the same packet are ignored (libpinedio pads to 32).
//   0xA0            Get status: return 6 bytes, byte 0 = D0-D7 levels
//
// Each response goes back as its own short (<32 byte) IN packet, which is what ends each of the
// host's IN transfers. That is why this is a raw class driver and not TinyUSB's vendor class:
// the vendor class turns both directions into byte streams and loses the packet boundaries.

#include <string.h>
#include "tusb.h"
#include "device/usbd_pvt.h"
#include "ch341_emu.h"
#include "t114_hw.h"

#define CMD_SPI_STREAM   0xA8
#define CMD_UIO_STREAM   0xAB
#define CMD_GET_STATUS   0xA0
#define UIO_STM_END      0x20
#define UIO_STM_DIR      0x40
#define UIO_STM_OUT      0x80
#define UIO_STM_US       0xC0

#define RING_SLOTS 8 // power of two

typedef struct {
  uint8_t len;
  uint8_t data[CH341_PACKET_LEN];
} pkt_t;

typedef struct {
  pkt_t slot[RING_SLOTS];
  uint8_t head, tail; // free-running
} ring_t;

static inline bool ring_empty(ring_t const *r) { return r->head == r->tail; }
static inline bool ring_full(ring_t const *r) { return (uint8_t)(r->head - r->tail) >= RING_SLOTS; }
static inline pkt_t *ring_head(ring_t *r) { return &r->slot[r->head & (RING_SLOTS - 1)]; }
static inline pkt_t *ring_tail(ring_t *r) { return &r->slot[r->tail & (RING_SLOTS - 1)]; }

static ring_t rx_ring; // OUT packets from the host, waiting to be executed
static ring_t tx_ring; // responses waiting to go out on EP 0x82

CFG_TUSB_MEM_ALIGN static uint8_t ep_out_buf[CH341_PACKET_LEN];
CFG_TUSB_MEM_ALIGN static uint8_t ep_in_buf[CH341_PACKET_LEN];

static uint8_t rhport_used;
static bool opened;
static bool out_armed;

// D0..D5 shadow state, as the CH341 holds it
static uint8_t d_state;
static uint8_t d_dir;

static uint32_t led_next_ms;
static bool activity;

//--------------------------------------------------------------------+
// Command execution: one OUT packet in, at most one response packet out
//--------------------------------------------------------------------+
static void execute(pkt_t const *in, pkt_t *out) {
  out->len = 0;
  if (in->len == 0) return;

  switch (in->data[0]) {
    case CMD_SPI_STREAM: {
      uint8_t n = in->len - 1;
      if (n) {
        hw_spi_xfer(&in->data[1], out->data, n);
        out->len = n;
        activity = true;
      }
      break;
    }

    case CMD_UIO_STREAM:
      for (uint8_t i = 1; i < in->len; i++) {
        uint8_t b = in->data[i];
        uint8_t top = b & 0xC0;
        if (top == UIO_STM_OUT) {
          d_state = b & 0x3F;
          hw_apply_pins(d_state, d_dir);
        } else if (top == UIO_STM_DIR) {
          d_dir = b & 0x3F;
          hw_apply_pins(d_state, d_dir);
        } else if (top == UIO_STM_US) {
          hw_delay_us(b & 0x3F);
        } else if (b & UIO_STM_END) { // 0x20..0x3F
          break;
        } else { // 0x00..0x1F: UIO_STM_IN
          if (out->len < CH341_PACKET_LEN - 1) out->data[out->len++] = hw_read_pins(d_state, d_dir);
        }
      }
      break;

    case CMD_GET_STATUS:
      memset(out->data, 0, 6);
      out->data[0] = hw_read_pins(d_state, d_dir);
      out->len = 6;
      break;

    default:
      // I2C stream (speed setup from flashrom-style hosts), print/parallel modes, etc: accept and ignore.
      break;
  }
}

//--------------------------------------------------------------------+
// Endpoint plumbing (all in tud_task context)
//--------------------------------------------------------------------+
static void arm_out(void) {
  if (!opened || out_armed || ring_full(&rx_ring)) return;
  if (usbd_edpt_xfer(rhport_used, CH341_EP_OUT, ep_out_buf, CH341_PACKET_LEN, false)) out_armed = true;
}

static void kick_in(void) {
  if (!opened || ring_empty(&tx_ring) || usbd_edpt_busy(rhport_used, CH341_EP_IN)) return;
  pkt_t *p = ring_tail(&tx_ring);
  memcpy(ep_in_buf, p->data, p->len);
  if (usbd_edpt_xfer(rhport_used, CH341_EP_IN, ep_in_buf, p->len, false)) tx_ring.tail++;
}

static void drv_init(void) {}

static bool drv_deinit(void) { return true; }

static void drv_reset(uint8_t rhport) {
  (void)rhport;
  opened = false;
  out_armed = false;
  rx_ring.head = rx_ring.tail = 0;
  tx_ring.head = tx_ring.tail = 0;
  d_state = 0;
  d_dir = 0;
  hw_apply_pins(d_state, d_dir);
}

static uint16_t drv_open(uint8_t rhport, tusb_desc_interface_t const *itf, uint16_t max_len) {
  if (itf->bInterfaceClass != TUSB_CLASS_VENDOR_SPECIFIC || itf->bInterfaceNumber != 0) return 0;
  uint16_t const len = sizeof(tusb_desc_interface_t) + itf->bNumEndpoints * sizeof(tusb_desc_endpoint_t);
  TU_VERIFY(max_len >= len, 0);

  uint8_t const *p = tu_desc_next(itf);
  for (uint8_t i = 0; i < itf->bNumEndpoints; i++) {
    TU_ASSERT(tu_desc_type(p) == TUSB_DESC_ENDPOINT, 0);
    TU_ASSERT(usbd_edpt_open(rhport, (tusb_desc_endpoint_t const *)p), 0);
    p = tu_desc_next(p);
  }

  rhport_used = rhport;
  opened = true;
  out_armed = false;
  arm_out();
  return len;
}

static bool drv_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request) {
  (void)rhport; (void)stage; (void)request;
  return false; // nothing interface-specific; stall
}

static bool drv_xfer_cb(uint8_t rhport, uint8_t ep_addr, xfer_result_t result, uint32_t xferred_bytes) {
  (void)rhport;
  if (ep_addr == CH341_EP_OUT) {
    out_armed = false;
    if (result == XFER_RESULT_SUCCESS) {
      pkt_t *p = ring_head(&rx_ring);
      p->len = (uint8_t)TU_MIN(xferred_bytes, CH341_PACKET_LEN);
      memcpy(p->data, ep_out_buf, p->len);
      rx_ring.head++;
    }
    arm_out();
  } else if (ep_addr == CH341_EP_IN) {
    kick_in();
  }
  return true;
}

static usbd_class_driver_t const ch341_driver = {
    .name = "CH341",
    .init = drv_init,
    .deinit = drv_deinit,
    .reset = drv_reset,
    .open = drv_open,
    .control_xfer_cb = drv_control_xfer_cb,
    .xfer_cb = drv_xfer_cb,
    .xfer_isr = NULL,
    .sof = NULL,
};

usbd_class_driver_t const *usbd_app_driver_get_cb(uint8_t *driver_count) {
  *driver_count = 1;
  return &ch341_driver;
}

//--------------------------------------------------------------------+
// Device-level vendor control requests
//--------------------------------------------------------------------+
CFG_TUSB_MEM_ALIGN static uint8_t ctrl_buf[64];

bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *req) {
  // Our own request: reboot into the UF2 bootloader
  if (req->bRequest == CH341_EMU_REQ_BOOTLOADER && req->wValue == CH341_EMU_BOOTLOADER_MAGIC &&
      req->bmRequestType_bit.direction == TUSB_DIR_OUT) {
    if (stage == CONTROL_STAGE_SETUP) return tud_control_status(rhport, req);
    if (stage == CONTROL_STAGE_ACK) {
      hw_enter_bootloader();
    }
    return true;
  }

  // Anything else a CH341 driver might send (0x5F version query, 0xA1 init, ...): answer politely
  if (stage != CONTROL_STAGE_SETUP) return true;
  uint16_t n = TU_MIN(req->wLength, sizeof(ctrl_buf));
  memset(ctrl_buf, 0, sizeof(ctrl_buf));
  if (req->bmRequestType_bit.direction == TUSB_DIR_IN) {
    if (req->bRequest == 0x5F && n >= 2) { ctrl_buf[0] = 0x30; ctrl_buf[1] = 0x00; } // chip version
    return tud_control_xfer(rhport, req, ctrl_buf, n);
  }
  if (n == 0) return tud_control_status(rhport, req);
  return tud_control_xfer(rhport, req, ctrl_buf, n);
}

//--------------------------------------------------------------------+
// Public
//--------------------------------------------------------------------+
void ch341_emu_init(void) {
  d_state = 0;
  d_dir = 0;
  hw_init();
}

void ch341_emu_task(void) {
  // Execute queued commands while there is room for their responses
  while (!ring_empty(&rx_ring) && !ring_full(&tx_ring)) {
    pkt_t *resp = ring_head(&tx_ring);
    execute(ring_tail(&rx_ring), resp);
    rx_ring.tail++;
    if (resp->len) tx_ring.head++;
  }
  arm_out();
  kick_in();

  // LED: on while configured, blinks off briefly on SPI traffic
  uint32_t now = hw_millis();
  if (activity) {
    activity = false;
    hw_led(false);
    led_next_ms = now + 15;
  } else if ((int32_t)(now - led_next_ms) >= 0) {
    hw_led(tud_mounted());
    led_next_ms = now + 100;
  }
}
