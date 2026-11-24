// End-to-end: meshtasticd's real libpinedio-usb.c -> fake USB -> ch341_emu.c -> fake SX1262.
// Calls the library the way meshtasticd's Ch341Hal (src/platform/portduino/USBHal.h) does.

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "libpinedio-usb.h"
#include "fake_radio.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("  FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static struct pinedio_inst inst;
static atomic_int irq_hits;
static void on_irq(void) { atomic_fetch_add(&irq_hits, 1); }

static void fill(uint8_t *b, uint32_t n, uint32_t seed) { for (uint32_t i = 0; i < n; i++) b[i] = (uint8_t)(seed * 131u + i * 7u + 3u); }

// One RadioLib-style CS window: select, transfer, deselect. `packed` = meshtasticd's fast path.
static void spi_window(uint32_t n, uint32_t seed, bool packed) {
  uint8_t out[512], in[512];
  fill(out, n, seed);
  memset(in, 0xEE, n);
  int before = radio.n_windows;
  int32_t r;
  if (packed) {
    r = pinedio_transceive_select(&inst, out, in, n);
  } else {
    pinedio_digital_write(&inst, 0, false);
    r = pinedio_transceive(&inst, out, in, n);
    pinedio_digital_write(&inst, 0, true);
  }
  CHECK(r == 0, "transfer of %u returned %d", n, r);
  CHECK(radio.n_windows == before + 1, "%u bytes: expected 1 CS window, got %d", n, radio.n_windows - before);
  struct window *w = &radio.windows[(radio.n_windows - 1) % MAX_WINDOWS];
  CHECK(w->len == n, "%u bytes: radio saw %u inside CS", n, w->len);
  CHECK(memcmp(w->mosi, out, n) == 0, "%u bytes: MOSI mismatch (bit order or framing)", n);
  int bad = 0;
  for (uint32_t i = 0; i < n; i++) bad += in[i] != radio_miso_pattern(i);
  CHECK(bad == 0, "%u bytes: %d MISO bytes wrong (first %02x want %02x)", n, bad, in[0], radio_miso_pattern(0));
}

int main(void) {
  printf("init\n");
  pinedio_set_option(&inst, PINEDIO_OPTION_VID, 0x1A86);
  pinedio_set_option(&inst, PINEDIO_OPTION_PID, 0x5512);
  CHECK(pinedio_init(&inst, NULL) == 0, "pinedio_init failed");
  CHECK(strlen(inst.serial_number) == 8, "serial '%s' is not 8 chars (meshtasticd needs 8 for the MAC)", inst.serial_number);
  printf("  serial %s\n", inst.serial_number);

  // Ch341Hal constructor + RadioLib Module::init()
  pinedio_set_option(&inst, PINEDIO_OPTION_AUTO_CS, 0);
  pinedio_set_pin_mode(&inst, 3, true);
  pinedio_set_pin_mode(&inst, 5, true);
  pinedio_set_pin_mode(&inst, 0, true); // CS
  pinedio_digital_write(&inst, 0, true);
  CHECK(radio.cs == 1, "CS should idle high");
  pinedio_set_pin_mode(&inst, 2, true); // RESET
  pinedio_digital_write(&inst, 2, false);
  CHECK(radio.reset == 0, "RESET should be low");
  pinedio_digital_write(&inst, 2, true);
  CHECK(radio.reset == 1 && radio.reset_pulses == 1, "RESET pulse not seen");
  CHECK(radio.cs == 1, "CS disturbed by RESET write");

  printf("BUSY / DIO1 reads\n");
  atomic_store(&radio.busy, 1);
  CHECK(pinedio_digital_read(&inst, 4) != 0, "BUSY high read as low");
  atomic_store(&radio.busy, 0);
  CHECK(pinedio_digital_read(&inst, 4) == 0, "BUSY low read as high");
  atomic_store(&radio.dio1, 1);
  CHECK(pinedio_digital_read(&inst, 6) != 0, "DIO1 high read as low");
  atomic_store(&radio.dio1, 0);
  CHECK(pinedio_digital_read(&inst, 6) == 0, "DIO1 low read as high");

  printf("SPI windows, packed CS (meshtasticd default)\n");
  uint32_t sizes[] = {1, 2, 3, 4, 9, 30, 31, 32, 33, 61, 62, 63, 64, 100, 255, 258};
  for (unsigned i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) spi_window(sizes[i], i, true);
  printf("SPI windows, separate CS writes\n");
  for (unsigned i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) spi_window(sizes[i], i + 100, false);
  CHECK(radio.bytes_outside_cs == 0, "%u bytes clocked with CS high", radio.bytes_outside_cs);

  printf("write_read\n");
  {
    uint8_t cmd[4] = {0x1D, 0x07, 0x40, 0x00}, data[8];
    pinedio_digital_write(&inst, 0, false);
    CHECK(pinedio_write_read(&inst, cmd, 4, data, 8) == 0, "write_read failed");
    pinedio_digital_write(&inst, 0, true);
    struct window *w = &radio.windows[(radio.n_windows - 1) % MAX_WINDOWS];
    CHECK(w->len == 12 && memcmp(w->mosi, cmd, 4) == 0, "write_read command bytes wrong");
    CHECK(data[0] == radio_miso_pattern(4) && data[7] == radio_miso_pattern(11), "write_read data wrong");
  }

  printf("DIO1 rising edge -> interrupt callback (poll thread)\n");
  atomic_store(&radio.dio1, 0);
  pinedio_attach_interrupt(&inst, PINEDIO_PIN_D6, PINEDIO_INT_MODE_RISING, on_irq);
  usleep(20000);
  // keep doing SPI on the main thread while the poll thread runs, as meshtasticd does
  for (int i = 0; i < 20; i++) spi_window(40, 500 + i, true);
  atomic_store(&radio.dio1, 1);
  for (int i = 0; i < 200 && atomic_load(&irq_hits) == 0; i++) usleep(1000);
  CHECK(atomic_load(&irq_hits) == 1, "expected 1 interrupt, got %d", atomic_load(&irq_hits));
  atomic_store(&radio.dio1, 0);
  usleep(20000);
  CHECK(atomic_load(&irq_hits) == 1, "falling edge fired a RISING interrupt");
  pinedio_deattach_interrupt(&inst, PINEDIO_PIN_D6);

  printf("stress: 2000 random windows\n");
  srand(1);
  for (int i = 0; i < 2000; i++) spi_window(1 + rand() % 300, 1000 + i, rand() & 1);
  CHECK(radio.bytes_outside_cs == 0, "%u bytes clocked with CS high", radio.bytes_outside_cs);

  pinedio_deinit(&inst);
  extern unsigned long stat_out_pkts, stat_in_pkts;
  printf("%lu OUT packets, %lu IN packets\n", stat_out_pkts, stat_in_pkts);
  printf(fails ? "FAILED: %d\n" : "ALL PASS\n", fails);
  return fails != 0;
}
