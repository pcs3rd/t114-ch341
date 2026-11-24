// Heltec Mesh Node T114 hardware layer: SX1262 on SPIM2 + GPIO.

#include <string.h>
#include "nrf.h"
#include "hal/nrf_gpio.h"
#include "bsp/board_api.h"
#include "ch341_emu.h"
#include "t114_hw.h"

// --- T114 board power / peripherals (Meshtastic heltec_mesh_node_t114 variant) ---
#define T114_VEXT_EN     21  // P0.21, high = Vext on (NeoPixels, TFT, GPS)
#define T114_VTFT_CTRL    3  // P0.3, low = TFT powered
#define T114_VTFT_LEDA   15  // P0.15, low = TFT backlight on
#define T114_NEOPIXEL    14  // P0.14, 2x WS2812, GRB

#ifndef T114_NEOPIXEL_VEXT
#define T114_NEOPIXEL_VEXT 1 // switch Vext on so the NeoPixels have power
#endif

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

  // Keep the TFT and its backlight dark; we never drive the display.
  nrf_gpio_pin_set(T114_VTFT_CTRL);
  nrf_gpio_cfg_output(T114_VTFT_CTRL);
  nrf_gpio_pin_set(T114_VTFT_LEDA);
  nrf_gpio_cfg_output(T114_VTFT_LEDA);
#if T114_NEOPIXEL_VEXT
  nrf_gpio_pin_set(T114_VEXT_EN);
  nrf_gpio_cfg_output(T114_VEXT_EN);
#endif

  // WS2812 on PWM0: 16 MHz, 20 ticks = 1.25 us per bit. Bit 15 set makes each value the
  // high time at the start of the period; 0x8000 alone is a low period.
  nrf_gpio_pin_clear(T114_NEOPIXEL);
  nrf_gpio_cfg_output(T114_NEOPIXEL);
  NRF_PWM0->PSEL.OUT[0] = T114_NEOPIXEL;
  NRF_PWM0->PSEL.OUT[1] = 0xFFFFFFFFul;
  NRF_PWM0->PSEL.OUT[2] = 0xFFFFFFFFul;
  NRF_PWM0->PSEL.OUT[3] = 0xFFFFFFFFul;
  NRF_PWM0->MODE = PWM_MODE_UPDOWN_Up;
  NRF_PWM0->PRESCALER = PWM_PRESCALER_PRESCALER_DIV_1;
  NRF_PWM0->COUNTERTOP = 20;
  NRF_PWM0->LOOP = 0;
  NRF_PWM0->DECODER = (PWM_DECODER_LOAD_Common << PWM_DECODER_LOAD_Pos) |
                      (PWM_DECODER_MODE_RefreshCount << PWM_DECODER_MODE_Pos);
  NRF_PWM0->SEQ[0].REFRESH = 0;
  NRF_PWM0->SEQ[0].ENDDELAY = 0;
  NRF_PWM0->ENABLE = PWM_ENABLE_ENABLE_Enabled;
}

#define WS_T0H   (6u | 0x8000u)   // 0.375 us high
#define WS_T1H   (13u | 0x8000u)  // 0.8125 us high
#define WS_LOW   (0x8000u)
#define WS_BITS  (2 * 24)
#define WS_TAIL  2                // end low so the line idles low (latch comes from the gap between frames)

static uint16_t ws_seq[WS_BITS + WS_TAIL] __attribute__((aligned(4)));
static bool ws_running;

bool hw_pixels_write(uint8_t const rgb[6]) {
  if (ws_running) {
    if (!NRF_PWM0->EVENTS_SEQEND[0]) return false;
    NRF_PWM0->EVENTS_SEQEND[0] = 0;
    NRF_PWM0->EVENTS_STOPPED = 0;
    NRF_PWM0->TASKS_STOP = 1;
    while (!NRF_PWM0->EVENTS_STOPPED) {}
    ws_running = false;
  }
  uint16_t *p = ws_seq;
  for (int px = 0; px < 2; px++) {
    uint8_t const grb[3] = {rgb[px * 3 + 1], rgb[px * 3 + 0], rgb[px * 3 + 2]};
    for (int c = 0; c < 3; c++)
      for (int bit = 7; bit >= 0; bit--) *p++ = (grb[c] >> bit) & 1 ? WS_T1H : WS_T0H;
  }
  for (int i = 0; i < WS_TAIL; i++) *p++ = WS_LOW;

  NRF_PWM0->SEQ[0].PTR = (uint32_t)ws_seq;
  NRF_PWM0->SEQ[0].CNT = WS_BITS + WS_TAIL;
  NRF_PWM0->EVENTS_SEQEND[0] = 0;
  NRF_PWM0->TASKS_SEQSTART[0] = 1;
  ws_running = true;
  return true;
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


void hw_enter_bootloader(void) {
  NRF_POWER->GPREGRET = 0x57; // Adafruit nRF52 bootloader: DFU_MAGIC_UF2_RESET
  NVIC_SystemReset();
}
