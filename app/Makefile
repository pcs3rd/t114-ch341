include ../../../hw/bsp/family_support.mk

INC += src

EXAMPLE_SOURCE += $(wildcard src/*.c)
SRC_C += $(addprefix $(EXAMPLE_PATH)/, $(EXAMPLE_SOURCE))

# family.mk sets CONFIG_GPIO_AS_PINRESET, which makes SystemInit write UICR.PSELRESET on first
# boot. The T114 bootloader deliberately leaves P0.18 as a GPIO, so don't touch UICR.
CFLAGS := $(filter-out -DCONFIG_GPIO_AS_PINRESET,$(CFLAGS))

include ../../../hw/bsp/family_rules.mk
