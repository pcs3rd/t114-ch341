#include <string.h>
#include "status.h"
#include "t114_hw.h"

// SX1262 opcodes (datasheet section 13)
#define OP_SET_SLEEP       0x84
#define OP_SET_STANDBY     0x80
#define OP_SET_FS          0xC1
#define OP_SET_TX          0x83
#define OP_SET_RX          0x82
#define OP_SET_CAD         0xC5
#define OP_GET_IRQ_STATUS  0x12

// IrqStatus bits
#define IRQ_TX_DONE   (1u << 0)
#define IRQ_RX_DONE   (1u << 1)
#define IRQ_CRC_ERR   (1u << 6)

#define HOST_IDLE_MS   2000u
#define FLASH_RX_MS    150u
#define FLASH_CRC_MS   250u

typedef struct { uint8_t r, g, b; } rgb_t;

static const rgb_t C_OFF    = {0, 0, 0};
static const rgb_t C_RED    = {255, 0, 0};
static const rgb_t C_AMBER  = {255, 110, 0};
static const rgb_t C_GREEN  = {0, 255, 0};
static const rgb_t C_BLUE   = {0, 40, 255};
static const rgb_t C_ORANGE = {255, 50, 0};

static uint8_t brightness = STATUS_DEFAULT_BRIGHTNESS;
static radio_state_t radio = RADIO_UNKNOWN;
static uint32_t last_host_ms;
static bool host_seen;

// Current CS window
static bool selected;
static uint32_t win_pos;
static uint8_t win_op;
static uint8_t win_miso[4];

// Flash overlay on the radio pixel
static rgb_t flash_color;
static uint32_t flash_until;
static bool flash_on;
static uint32_t now_cache;

static uint8_t shown[6];
static bool shown_valid;

// The CH341 clocks bytes LSB-first (libpinedio pre-reverses them), so what passes through the
// emulation is the bit-reverse of what the SX1262 actually sees and sends.
static inline uint8_t rev8(uint8_t x) {
  x = (uint8_t)(((x >> 1) & 0x55) | ((x << 1) & 0xAA));
  x = (uint8_t)(((x >> 2) & 0x33) | ((x << 2) & 0xCC));
  return (uint8_t)((x >> 4) | (x << 4));
}

static void flash(rgb_t c, uint32_t ms) {
  flash_color = c;
  flash_until = now_cache + ms;
  flash_on = true;
}

void status_init(void) {
  brightness = STATUS_DEFAULT_BRIGHTNESS;
  radio = RADIO_UNKNOWN;
  host_seen = false;
  selected = false;
  flash_on = false;
  shown_valid = false;
}

void status_set_brightness(uint8_t level) {
  brightness = level;
  shown_valid = false;
}

uint8_t status_get_brightness(void) { return brightness; }

void status_host_packet(uint32_t now_ms) {
  last_host_ms = now_ms;
  host_seen = true;
}

void status_cs(bool sel) {
  if (sel && !selected) {
    win_pos = 0;
    win_op = 0;
    memset(win_miso, 0, sizeof(win_miso));
  }
  selected = sel;
}

static void on_irq_flags(uint16_t irq) {
  if (irq & IRQ_CRC_ERR) {
    flash(C_ORANGE, FLASH_CRC_MS);
  } else if (irq & IRQ_RX_DONE) {
    flash(C_GREEN, FLASH_RX_MS);
  }
  // The SX1262 drops back to standby by itself after TxDone; meshtasticd follows with SetRx.
  if ((irq & IRQ_TX_DONE) && radio == RADIO_TX) radio = RADIO_STANDBY;
}

void status_spi(uint8_t const *mosi, uint8_t const *miso, uint8_t n, uint32_t now_ms) {
  now_cache = now_ms;
  if (!selected) return;
  for (uint8_t i = 0; i < n; i++, win_pos++) {
    if (win_pos == 0) {
      win_op = rev8(mosi[i]);
      switch (win_op) {
        case OP_SET_TX:      radio = RADIO_TX; break;
        case OP_SET_RX:      radio = RADIO_RX; break;
        case OP_SET_CAD:     radio = RADIO_RX; break; // channel scan before TX: still "listening"
        case OP_SET_STANDBY:
        case OP_SET_SLEEP:
        case OP_SET_FS:      radio = RADIO_STANDBY; break;
        default: break;
      }
    } else if (win_pos < sizeof(win_miso)) {
      win_miso[win_pos] = rev8(miso[i]);
      // GetIrqStatus: MOSI 12 00 00 00 / MISO xx status irq[15:8] irq[7:0]
      if (win_op == OP_GET_IRQ_STATUS && win_pos == 3) {
        on_irq_flags((uint16_t)((win_miso[2] << 8) | win_miso[3]));
      }
    }
  }
}

static void put(uint8_t *dst, rgb_t c) {
  dst[0] = (uint8_t)((c.r * brightness + 127) / 255);
  dst[1] = (uint8_t)((c.g * brightness + 127) / 255);
  dst[2] = (uint8_t)((c.b * brightness + 127) / 255);
}

static void compute(uint8_t rgb[6], uint32_t now_ms, bool mounted) {
  rgb_t link;
  if (!mounted) link = C_RED;
  else if (host_seen && (uint32_t)(now_ms - last_host_ms) < HOST_IDLE_MS) link = C_GREEN;
  else link = C_AMBER;

  rgb_t rad;
  if (flash_on && (int32_t)(now_ms - flash_until) < 0) {
    rad = flash_color;
  } else {
    flash_on = false;
    switch (radio) {
      case RADIO_RX: rad = C_BLUE; break;
      case RADIO_TX: rad = C_RED; break;
      default: rad = C_OFF; break;
    }
  }
  // Without a host, the radio pixel just shows whatever it last knew; blank it instead.
  if (!mounted) rad = C_OFF;

  put(&rgb[0], link);
  put(&rgb[3], rad);
}

void status_colors(uint8_t rgb[6]) { memcpy(rgb, shown, 6); }
radio_state_t status_radio_state(void) { return radio; }

void status_task(uint32_t now_ms, bool usb_mounted) {
  now_cache = now_ms;
  if (!usb_mounted) {
    radio = RADIO_UNKNOWN;
    host_seen = false;
  }
  uint8_t rgb[6];
  compute(rgb, now_ms, usb_mounted);
  if (shown_valid && memcmp(rgb, shown, 6) == 0) return;
  if (hw_pixels_write(rgb)) {
    memcpy(shown, rgb, 6);
    shown_valid = true;
  }
}
