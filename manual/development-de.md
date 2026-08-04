# Entwicklung
## Übersicht

### Technologie und Tools

- C++23
- Qt 6.11
- QML
- Qt Quick 3D

Die GUI nutzt Qt 6.11 mit QML für die UI und nicht den klassischen Ansatz über
QWidgets. Das Hauptfenster ist ein 3D Canvas, der mit QML aufgebaut wird.
Die Hauptmasse des Codes ist jedoch C++.

### Architektur

```
QObject
├── Element                    — Basisklasse aller Projektelemente
│   └── Element3d             — 3D-Element mit pos/rot/scale/geometry
│       ├── Cad               — CAD-Input-Container
│       ├── Cam               — CAM-Output-Container
│       ├── Group             — ein CAD-Layer (Gruppe von Formen)
│       ├── Rectangle         — Rechteck-Form
│       ├── Polygon           — Polygon/Linien-Form
│       ├── Ellipse           — Ellipsen/Kreis-Form
│       ├── Text              — Text-Form
│       ├── Fixture           — Fixture mit Framing
│       ├── Framing           — Framing-Kontur-Element
│       ├── Stock             — Rohteil-Definition
│       ├── CameraElement     — Kameragerät + Overlay
│       └── Recipe            — Laser-Layer mit Rezept + Overrides
├── Machine (virtuell)        — Basisklasse für alle Maschinentypen
│   ├── Laser (virtuell)      — Laser-Maschine mit Framing/Marking-FSM
│   │   ├── LaserBJJCZ        — BJJCZ USB-Laser
│   │   └── LaserRKQ          — RKQ Ethernet-Laser
│   └── MachineGCode          — G-Code CNC-Maschine
├── ZCam                       — Top-Level Anwendungs-Controller
├── Project                    — besitzt CAD, CAM, Fixture, Undo-Stack, Machine
├── Machines                   — Container für Maschinen-JSON-Dateien
└── InspectorModel/MachineModel — QAbstractListModel für QML
```

### Element

Die Klasse `Element` ist die Basisklasse aller grafischen Elemente in ZCam. Sie implementiert ein
Geometry Element (`TessGeometry`), dessen Basisklasse `QQuick3DGeometry()` ist, und das von Qt
Quick 3D zum Aufbau der 3D-Szene benötigt wird.

### Interface Qt Quick 3D — C++

Das Basis-3D-Element ist eine `Node`. Der 3D Canvas baut eine Baumstruktur von Nodes auf, die
ihr Gegenstück auf der C++-Seite haben. C++-Modul und Canvas sind über Signal/Slots miteinander
verbunden. Folgende Signale in ZCam steuern den Canvas:

```c++
      void remove3dElement(Element*);           // Signal 3D-GUI: Element aus Scene Graph entfernen
      void add3dElement(Element*);              // Signal 3D-GUI: neues Element zum Scene Graph hinzufügen
      void addSubElement(Element*, Element*);   // Signal 3D-GUI: neues Sub-Element zum Scene Graph hinzufügen
      void rootElementChanged(Element*);        // Signal 3D-GUI: Scene Graph neu aufbauen
```

Die Wurzel des Node-Baumes findet sich in `ZCam::topLevel()`:

```cpp
  class ZCam : public QObject
      {
      ...
      Q_PROPERTY(TopLevel* topLevel READ topLevel WRITE setTopLevel NOTIFY topLevelChanged)
      ...
      TopLevel* _topLevel{nullptr};
      ...
```

`setTopLevel(...element...)` löst das Signal `topLevelChanged()` aus, welches dem QML-Teil
in `ProjectTree.qml`: `base.onRootElementChanged()` signalisiert, dass das Projekt neu
gerendert werden muss.

```qml
function onRootElementChanged(e) {
   // alten Baum zerstören
   var n = base.children.length;
   for (var i = 0; i < n; ++i) {
       base.children[i].destroy(100);
   }
   if (e)
       base.addElement(base, e);    // Shape-Komponente hinzufügen
}
```

### Properties

Die Liste der Element-Properties wird an mehreren Stellen benötigt:

- zur Konstruktion der QML-GUI-Elemente
- zum Schreiben/Lesen der Projektdatei

Die Liste der für ein Element verfügbaren Properties und ihre Eigenschaften werden in einem
JSON-String konfiguriert und können über `std::string_view Element3d::properties()` abgerufen
werden.

#### Property-JSON-Format

Eine "row" besteht aus einem oder mehreren Properties "cells". Eine "cell" kann den Typ "empty"
haben und nimmt nur leeren Platz ein. Eine "row" hat ein Label und eine "cell" ein optionales
"sublabel". Rows können in mehreren Spalten angeordnet werden ("columns" ist optional, Default
ist 1). Eine Row kann leer sein "{}" und nimmt in der GUI dann nur Platz ein.

Beispiel:

```json
{
    "class": "Text",
    "columns": 1,
    "rows": [
        {
            "label": "Location",
            "cells": [
                {
                    "name": "property1",
                    "sublabel": "x",
                    "type": "float"
                },
                {
                    "name": "property2",
                    "sublabel": "y",
                    "type": "double"
                }
            ]
        },
        {
            "label": "Rotation",
            "cells": [
                {
                    "name": "property3",
                    "type": "vector3d"
                }
            ]
        }
    ]
}
```

Liste der Property-Typen:

| Typ | Beschreibung |
| ---- | ---- |
| `text` | Texteingabe |
| `bool` | Checkbox |
| `int` | Spinbox |
| `float` | DoubleSpinBox |
| `vector3d` | Drei Double-Spinboxen |
| `scale` | Drei Double-Spinboxen (mit Lock-Modus) |
| `vector2d` | Zwei Double-Spinboxen |
| `font` | Schriftart-Auswahl |
| `halign` | Horizontale Ausrichtung |
| `multiline` | Mehrzeiliger Text |
| `singleline` | Einzeiliger Text |
| `line` | Linien-/Stiftstil |
| `color` | Farbauswahl |
| `layer` | CAD-Layer |
| `recipe` | Rezept |
| `machine` | Maschine |
| `machineName` | Maschinenname (nur Lesezugriff) |
| `machineType` | Maschinentyp |
| `override` | Override-Parameter-Typ |
| `pulsewidth` | Pulsbreite |
| `lineJoin` | Linienverbindungs-Stil |
| `lineEnd` | Linienend-Stil |
| `lockScale` | Skalierungs-Lock-Modus |
| `lockSize` | Größen-Lock-Modus |
| `framingType` | Framing-Typ |
| `cameraName` | Kameragerät-Name |
| `cameraView` | Live-Kamera-Ansicht |
| `empty` | Leerer Platzhalter |

### Machine-Klassenhierarchie

Die `Machine`-Klasse ist die virtuelle Basisklasse für alle Maschinentypen:

```
QObject
└── Machine (virtuell)
    ├── Laser (virtuell)
    │   ├── LaserBJJCZ   — USB-Kommunikation (BJJCZ-Boards)
    │   └── LaserRKQ     — Ethernet-Kommunikation via libpcap (RKQ-LM-441)
    └── MachineGCode     — G-Code CNC-Maschine
```

`Machine::create()` ist eine Factory-Methode, die die korrekte konkrete Unterklasse basierend
auf den Maschinentyp- und Board-Typ-Strings erstellt. Die JSON-Serialisierung verwendet
`metaObject()`, um sicherzustellen, dass die korrekte Metatabelle der konkreten Unterklasse
verwendet wird.

### Laser-State-Machine

Der Laser arbeitet über eine State-Machine mit folgenden Zuständen:

| Zustand | Beschreibung |
| :--- | :--- |
| `Off` | Laser ist nicht initialisiert |
| `Idle` | Laser ist an, aber nicht beim Framing oder Marking |
| `Framing` | Framing-Thread läuft (Geometrie wird umfahren) |
| `Marking` | Marking-Thread läuft (Gravur wird ausgeführt) |

Übergänge werden gesteuert durch `init()`, `exit()`, `startFraming()`, `startMarking()` und
`stop()`. Die konkreten Engine-Methoden (`initEngine()`, `startFramingEngine()`,
`stopMarkingEngine()`, etc.) werden von `LaserBJJCZ` und `LaserRKQ` implementiert.

### Coding-Konventionen

- C++23, Qt6, QML
- `PROP(T, name)` / `PROPV(T, name, value)` Makros für Q_PROPERTY mit NOTIFY
- `nlohmann::json` für Serialisierung
- `std::format`-basiertes Logging (logger.h)
- Keine schweren OOP-Hierarchien; bevorzuge Wert-Semantik und Komposition
- Jede C++-Klassendefinition und jede Funktion/Methode beginnt mit einem Header-Kommentarblock
- Jede C++-Datei beginnt mit dem Standard-ZCam-Copyright-Header

### Third-Party-Code

Zur Bequemlichkeit enthält ZCam einige Third-Party-Quellen:

- **clipper2** von Angus Johnson — Lizenz: [Boost](https://www.boost.org/LICENSE_1_0.txt)
- **tess2** von Mikko Mononen — Lizenz: SGI FREE SOFTWARE LICENSE B (Version 2.0, Sept. 18, 2008)
- **libdxfrw** — DXF-Datei lesen/schreiben
- **nanosvg** — SVG-Datei parsen
- **libpcap** — Raw-Ethernet-Frame-Capture (für RKQ-LM-441-Kommunikation)
