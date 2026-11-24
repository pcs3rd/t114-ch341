/* metadata:
   name: Heltec Mesh Node T114 (CH341 bridge)
   url: https://heltec.org/project/mesh-node-t114/
*/
#ifndef BOARD_H_
#define BOARD_H_

#define _PINNUM(port, pin)    ((port)*32 + (pin))

// Green LED, active low
#define LED_PIN               _PINNUM(1, 3)
#define LED_STATE_ON          0

// User button
#define BUTTON_PIN            _PINNUM(1, 10)
#define BUTTON_STATE_ACTIVE   0

// Unused debug UART (Serial2 header pins)
#define UART_RX_PIN           _PINNUM(0, 9)
#define UART_TX_PIN           _PINNUM(0, 10)

#endif
