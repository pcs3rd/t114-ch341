// Heltec Mesh Node T114 hardware layer: SX1262 on SPIM2 + GPIO.

#include <string.h>
#include "nrf.h"
#include "hal/nrf_gpio.h"
#include "bsp/board_api.h"
#include "ch341_emu.h"
#include "t114_hw.h"

static uint8_t spi_tx[CH341_PACKET_LEN] __attribute__((aligned(4)));
static uint8_t spi_rx[CH341_PACKET_LEN] __attribute__((aligned(4)));

void hw_init(void) {
  // DWT cycle counter for delays / timekeeping
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

  nrf_gpio_cfg_input(T114_LORA_BUSY, NRF_GPIO_PIN_NOPULL);
  nrf_gpio_cfg_input(T114_LORA_DIO1, NRF_GPIO_PIN_NOPULL);

  // SPIM2. The CH341 shifts LSB-first and libpinedio bit-reverses every byte to compensate,
  // so running SPIM LSB-first passes the radio's MSB-first bytes through untouched.
  nrf_gpio_pin_clear(T114_LORA_SCK);
  nrf_gpio_cfg(T114_LORA_SCK, NRF_GPIO_PIN_DIR_OUTPUT, NRF_GPIO_PIN_INPUT_CONNECT, NRF_GPIO_PIN_NOPULL,
               NRF_GPIO_PIN_S0S1, NRF_GPIO_PIN_NOSENSE);
  nrf_gpio_pin_clear(T114_LORA_MOSI);
  nrf_gpio_cfg_output(T114_LORA_MOSI);
  nrf_gpio_cfg_input(T114_LORA_MISO, NRF_GPIO_PIN_NOPULL);

  NRF_SPIM2->ENABLE = SPIM_ENABLE_ENABLE_Disabled;
  NRF_SPIM2->PSEL.SCK = T114_LORA_SCK;
  NRF_SPIM2->PSEL.MOSI = T114_LORA_MOSI;
  NRF_SPIM2->PSEL.MISO = T114_LORA_MISO;
  NRF_SPIM2->CONFIG = (SPIM_CONFIG_ORDER_LsbFirst << SPIM_CONFIG_ORDER_Pos) |
                      (SPIM_CONFIG_CPHA_Leading << SPIM_CONFIG_CPHA_Pos) |
                      (SPIM_CONFIG_CPOL_ActiveHigh << SPIM_CONFIG_CPOL_Pos);
  NRF_SPIM2->FREQUENCY = CH341_SPI_FREQ;
  NRF_SPIM2->ORC = 0xFF;
  NRF_SPIM2->ENABLE = SPIM_ENABLE_ENABLE_Enabled;

  hw_apply_pins(0, 0);
}

void hw_apply_pins(uint8_t d_state, uint8_t d_dir) {
  // D0 -> CS. As an input (the CH341's power-on state) it is pulled up, so the radio stays deselected.
  if (d_dir & 0x01) {
    nrf_gpio_pin_write(T114_LORA_CS, d_state & 0x01);
    nrf_gpio_cfg_output(T114_LORA_CS);
  } else {
    nrf_gpio_cfg_input(T114_LORA_CS, NRF_GPIO_PIN_PULLUP);
  }
  // D2 -> NRESET. Released, the SX1262's own pull-up keeps it out of reset.
  if (d_dir & 0x04) {
    nrf_gpio_pin_write(T114_LORA_RESET, (d_state >> 2) & 1u);
    nrf_gpio_cfg_output(T114_LORA_RESET);
  } else {
    nrf_gpio_cfg_input(T114_LORA_RESET, NRF_GPIO_PIN_NOPULL);
  }
  // D1 (RXEN on a MeshStick) and D3/D5 (SCK/MOSI, owned by SPIM) have no physical effect here.
}

static inline uint8_t level_of(uint32_t pin, bool is_output) {
  return (uint8_t)(is_output ? nrf_gpio_pin_out_read(pin) : nrf_gpio_pin_read(pin));
}

uint8_t hw_read_pins(uint8_t d_state, uint8_t d_dir) {
  uint8_t v = 0;
  v |= level_of(T114_LORA_CS, d_dir & 0x01) << 0;
  v |= ((d_dir & 0x02) ? ((d_state >> 1) & 1u) : 1u) << 1; // virtual pin, reads as pulled high
  v |= level_of(T114_LORA_RESET, d_dir & 0x04) << 2;
  // D3 = SCK, idle low in SPI mode 0
  v |= (uint8_t)nrf_gpio_pin_read(T114_LORA_BUSY) << 4;
  // D5 = MOSI, reported low
  v |= (uint8_t)nrf_gpio_pin_read(T114_LORA_DIO1) << 6;
  v |= (uint8_t)nrf_gpio_pin_read(T114_LORA_MISO) << 7;
  return v;
}

void hw_spi_xfer(uint8_t const *tx, uint8_t *rx, uint8_t n) {
  memcpy(spi_tx, tx, n);
  NRF_SPIM2->TXD.PTR = (uint32_t)spi_tx;
  NRF_SPIM2->TXD.MAXCNT = n;
  NRF_SPIM2->RXD.PTR = (uint32_t)spi_rx;
  NRF_SPIM2->RXD.MAXCNT = n;
  NRF_SPIM2->EVENTS_END = 0;
  NRF_SPIM2->TASKS_START = 1;
  while (!NRF_SPIM2->EVENTS_END) {}
  NRF_SPIM2->EVENTS_END = 0;
  memcpy(rx, spi_rx, n);
}

void hw_delay_us(uint32_t us) {
  uint32_t start = DWT->CYCCNT;
  uint32_t n = us * (SystemCoreClock / 1000000u);
  while ((uint32_t)(DWT->CYCCNT - start) < n) {}
}

// Wraps every ~67 s at 64 MHz; callers only use differences, so extend it in software.
uint32_t hw_millis(void) {
  static uint32_t last_cyc, ms, rem;
  uint32_t now = DWT->CYCCNT;
  uint32_t d = now - last_cyc;
  last_cyc = now;
  rem += d;
  uint32_t per_ms = SystemCoreClock / 1000u;
  ms += rem / per_ms;
  rem %= per_ms;
  return ms;
}

void hw_led(bool on) { board_led_write(on); }

void hw_enter_bootloader(void) {
  NRF_POWER->GPREGRET = 0x57; // Adafruit nRF52 bootloader: DFU_MAGIC_UF2_RESET
  NVIC_SystemReset();
}
