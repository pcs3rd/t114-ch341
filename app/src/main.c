// Heltec Mesh Node T114 -> CH341 USB-to-SPI LoRa bridge for meshtasticd (spidev: ch341).

#include "bsp/board_api.h"
#include "tusb.h"
#include "ch341_emu.h"

int main(void) {
  board_init();
  ch341_emu_init();

  tusb_rhport_init_t dev_init = {
      .role = TUSB_ROLE_DEVICE,
      .speed = TUSB_SPEED_AUTO,
  };
  tusb_init(BOARD_TUD_RHPORT, &dev_init);
  board_init_after_tusb();

  while (1) {
    tud_task();
    ch341_emu_task();
  }
}
