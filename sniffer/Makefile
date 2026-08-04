#
#     Helper Makefile for the USB sniffer project
#

PROJECT = sniffer

#
#     Default target: build and run
#
$(PROJECT):
	cd build; cmake --build . --parallel 32 && cd .. && build/$(PROJECT) -o TRACE

#
#     Initialize the build directory with Ninja
#
init:
	rm -rf build; mkdir build; cd build; cmake -G Ninja ..

#
#     Build only (no run)
#
build/$(PROJECT):
	cd build; cmake --build . --parallel 32

#
#     "run" target — start the sniffer (usbmon mode)
#
r:
	./build/$(PROJECT)

#
#     "run-hw" — start the sniffer with hardware sniffer (-s)
#
rh:
	./build/$(PROJECT) -s

#
#     "run-hs" / "run-fs" / "run-ls" — run with a specific speed (hardware mode)
#
hs:
	./build/$(PROJECT) -s --speed hs

fs:
	./build/$(PROJECT) -s --speed fs

ls:
	./build/$(PROJECT) -s --speed ls

#
#     "clean" target
#
clean:
	rm -rf build

#
#     Install udev rules for the sniffer hardware
#
udev:
	sudo cp usb-sniffer/bin/90-usb-sniffer.rules /etc/udev/rules.d/
	sudo udevadm control --reload-rules
	sudo udevadm trigger
