// t114_hw.h implemented against a fake SX1262 that records what crosses its pins.
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include "t114_hw.h"
#include "fake_radio.h"

struct fake_radio radio;

static uint8_t rev(uint8_t x) {
  x = ((x >> 1) & 0x55) | ((x << 1) & 0xaa);
  x = ((x >> 2) & 0x33) | ((x << 2) & 0xcc);
  return (uint8_t)((x >> 4) | (x << 4));
}

uint8_t radio_miso_pattern(uint32_t pos) { return (uint8_t)(pos * 37u + 0x5Au); }

void hw_init(void) { uint32_t t = radio.now_ms; memset(&radio, 0, sizeof(radio)); radio.now_ms = t; radio.cs = 1; radio.reset = 1; }

void hw_apply_pins(uint8_t st, uint8_t dir) {
  int cs = (dir & 1) ? (st & 1) : 1;
  int rst = (dir & 4) ? ((st >> 2) & 1) : 1;
  if (radio.cs && !cs) { radio.win_len = 0; }                       // select
  if (!radio.cs && cs) {                                            // deselect: close the window
    struct window *w = &radio.windows[radio.n_windows % MAX_WINDOWS];
    w->len = radio.win_len;
    memcpy(w->mosi, radio.win, radio.win_len < sizeof(w->mosi) ? radio.win_len : sizeof(w->mosi));
    radio.n_windows++;
  }
  if (radio.reset && !rst) radio.reset_pulses++;
  radio.cs = cs;
  radio.reset = rst;
}

uint8_t hw_read_pins(uint8_t st, uint8_t dir) {
  uint8_t v = 0;
  v |= (uint8_t)radio.cs << 0;
  v |= (uint8_t)(((dir & 2) ? ((st >> 1) & 1) : 1) << 1);
  v |= (uint8_t)radio.reset << 2;
  v |= (uint8_t)(atomic_load(&radio.busy) & 1) << 4;
  v |= (uint8_t)(atomic_load(&radio.dio1) & 1) << 6;
  v |= 1u << 7;
  return v;
}

// Bytes reach here LSB-first, as the CH341 would clock them: a byte b on an LSB-first wire is the
// MSB-first byte rev(b), which is what the radio sees. Its MISO answer comes back the same way.
void hw_spi_xfer(uint8_t const *tx, uint8_t *rx, uint8_t n) {
  if (radio.cs) radio.bytes_outside_cs += n;
  for (uint8_t i = 0; i < n; i++) {
    if (radio.win_len < sizeof(radio.win)) radio.win[radio.win_len] = rev(tx[i]);
    uint8_t miso = radio_miso_pattern(radio.win_len);
    if (radio.win_len >= 2 && radio.win_len <= 3 && radio.win[0] == 0x12)  // GetIrqStatus
      miso = radio.win_len == 2 ? (uint8_t)(radio.irq >> 8) : (uint8_t)radio.irq;
    rx[i] = rev(miso);
    radio.win_len++;
  }
}

void hw_delay_us(uint32_t us) { (void)us; }
uint32_t hw_millis(void) { return radio.now_ms; }
bool hw_pixels_write(uint8_t const rgb[6]) { memcpy(radio.pixels, rgb, 6); radio.pixel_writes++; return true; }
void hw_enter_bootloader(void) {}
