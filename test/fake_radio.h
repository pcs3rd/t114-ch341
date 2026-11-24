#ifndef FAKE_RADIO_H
#define FAKE_RADIO_H
#include <stdatomic.h>
#include <stdint.h>

#define MAX_WINDOWS 64
struct window { uint32_t len; uint8_t mosi[512]; };

struct fake_radio {
  int cs, reset;
  atomic_int busy, dio1;
  uint8_t win[512];
  uint32_t win_len;
  struct window windows[MAX_WINDOWS];
  int n_windows;
  int reset_pulses;
  uint32_t bytes_outside_cs;
  uint16_t irq;            // what GetIrqStatus (0x12) returns
  uint32_t now_ms;         // fake clock for hw_millis()
  uint8_t pixels[6];       // last frame written to the WS2812s
  int pixel_writes;
};
extern struct fake_radio radio;
uint8_t radio_miso_pattern(uint32_t pos);
#endif
