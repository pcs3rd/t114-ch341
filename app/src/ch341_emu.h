#ifndef CH341_EMU_H_
#define CH341_EMU_H_

#include <stdint.h>
#include <stdbool.h>

#define CH341_EP_OUT     0x02
#define CH341_EP_IN      0x82
#define CH341_EP_INT     0x81
#define CH341_PACKET_LEN 32

// --- SX1262 wiring on the Heltec Mesh Node T114 (from Meshtastic's variant.h) ---
#define T114_LORA_SCK    19  // P0.19
#define T114_LORA_MOSI   22  // P0.22
#define T114_LORA_MISO   23  // P0.23
#define T114_LORA_CS     24  // P0.24
#define T114_LORA_RESET  25  // P0.25
#define T114_LORA_BUSY   17  // P0.17
#define T114_LORA_DIO1   20  // P0.20

// --- How CH341 D0..D7 map onto the radio (same as the MeshStick / meshtasticd ch341 configs) ---
//   D0 = CS (out)    D1 = RXEN (unused, virtual)   D2 = RESET (out)   D3 = SCK (hardware SPI)
//   D4 = BUSY (in)   D5 = MOSI (hardware SPI)      D6 = DIO1/IRQ (in) D7 = MISO (hardware SPI)

#ifndef CH341_SPI_FREQ
#define CH341_SPI_FREQ SPIM_FREQUENCY_FREQUENCY_M8
#endif

// Vendor control request that reboots into the UF2 bootloader (so a remote host can reflash).
// bmRequestType 0x40, bRequest 0xF7, wValue 0xB007.
#define CH341_EMU_REQ_BOOTLOADER  0xF7
#define CH341_EMU_BOOTLOADER_MAGIC 0xB007

void ch341_emu_init(void);
void ch341_emu_task(void);

#endif
