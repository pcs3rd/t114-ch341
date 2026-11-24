MCU_VARIANT = nrf52840
CFLAGS += -DNRF52840_XXAA

# The T114 ships with the Adafruit nRF52 UF2 bootloader + S140 v6 SoftDevice,
# so the application is linked at 0x26000 like the Feather nRF52840.
LD_FILE = ${FAMILY_PATH}/linker/nrf52840_s140_v6.ld
