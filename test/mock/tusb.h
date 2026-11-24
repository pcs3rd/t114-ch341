// Minimal TinyUSB surface for compiling ch341_emu.c / usb_descriptors.c on the host.
#ifndef MOCK_TUSB_H
#define MOCK_TUSB_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#define CFG_TUSB_MEM_ALIGN __attribute__((aligned(4)))
#define CFG_TUD_ENDPOINT0_SIZE 64
#define TU_MIN(a, b) (((a) < (b)) ? (a) : (b))
#define TU_BIT(n) (1u << (n))
#define TU_VERIFY(c, r) do { if (!(c)) return r; } while (0)
#define TU_ASSERT(c, r) do { if (!(c)) { fprintf(stderr, "TU_ASSERT %s:%d\n", __FILE__, __LINE__); return r; } } while (0)
#define U16_TO_U8S_LE(x) (uint8_t)((x) & 0xff), (uint8_t)(((x) >> 8) & 0xff)
#include <stdio.h>

enum { TUSB_DESC_DEVICE = 1, TUSB_DESC_CONFIGURATION = 2, TUSB_DESC_STRING = 3, TUSB_DESC_INTERFACE = 4, TUSB_DESC_ENDPOINT = 5 };
enum { TUSB_XFER_CONTROL = 0, TUSB_XFER_ISOCHRONOUS, TUSB_XFER_BULK, TUSB_XFER_INTERRUPT };
enum { TUSB_CLASS_VENDOR_SPECIFIC = 0xFF };
enum { TUSB_DIR_OUT = 0, TUSB_DIR_IN = 1 };
enum { CONTROL_STAGE_IDLE, CONTROL_STAGE_SETUP, CONTROL_STAGE_DATA, CONTROL_STAGE_ACK };
typedef enum { XFER_RESULT_SUCCESS = 0, XFER_RESULT_FAILED, XFER_RESULT_STALLED, XFER_RESULT_TIMEOUT } xfer_result_t;

#define TUD_CONFIG_DESC_LEN 9
#define TUD_CONFIG_DESCRIPTOR(_config_num, _itfcount, _stridx, _total_len, _attribute, _power_ma) \
  9, TUSB_DESC_CONFIGURATION, U16_TO_U8S_LE(_total_len), _itfcount, _config_num, _stridx, TU_BIT(7) | _attribute, (_power_ma)/2

typedef struct __attribute__((packed)) {
  uint8_t bLength, bDescriptorType; uint16_t bcdUSB;
  uint8_t bDeviceClass, bDeviceSubClass, bDeviceProtocol, bMaxPacketSize0;
  uint16_t idVendor, idProduct, bcdDevice;
  uint8_t iManufacturer, iProduct, iSerialNumber, bNumConfigurations;
} tusb_desc_device_t;

typedef struct __attribute__((packed)) {
  uint8_t bLength, bDescriptorType, bInterfaceNumber, bAlternateSetting, bNumEndpoints;
  uint8_t bInterfaceClass, bInterfaceSubClass, bInterfaceProtocol, iInterface;
} tusb_desc_interface_t;

typedef struct __attribute__((packed)) {
  uint8_t bLength, bDescriptorType, bEndpointAddress, bmAttributes; uint16_t wMaxPacketSize; uint8_t bInterval;
} tusb_desc_endpoint_t;

typedef struct __attribute__((packed)) {
  union { struct __attribute__((packed)) { uint8_t recipient : 5; uint8_t type : 2; uint8_t direction : 1; } bmRequestType_bit; uint8_t bmRequestType; };
  uint8_t bRequest; uint16_t wValue, wIndex, wLength;
} tusb_control_request_t;

static inline uint8_t const *tu_desc_next(void const *d) { uint8_t const *p = (uint8_t const *)d; return p + p[0]; }
static inline uint8_t tu_desc_type(void const *d) { return ((uint8_t const *)d)[1]; }

typedef struct {
  char const *name;
  void (*init)(void);
  bool (*deinit)(void);
  void (*reset)(uint8_t rhport);
  uint16_t (*open)(uint8_t rhport, tusb_desc_interface_t const *desc_intf, uint16_t max_len);
  bool (*control_xfer_cb)(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request);
  bool (*xfer_cb)(uint8_t rhport, uint8_t ep_addr, xfer_result_t result, uint32_t xferred_bytes);
  bool (*xfer_isr)(uint8_t rhport, uint8_t ep_addr, xfer_result_t result, uint32_t xferred_bytes);
  void (*sof)(uint8_t rhport, uint32_t frame_count);
} usbd_class_driver_t;

bool usbd_edpt_open(uint8_t rhport, tusb_desc_endpoint_t const *desc_ep);
bool usbd_edpt_xfer(uint8_t rhport, uint8_t ep_addr, uint8_t *buffer, uint16_t total_bytes, bool is_isr);
bool usbd_edpt_busy(uint8_t rhport, uint8_t ep_addr);
bool tud_control_status(uint8_t rhport, tusb_control_request_t const *request);
bool tud_control_xfer(uint8_t rhport, tusb_control_request_t const *request, void *buffer, uint16_t len);
bool tud_mounted(void);
usbd_class_driver_t const *usbd_app_driver_get_cb(uint8_t *driver_count);
uint8_t const *tud_descriptor_device_cb(void);
uint8_t const *tud_descriptor_configuration_cb(uint8_t index);
uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid);
#endif
