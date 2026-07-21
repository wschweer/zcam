---

<span style="font-size:48px">ZCad - Manual</span>

---

[TOC]

- [Entwicklung](development-de.md)
- [Installation](install-de.md)


## GUI
### 3D Panel
#### Navigation

| Action | Input |
| :--- | :--- |
| Scale  | `Ctrl` + Mouse wheel |
| Pan    | Middle mouse button + drag |
| Rotate | Right mouse button + drag |

Eine 3DConnexion "SpaceMouse" wird automatisch erkannt, wenn vorhanden.


#### Views

| Icon | Action | Icon | Action |
| :--- | :--- | :--- | :--- |
<img src="../icons/view-top.svg" style="width:18px;"/>        | Oben                    | <img src="../icons/view-bottom.svg" style="width:18px;"/>      | Unten
<img src="../icons/view-front.svg" style="width:18px;"/>      | Front                   | <img src="../icons/view-rear.svg" style="width:18px;"/>        | Rückseite
<img src="../icons/view-left.svg" style="width:18px;"/>       | Links                   | <img src="../icons/view-right.svg" style="width:18px;"/>       | Rechts
<img src="../icons/view-isometric.svg" style="width:18px;"/>  | Isometrische Projektion | <img src="../icons/view-perspective.svg" style="width:18px;"/> | Perspektivische Projektion
<img src="../icons/view-fullscreen.svg" style="width:18px;"/> | Skaliere auf Bildschirm |

-----

## Galvo Laser
### Arbeitsfläche und Koordinatensystem

Der Nullpunkt des Koordinatensystems liegt in der linken unteren Ecke des Arbeitsbereichs. ZCad verwendet
ein rein positives Koordinatensystem.

Der Arbeitsbereich (Scanfeld) hängt von der Brennweite der installierten Optik (F-Theta-Optik)  ab:

|Linsentyp | Brennweite | Scanfeld | Arbeitsabstand | Spot-Größe | Typischer Einsatzzweck & Empfehlung
|-----|-----|-----|-----|-----|-----|
| F-100         | 100 mm |70 x 70 mm   |ca. 115 mm     |Sehr klein (ca. 15–20 µm)  |Maximale Präzision, feine Tiefengravuren, Schneiden dünner Bleche. Ideal für 20W-Laser.                     |
| F-160 / F-163 | 160 mm |110 x 110 mm |ca. 175–185 mm |Klein (ca. 25–30 µm)       |Der Standard-Allrounder. Perfekte Balance aus Feldgröße, feinem Fokus und hoher Energiedichte.              |
| F-210 / F-220 | 210 mm |150 x 150 mm |ca. 230–250 mm |Mittel (ca. 35–40 µm)      |Guter Kompromiss, wenn das 110er Feld knapp zu klein ist, aber noch Gravurleistung benötigt wird.           |
| F-254         | 254 mm |175 x 175 mm |ca. 280–295 mm |Mittel-Groß (ca. 45–50 µm) |Für größere Werkstücke. Benötigt oft schon mindestens 30W oder 50W Laserleistung für tiefe Metallgravuren.  |
| F-290 / F-300 | 290 mm |200 x 200 mm |ca. 320–350 mm |Groß (ca. 55–60 µm)        |Große Flächen. Gravuren werden spürbar flacher. Hauptsächlich für Beschriftungen oder starke Laser (50W+).  |
| F-330 / F-350 | 330 mm |220 x 220 mm |ca. 350–390 mm |Sehr Groß (ca. 65–75 µm)   |Große Bauteile, Gehäusebeschriftungen, Farbumschlag auf Kunststoffen.                                       |
| F-420         | 420 mm |300 x 300 mm |ca. 460–480 mm |Sehr Groß (ca. 80–90 µm)   |Maximale Feldgröße. Nur für sehr starke Laser (50W–100W) oder reine Oberflächenmarkierungen sinnvoll.       |
