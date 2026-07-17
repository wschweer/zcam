** Cam reorganisation

LaserLayer() und Fixture() sollen _geometry nicht mehr mit daten füllen und
damit nicht mehr im 3d canvas direkt sichtbar sein. Dieses Datensammeln über
alle Children, die LaserLayer sind macht nun Cam().

LaserLayer->update() wird also durch eine Datensammelfunktion ersetzt, die die
bisherigen _geometry daten an Cam() zurückgibt.

Cam ordnet die gesammelten Daten in ein grid aus rows * columns an mit den
konfigurierten werten distanceX distanceY als Abstand (durch kopieren in jede
dieser Kacheln)

Dann kann auch eine passende Framing Linie um alle daten gelegt werden. Dies
Linie bildet die _geometry von Framing().
