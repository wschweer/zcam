# Installation
## Galvo Laser
### RKQ-LM-441 Board

UV-Laser sind oft mit dem RKQ-LM-441 Board ausgestattet. Als Software wird RK-CAD
verwendet welche als Besonderheit die Innengravur von Kristallblöcken ermöglicht.
Diese Boards werden über Ethernet mit dem Rechner verbunden.

Für einen problemlosen Betrieb empfehle ich, eine separate Netzwerkkarte für die
Verbindung Rechner-Laser zu verwenden.

- installiere eine separate Netzwerkkarte
- konfiguriere die Netzwerkkarte im Netzwerkmananger so, das sie nicht vom
  System verwendet wird (d.h. keine automatische Verbindung)
- konfiguriere ZCam für die Verwendung des RKQ controllers und trage die
  verwendete Netzwerkschnitte ein (z.B. enp11s0)
- gib ZCad die Rechte, Raw-Sockets öffnen zu können:

      sudo setcap cap_net_raw+ep /pfad/zu/zcad

- teste, ob connect eine Verbindung zum Laser herstellen kann

### BJJCZ Boards

Dies ist die am weitesten verbreitete Controller Variante für Galvo Laser
von Beijing JCZ Technology. Diese Boards werden überwiegend mit der hauseigenen
Software EZCAD betrieben und sind meist LightBurn kompatibel.

Diese Boards werden über USB mit dem Rechner verbunden.

Um auf die USB Ports zugreifen zu können, musst du bestimmte Rechte konfigurieren.

- richte eine Grupper `plugdev` ein
- trage dich als Mitglied der Gruppe `plugdev` ein
- verbinde den Laser über USB mit dem Rechner
- schalte den Laser ein und finde die Vendor-Id und die Produkt-Id deines Geräts:

      lsusb

- konfiguriere udev, erstellen eine Datei `/etc/udev/rules.d/70-fiber-laser.rules`,
trage die ermittelte Vendor- und Produkt-Id ein:

      SUBSYSTEM=="usb", ATTR{idVendor}=="9588", ATTR{idProduct}=="9899", MODE="0666", GROUP="plugdev"
