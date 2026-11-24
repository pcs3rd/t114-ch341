// Status LEDs on the T114's two WS2812s.
//
//   Pixel 0, link:   red = no USB host, amber = enumerated but idle, green = meshtasticd driving it
//   Pixel 1, radio:  off = standby/sleep, blue = listening, red = transmitting,
//                    green flash = packet received, orange flash = CRC error
//
// Radio state is inferred by watching the SPI traffic go past: SX1262 opcodes at the start of
// each CS window, and the IRQ flags in GetIrqStatus replies.
#ifndef STATUS_H_
#define STATUS_H_

#include <stdint.h>
#include <stdbool.h>

typedef enum { RADIO_UNKNOWN, RADIO_STANDBY, RADIO_RX, RADIO_TX } radio_state_t;

// Default brightness, 0-255 scale on every channel. WS2812s are painfully bright at 255.
#ifndef STATUS_DEFAULT_BRIGHTNESS
#define STATUS_DEFAULT_BRIGHTNESS 24
#endif

void status_init(void);
void status_set_brightness(uint8_t level);  // 0 turns both pixels off
uint8_t status_get_brightness(void);

// Hooks from the CH341 emulation
void status_host_packet(uint32_t now_ms);   // any OUT packet from the host
void status_cs(bool selected);              // radio CS edge
void status_spi(uint8_t const *mosi, uint8_t const *miso, uint8_t n, uint32_t now_ms); // CH341 (LSB-first) byte order

// Call often; pushes new colours to the pixels when they change
void status_task(uint32_t now_ms, bool usb_mounted);

// For tests
radio_state_t status_radio_state(void);
void status_colors(uint8_t rgb[6]);

#endif
