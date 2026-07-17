-----
# ZCad - Manual

-----

[TOC]

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

# Entwicklung
## Übersicht

### Technologie und Tools

- C++24
- Qt 6.11
- QML
- Qt Quick 3D

Die GUI nutzt Qt 6.11 mit QML für die UI und nicht den klassischen Ansatz über QQWidgets.
Das Hauptfenster ist ein 3D Canvas der mit QML aufgebaut wird.
Die Hauptmasse des Codes ist jedoch C++.

### Element
Die Klasse Element ist die Basisklasse aller grafischen Elemente in ZCam. Sie implementiert ein
Geometry Element (TessGeometry), dessen Basisklasse QQuick3DGeometry() ist und von Qt Quick 3D
zum Aufbau der 3D-Szene benötigt wird.

### Interface Qt Quick 3D - C++

Das Basis-3D Element ist eine Node. Der 3D Canvas baut eine Baumstruktur von Nodes auf die
ihr Gegenstück auf der C++ Seite haben. C++ Modul und Canvas sind über Signal/Slots miteinander
verbunden. Folgende Signale in Zcam steuern den Canvas:

```c++
      void remove3dElement(Element*);           // signal 3d gui to remove an element from the scene graph
      void add3dElement(Element*);              // signal 3d gui to add a new element into the scene graph
      void addSubElement(Element*, Element*);   // signal 3d gui to add a new subelement into the scene graph
      void rootElementChanged(Element*);        // signal 3d gui to rebuild scene graph
```

Die Wurzel des Node-Baumes findet sich in Zcam::topLevel():

```cpp
  class ZCam : public QObject
      {
      ...
      Q_PROPERTY(TopLevel* topLevel READ topLevel WRITE setTopLevel NOTIFY topLevelChanged)
      ...
      TopLevel* _topLevel{nullptr};
      ...
```

`setTopLevel(...element...)` löst das Signal `topLevelChanged()` aus welches dem Qml Part
in ProjectTree.qml: `base.onRootElementChanged()` signalisiert, das das Projekt neu gerendered
werden muss.

```qml
function onRootElementChanged(e) {
   // destroy old tree
   var n = base.children.length;
   for (var i = 0; i < n; ++i) {
       base.children[i].destroy(100);
       }
   if (e)
       base.addElement(base, e);    // add Shape component
   }
```
