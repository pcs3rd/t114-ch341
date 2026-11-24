# Host-side test: meshtasticd's libpinedio-usb (host) <-> ch341_emu.c (device) over a fake USB bus.
LIBCH341_REV = 027cde5d9c0cecc91fffbd2d1ed4e41e32eeda79
LIBCH341 ?= .libch341

CFLAGS = -std=gnu11 -O1 -g -Wall -Wno-pointer-sign -Wno-unused-parameter -pthread \
         -fsanitize=address,undefined -Imock -I../app/src -I$(LIBCH341)

SRC = test.c fake_usb.c fake_radio.c ../app/src/ch341_emu.c ../app/src/status.c ../app/src/usb_descriptors.c $(LIBCH341)/libpinedio-usb.c

run: ch341_test
	./ch341_test

ch341_test: $(SRC) $(wildcard mock/*.h mock/*/*.h ../app/src/*.h) | $(LIBCH341)/libpinedio-usb.c
	$(CC) $(CFLAGS) -o $@ $(SRC)

$(LIBCH341)/libpinedio-usb.c:
	git clone -q https://github.com/meshtastic/libch341-spi-userspace.git $(LIBCH341)
	git -C $(LIBCH341) checkout -q $(LIBCH341_REV)

clean:
	rm -f ch341_test
.PHONY: run clean
