// Hardware hooks used by the CH341 protocol code. t114_hw.c implements them for the Heltec T114;
// the host-side test harness (test/) implements them with a fake SX1262.
#ifndef T114_HW_H_
#define T114_HW_H_

#include <stdint.h>
#include <stdbool.h>

void hw_init(void);
// Apply CH341 D0..D5 levels/directions to the radio pins (CS = D0, RESET = D2)
void hw_apply_pins(uint8_t d_state, uint8_t d_dir);
// Sample D0..D7 as a CH341 status byte (BUSY = D4, DIO1 = D6, MISO = D7)
uint8_t hw_read_pins(uint8_t d_state, uint8_t d_dir);
// Full duplex, LSB-first (CH341 bit order)
void hw_spi_xfer(uint8_t const *tx, uint8_t *rx, uint8_t n);
void hw_delay_us(uint32_t us);
uint32_t hw_millis(void);
void hw_led(bool on);
void hw_enter_bootloader(void);

#endif
