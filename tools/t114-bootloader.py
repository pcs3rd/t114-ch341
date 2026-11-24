#!/usr/bin/env python3
"""Reboot a T114 CH341 bridge into its UF2 bootloader, so it can be reflashed without touching it.

    pip install pyusb        (or: nix-shell -p python3Packages.pyusb)
    sudo systemctl stop meshtasticd
    ./t114-bootloader.py [SERIAL]

The board then shows up as a USB drive (HT-n5262 or similar); copy a .uf2 onto it.
That also works for going back to stock Meshtastic firmware.
"""
import sys

import usb.core
import usb.util

serial = sys.argv[1] if len(sys.argv) > 1 else None
devs = [d for d in usb.core.find(find_all=True, idVendor=0x1A86, idProduct=0x5512)
        if "T114" in (usb.util.get_string(d, d.iProduct) or "")]
if serial:
    devs = [d for d in devs if usb.util.get_string(d, d.iSerialNumber) == serial]
if not devs:
    sys.exit("no T114 CH341 bridge found")
if len(devs) > 1:
    sys.exit("several bridges found, pass a serial: " +
             " ".join(usb.util.get_string(d, d.iSerialNumber) for d in devs))

d = devs[0]
try:
    # vendor, host-to-device, device recipient; bRequest 0xF7, wValue 0xB007
    d.ctrl_transfer(0x40, 0xF7, 0xB007, 0, None)
except usb.core.USBError:
    pass  # the device resets during the status stage, which can surface as an error
print("rebooting", usb.util.get_string(d, d.iSerialNumber), "into the UF2 bootloader")
