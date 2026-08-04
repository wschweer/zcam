# Umstrukturierung Machine — ERLEDIGT

Bisher enthielt Machine einen Pointer auf Laser. Machine ist nun die Basisklasse von
Laser. Ebenso ist LaserEngine Teil von Laser, welches die Basisklasse für Laser-Varianten
darstellt. Es ergibt sich folgende Struktur:

- QObject
  - class Machine : public QObject        (virtuelle Klasse)
    - class Laser : public Machine        (virtuelle Klasse)
      - class LaserBJJCZ : public Laser
      - class LaserRKQ : public Laser
    - class MachineGCode : public Machine      (neu)


## Implementierung

Die `LaserEngine`-Klasse wurde aufgelöst. Ihre Funktionalität wurde in die `Laser`-
Basisklasse integriert:

- **laserengine.h** — Enthält nur noch Datentypen und Hilfsstrukturen (LaserPath,
  LaserParameterSet, ParameterType, Pulse33, LaserStatusFlags, etc.). Die
  `LaserEngine`-Klasse selbst wurde entfernt.
- **laser.h** — `Laser` erbt nun von `Machine` (statt von `QObject`). Die
  LaserEngine-Schnittstelle (init, exit, stop, startFraming, etc.) ist als
  pure virtual Methoden in `Laser` definiert. Die Pulse-Tabelle ist eine statische
  Methode von `Laser`. Die Laser-Status-Properties (enabled, framing, marking,
  testMode, dryRun, etc.) sind direkt in `Laser` definiert.
- **laser.cpp** — Implementiert die Framing/Marking-State-Machine und die
  Hintergrund-Threads. Ruft die abstrakten Engine-Methoden (startFramingEngine,
  stopMarkingEngine, etc.) auf, die von den konkreten Laser-Varianten implementiert
  werden.
- **laser_bjjcz.h/.cpp** — `LaserBJJCZ` erbt von `Laser` (statt von `LaserEngine`).
  Implementiert die USB-Kommunikation und alle BJJCZ-spezifischen Befehle.
- **laser_rkq.h/.cpp** — `LaserRKQ` erbt von `Laser` (statt von `LaserEngine`).
  Implementiert die Ethernet-Kommunikation via libpcap.

### Machine als virtuelle Basisklasse

- **machine.h** — `Machine` hat `virtual ~Machine() = default` und die
  `PROPV(Laser*, laser, nullptr)` Property wurde entfernt. Eine statische
  Factory-Methode `Machine::create(zcam, machineType, boardType)` wurde
  hinzugefügt, die die korrekte konkrete Unterklasse erzeugt.
- **machine.cpp** — Verwendet `metaObject()` statt `&Machine::staticMetaObject`
  für die JSON-Serialisierung, so dass die korrekte Metatabelle der konkreten
  Unterklasse verwendet wird. Die `fromJson`-Methode erzeugt keinen `Laser` mehr.

### MachineGCode (neu)

- **machinegcode.h/.cpp** — Neue konkrete `Machine`-Subklasse für G-Code CNC
  Maschinen. Verwendet die `Machine`-Basisklassen-Implementierung für
  `toJson`/`fromJson`/`properties`.

### Machines

- **machines.cpp** — Verwendet `Machine::create()` Factory, um die korrekte
  `Machine`-Subklasse basierend auf `type` und `boardType` aus dem JSON zu erzeugen.

### Anpassungen an Anwendungen

- **materialtest.cpp** — Verwendet `Laser::pulseTable()` (statisch) statt
  `machine()->laser()->engine()->pulseTable()`.
- **inspector_model.cpp** — Verwendet `qobject_cast<Laser*>()` um die
  `laserPulseList()` vom Machine-Objekt aufzurufen.
- **LaserPanel.qml** — Die `laser` Property bezieht sich direkt auf das Machine-
  Objekt (da Laser nun von Machine erbt). Die `toString()`-Methode prüft, ob
  das Machine-Objekt ein Laser ist.
- **PropertyEditor.qml** — Die `freqModel()`-Funktionen verwenden
  `ZCam.project?.machine?.laserPulseList` statt
  `ZCam.project.laser.engine.laserPulseList`.

### CMakeLists.txt

- Neue Dateien `src/machinegcode.cpp`/`src/machinegcode.h` wurden hinzugefügt.
- Der doppelte Eintrag `src/laser.h` wurde entfernt.


# Umstrukturierung der JSON Property Listen — ERLEDIGT

In der Gui werden Properties wie folgt angeordnet: Eine "row" besteht aus einem oder mehreren
Properties "cells". Eine "cell" kann den typ "empty" haben und nimmt dann nur leeren Platz ein.
Eine "row" hat ein Label und eine "cell" ein "sublabel" welches optional ist.

Rows können in mehreren Spalten angeordnet werden ("columns"). "columns" ist optional und
default ist "1".
Eine Row kann leer sein "{}" und nimmt in der GUI dann nur Platz ein.

Beispiel:

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
                     },
                    ]
              ]

## Implementierung

Die C++ Routinen wurden angepasst, um sowohl das neue "rows"/"cells" Format als auch das
alte "items"/"row" Format zu unterstützen. Beide Formate können gleichzeitig verwendet werden,
wobei das alte Format als Rückfalloption dient.

### Geänderte Dateien:

- `src/propertyjson.h` — unverändert (Schnittstelle bleibt gleich)
- `src/propertyjson.cpp` — `collectPropertyNames()` und `parseAllPropertyNames()` unterstützen jetzt "rows"/"cells"
- `src/inspector_model.h` — `ColumnItem` um `isEmpty` Feld erweitert
- `src/inspector_model.cpp` — `parseProperties()` verarbeitet "rows"/"cells" Format; `connectPropertySignals()` überspringt "empty" Einträge; `setData()` blockiert "empty" Einträge
- `src/machinemodel.cpp` — `parseProperties()` verarbeitet "rows"/"cells" Format
- `src/configmodel.cpp` — `parseProperties()` verarbeitet "rows"/"cells" Format
- `src/layersettingmodel.cpp` — `parseProperties()` verarbeitet "rows"/"cells" Format
- `qml/PropertyEditor.qml` — `propMetaMap` verarbeitet "rows"/"cells"; "empty" propName wird zu `emptyDelegate` geroutet; `sublabel` wird für Unter-Property-Labels unterstützt (mit Fallback auf `label`)