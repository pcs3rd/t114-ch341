// USB descriptors mimicking a CH341A in synchronous-serial (SPI/I2C/GPIO) mode, 1a86:5512.
// Endpoint layout matches the real chip: bulk IN 0x82, bulk OUT 0x02, interrupt IN 0x81,
// all with 32-byte max packet. The 32-byte packet size is load-bearing: libpinedio frames
// every CH341 command as exactly one USB packet.

#include <stdio.h>
#include "tusb.h"
#include "nrf.h"
#include "ch341_emu.h"

#ifndef CH341_EMU_VID
#define CH341_EMU_VID 0x1A86
#endif
#ifndef CH341_EMU_PID
#define CH341_EMU_PID 0x5512
#endif

static tusb_desc_device_t const desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = TUSB_CLASS_VENDOR_SPECIFIC,
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = CH341_EMU_VID,
    .idProduct          = CH341_EMU_PID,
    .bcdDevice          = 0x0304,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01,
};

uint8_t const *tud_descriptor_device_cb(void) { return (uint8_t const *)&desc_device; }

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + 9 + 3 * 7)

static uint8_t const desc_configuration[] = {
    // Config: 1 interface, bus powered, 100 mA
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, CONFIG_TOTAL_LEN, 0x00, 100),
    // Interface 0: vendor class, subclass 1, protocol 2, 3 endpoints (as a real CH341A)
    9, TUSB_DESC_INTERFACE, 0, 0, 3, TUSB_CLASS_VENDOR_SPECIFIC, 0x01, 0x02, 0,
    // Bulk IN 0x82
    7, TUSB_DESC_ENDPOINT, CH341_EP_IN, TUSB_XFER_BULK, U16_TO_U8S_LE(CH341_PACKET_LEN), 0,
    // Bulk OUT 0x02
    7, TUSB_DESC_ENDPOINT, CH341_EP_OUT, TUSB_XFER_BULK, U16_TO_U8S_LE(CH341_PACKET_LEN), 0,
    // Interrupt IN 0x81 (opened, never used)
    7, TUSB_DESC_ENDPOINT, CH341_EP_INT, TUSB_XFER_INTERRUPT, U16_TO_U8S_LE(8), 1,
};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
  (void)index;
  return desc_configuration;
}

// Strings. The serial must be exactly 8 characters: meshtasticd derives the node's MAC from it
// and only does so when strlen(serial) == 8.
static char const *string_desc_arr[] = {
    (const char[]){0x09, 0x04}, // 0: English
    "Heltec",                   // 1: Manufacturer
    "T114 CH341 LoRa bridge",   // 2: Product
    NULL,                       // 3: Serial, from FICR DEVICEID
};

static uint16_t _desc_str[32 + 1];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
  (void)langid;
  size_t chr_count;
  char serial[9];
  char const *str;

  if (index == 0) {
    memcpy(&_desc_str[1], string_desc_arr[0], 2);
    chr_count = 1;
  } else {
    if (index == 3) {
      snprintf(serial, sizeof(serial), "%08lX",
               (unsigned long)(NRF_FICR->DEVICEID[0] ^ NRF_FICR->DEVICEID[1]));
      str = serial;
    } else if (index < sizeof(string_desc_arr) / sizeof(string_desc_arr[0])) {
      str = string_desc_arr[index];
    } else {
      return NULL;
    }
    chr_count = strlen(str);
    if (chr_count > 32) chr_count = 32;
    for (size_t i = 0; i < chr_count; i++) _desc_str[1 + i] = str[i];
  }
  _desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
  return _desc_str;
}
