# t114-ch341: Heltec T114 as a MeshStick-style USB LoRa radio for meshtasticd

This firmware turns a Heltec Mesh Node T114 (nRF52840 + SX1262) into a USB-to-SPI bridge that
meshtasticd can drive with `spidev: ch341`, the same path used by the Elecrow MeshStick and the
PINE64 PineDio USB adapter. The nRF52840 pretends to be a WCH CH341A (`1a86:5512`) and passes
SPI and GPIO through to the T114's own SX1262. **Nothing changes on the meshtasticd side.**

```
Pi ──USB── nRF52840 (fake CH341A) ──SPI/GPIO── SX1262
           D0=CS  D2=RESET  D4=BUSY  D6=DIO1   SPIM2 @ 8 MHz
```

## Flash

1. Double-tap the T114's reset button. A USB drive appears.
2. Copy `t114_ch341.uf2` onto it. The board reboots, and the status LEDs (below) come on.

To go back to Meshtastic, double-tap reset and copy the stock T114 `.uf2` onto the drive. The
bootloader and SoftDevice are never touched. To reflash a board that's already deployed, use
`tools/t114ctl.py bootloader`, which sends a vendor request that reboots it into the bootloader.

## Status LEDs

The two NeoPixels show the state of the link and of the radio:

| Pixel | Colour | Meaning |
|---|---|---|
| Link (pixel 0) | red | No USB host (powered but not enumerated) |
| | amber | Enumerated, but meshtasticd hasn't talked to it for 2 s |
| | green | meshtasticd is driving the radio |
| Radio (pixel 1) | off | Standby / sleep, or no host |
| | blue | Listening (RX or channel scan) |
| | red | Transmitting |
| | green flash | Packet received |
| | orange flash | Packet received with a CRC error |

The radio pixel comes from watching the SPI traffic that passes through: the SX1262 opcode at
the start of each chip-select window (SetTx, SetRx, SetCad, SetStandby, SetSleep), and the IRQ
flags in GetIrqStatus replies. So it shows what meshtasticd actually told the radio to do. It
adds nothing to the protocol. The board's green LED stays off.

Brightness defaults to 24/255. Change it while running, with no reflash:
```
tools/t114ctl.py brightness 8     # dimmer
tools/t114ctl.py brightness 0     # off
```
The setting lasts until power-off. To change the default, rebuild with
`./build.sh EXTRA_CFLAGS=-DSTATUS_DEFAULT_BRIGHTNESS=8`.

The NeoPixels run off the T114's switched Vext rail, which the firmware turns on. That rail
also powers the TFT, which the firmware keeps off, and the GPS socket. **With a GPS module
fitted, it will draw power (tens of mA).** If you'd rather not have that, build with
`EXTRA_CFLAGS=-DT114_NEOPIXEL_VEXT=0`. If your board doesn't take its NeoPixel power from Vext, that setting
also turns the LEDs off.

## Configure meshtasticd

Copy `lora-usb-heltec-t114.yaml` to `/etc/meshtasticd/config.d/`.

**Don't use `Module: auto`.** Auto-detection picks a config by matching the USB product string
against meshtasticd's list of known sticks. This bridge isn't on that list, so with `auto`
meshtasticd stops with "Unable to find config". Set the module explicitly, as the yaml does.

The node ID is derived from the bridge's 8-character USB serial number, which comes from the
nRF52840's factory device ID. So each T114 keeps a stable node ID, and that ID is different from
the one it had running stock Meshtastic.

Docker (doco-cd / borg-drones): pass the device through like any USB stick,
e.g. `devices: ["/dev/bus/usb:/dev/bus/usb"]`. Containers that already work with a MeshStick
work unchanged.

## How it works

meshtasticd's `Ch341Hal` uses libpinedio-usb (libusb), which talks the CH341 stream protocol on
bulk endpoints 0x02/0x82. Each command is one 32-byte USB packet:

| Command | Meaning | Reply |
|---|---|---|
| `A8 d1..dN` | SPI stream, N ≤ 31, LSB-first | N bytes |
| `AB 80\|v 40\|d … 20` | UIO stream: set D0–D5 level (`80`), direction (`40`), read (`00`), delay (`C0`), end (`20`) | 1 byte per read |
| `A0` | Status | 6 bytes, byte 0 = D0–D7 |

The host bit-reverses every SPI byte to undo the CH341's LSB-first shifter. Running SPIM
LSB-first undoes that, so bytes pass through untouched. Responses go out as individual short
packets, because that's what ends each of libpinedio's IN transfers. That requirement is why
`ch341_emu.c` is a raw TinyUSB class driver: the stock vendor class buffers data into byte
streams and loses packet boundaries.

DIO1 interrupts are polled by libpinedio (status read every 1 ms), exactly as with a real
MeshStick.

## Build

With Nix (flakes):

```
nix build github:pcs3rd/t114-ch341   # or `nix build` in a checkout; UF2 lands in ./result/
nix flake check                      # runs the protocol test below
nix develop                          # arm-none-eabi-gcc, make, python3, pyusb
```

Every source is pinned as a flake input (TinyUSB, nrfx, CMSIS_5, uf2, libch341), so the build
is reproducible and never touches the network.

Without Nix: `./build.sh` clones TinyUSB at the same commit and writes `out/t114_ch341.uf2`.
It needs `arm-none-eabi-gcc` (+ newlib), make, python3 and git.

## Test (no hardware)

```
cd test && make
```
This builds meshtasticd's real libpinedio-usb (pinned) against a fake libusb. The fake routes
every bulk transfer, packet by packet, into the firmware's class driver, with a fake SX1262 on
the other side. It checks CS windows, MOSI/MISO byte order, every transfer size that crosses
31/32-byte packet boundaries, RESET, BUSY/DIO1 reads, the interrupt poll thread, the status-LED
radio sniffer (SetTx/SetRx/IRQ flags, including commands split across USB packets), and 2000
random transfers. It runs under ASan/UBSan.

## Status / caveats

- Not yet run on real hardware. The protocol layer and LED logic are tested end to end against
  the real host library, but USB enumeration on the nRF52840, the SPIM wiring and the WS2812
  PWM timing are only verified by compiling. The T114 pin map comes from Meshtastic's `heltec_mesh_node_t114/variant.h`.
- If something's off on first plug-in, check `dmesg` for enumeration of `1a86:5512`, then run
  `meshtasticd` in the foreground. Its "CH341 Serial …" line confirms the USB layer works.
- On newer kernels, the in-tree `spi-ch341` driver may bind to the device first. libpinedio
  detaches it automatically, as it does for a real CH341.
- The T114's screen, GPS and button are left unused.
- Which physical NeoPixel is "pixel 0" hasn't been checked on hardware; if they're swapped
  relative to the table, that's cosmetic.
- `t114ctl.py` needs permission to the USB device (root, or a udev rule for `1a86:5512`).

## License

Firmware: MIT. TinyUSB (MIT) is fetched at build time. The test harness builds against
libch341-spi-userspace (GPL-2.0-or-later) but doesn't ship it.
