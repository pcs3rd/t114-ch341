#!/usr/bin/env python3
"""Control a T114 CH341 bridge over USB.

    t114ctl.py [-s SERIAL] brightness [0-255]   show or set status LED brightness (0 = off)
    t114ctl.py [-s SERIAL] bootloader           reboot into the UF2 bootloader for reflashing

Brightness is a vendor request that works while meshtasticd is running. It isn't saved, so it
resets to the firmware default on power-up; put it in a udev rule or systemd unit to make it
stick. For bootloader, stop meshtasticd first; the board then shows up as a USB drive, and you
copy a .uf2 onto it (stock Meshtastic works too).

Needs pyusb (pip install pyusb, or `nix develop` in this repo).
"""
import argparse
import sys

import usb.core
import usb.util

REQ_BOOTLOADER, BOOTLOADER_MAGIC = 0xF7, 0xB007
REQ_BRIGHTNESS = 0xF8


def find(serial):
    devs = [d for d in usb.core.find(find_all=True, idVendor=0x1A86, idProduct=0x5512)
            if "T114" in (usb.util.get_string(d, d.iProduct) or "")]
    if serial:
        devs = [d for d in devs if usb.util.get_string(d, d.iSerialNumber) == serial]
    if not devs:
        sys.exit("no T114 CH341 bridge found")
    if len(devs) > 1:
        sys.exit("several bridges found, pass -s SERIAL: " +
                 " ".join(usb.util.get_string(d, d.iSerialNumber) for d in devs))
    return devs[0]


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("-s", "--serial", help="8-character USB serial of the bridge")
    sub = ap.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("brightness")
    b.add_argument("level", nargs="?", type=int)
    sub.add_parser("bootloader")
    args = ap.parse_args()

    d = find(args.serial)
    sn = usb.util.get_string(d, d.iSerialNumber)
    if args.cmd == "brightness":
        if args.level is not None:
            if not 0 <= args.level <= 255:
                sys.exit("level must be 0-255")
            d.ctrl_transfer(0x40, REQ_BRIGHTNESS, args.level, 0, None)
        level = d.ctrl_transfer(0xC0, REQ_BRIGHTNESS, 0, 0, 1)[0]
        print(f"{sn}: brightness {level}")
    else:
        try:
            d.ctrl_transfer(0x40, REQ_BOOTLOADER, BOOTLOADER_MAGIC, 0, None)
        except usb.core.USBError:
            pass  # the device resets during the status stage, which can surface as an error
        print(f"{sn}: rebooting into the UF2 bootloader")


if __name__ == "__main__":
    main()
