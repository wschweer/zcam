# Installation
## Galvo Laser
### RKQ-LM-441 Board

UV lasers are often equipped with the RKQ-LM-441 board. The software RK-CAD is
typically used, which notably enables internal engraving of crystal blocks.
These boards are connected to the computer via Ethernet.

For trouble-free operation, I recommend using a dedicated network card for the
computer–laser connection:

- Install a dedicated network card.
- Configure the network card in the Network Manager so it is not used by the
  system (i.e., no automatic connection).
- Configure ZCam to use the RKQ controller and enter the network interface name
  (e.g. `enp11s0`).
- Grant ZCam the right to open raw sockets:

      sudo setcap cap_net_raw+ep /path/to/zcam

- Test whether the Connect button establishes a connection to the laser.

> [!WARNING]
> RKQ-LM-441-based devices are currently not supported. The protocol used
> there is undocumented and there is little information about it on the
> net. I have since converted my UV laser to a BJJCZ board, which makes it
> unlikely that I will provide an RKQ driver in the foreseeable future.

### BJJCZ Boards

This is the most common controller variant for Galvo lasers, made by Beijing
JCZ Technology. These boards are mostly used with EZCAD software and are mostly
LightBurn-compatible. They connect to the computer via USB.

To access the USB ports, you need to configure the appropriate permissions:

- Set up a `plugdev` group:

      sudo groupadd plugdev

- Add yourself as a member of the `plugdev` group:

      sudo usermod -aG plugdev $USER

- Connect the laser via USB and power it on. Find the Vendor-ID and Product-ID
  of your device:

      lsusb
      ws@zephyr:~/$ lsusb
      ...
      Bus 003 Device 005: ID 256f:c635 3Dconnexion SpaceMouse Compact
      Bus 003 Device 006: ID 9588:9899 BJJCZ USBLMCV2
      Bus 004 Device 001: ID 1d6b:0003 Linux Foundation 3.0 root hub
      ...

- Look for a BJJCZ USBLMCV2 entry.
- Configure udev by creating a file
  `/etc/udev/rules.d/70-fiber-laser.rules` with the detected Vendor- and
  Product-ID:

      SUBSYSTEM=="usb", ATTR{idVendor}=="9588", ATTR{idProduct}=="9899", MODE="0666", GROUP="plugdev"