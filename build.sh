#!/usr/bin/env bash
# Build the T114 CH341-bridge UF2.
# Needs: git, make, python3, arm-none-eabi-gcc (+ newlib).
#   Debian/Ubuntu: apt install gcc-arm-none-eabi libnewlib-arm-none-eabi
#   NixOS:         nix-shell -p gcc-arm-embedded gnumake python3 git
set -euo pipefail

TINYUSB_REV=93aa62bce0f2708c9d0794b1947f50f9f1a0ac0d
HERE="$(cd "$(dirname "$0")" && pwd)"
TUSB="${TINYUSB_DIR:-$HERE/.tinyusb}"

if [ ! -d "$TUSB/.git" ]; then
  git clone https://github.com/hathach/tinyusb.git "$TUSB"
fi
git -C "$TUSB" fetch -q origin "$TINYUSB_REV" || true
git -C "$TUSB" checkout -q "$TINYUSB_REV"
( cd "$TUSB" && python3 tools/get_deps.py nrf )

# TinyUSB's make system only finds boards and apps inside its own tree
rm -rf "$TUSB/hw/bsp/nrf/boards/heltec_t114" "$TUSB/examples/device/t114_ch341"
cp -r "$HERE/board/heltec_t114" "$TUSB/hw/bsp/nrf/boards/heltec_t114"
cp -r "$HERE/app" "$TUSB/examples/device/t114_ch341"

make -C "$TUSB/examples/device/t114_ch341" BOARD=heltec_t114 -j"$(nproc)" all uf2 "$@"

mkdir -p "$HERE/out"
cp "$TUSB/examples/device/t114_ch341/_build/heltec_t114/t114_ch341.uf2" "$HERE/out/"
cp "$TUSB/examples/device/t114_ch341/_build/heltec_t114/t114_ch341.hex" "$HERE/out/"
echo "Built: $HERE/out/t114_ch341.uf2"
