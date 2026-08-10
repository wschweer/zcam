# Development
## Overview

### Technology and Tools

- C++23
- Qt 6.11
- QML
- Qt Quick 3D

The GUI uses Qt 6.11 with QML for the UI, rather than the classic approach via
QWidgets. The main window is a 3D Canvas built with QML. However, the bulk of the
code is C++.

### Architecture

```
QObject
├── Element                    — base class for all project elements
│   └── Element3d             — 3D element with pos/rot/scale/geometry
│       ├── Cad               — CAD input container
│       ├── Cam               — CAM output container
│       ├── Group             — a CAD layer (group of shapes)
│       ├── Rectangle         — rectangle shape
│       ├── Polygon           — polygon/line shape
│       ├── Ellipse           — ellipse/circle shape
│       ├── Text              — text shape
│       ├── Fixture           — fixture with framing
│       ├── Framing           — framing contour element
│       ├── Stock             — stock material definition
│       ├── CameraElement     — camera device + overlay
│       └── Recipe            — laser layer with recipe + overrides
├── Machine (virtual)         — base class for all machine types
│   ├── Laser (virtual)       — laser machine with framing/marking FSM
│   │   ├── LaserBJJCZ        — BJJCZ USB laser
│   │   └── LaserRKQ          — RKQ Ethernet laser
│   └── MachineGCode          — G-code CNC machine
├── ZCam                       — top-level application controller
├── Project                    — owns CAD, CAM, Fixture, undo stack, Machine
├── Machines                   — container for machine JSON files
└── InspectorModel/MachineModel — QAbstractListModel for QML
```

### Element

The class `Element` is the base class for all project elements in ZCam. It
implements a Geometry Element (`TessGeometry`), whose base class is
`QQuick3DGeometry()`, which is required by Qt Quick 3D to build the 3D scene.

### Interface Qt Quick 3D — C++

The basic 3D element is a `Node`. The 3D Canvas builds a tree structure of nodes
that have their counterparts on the C++ side. The C++ module and the canvas are
connected via signals/slots. The following signals in ZCam control the canvas:

```c++
      void remove3dElement(Element*);           // signal 3D-GUI: remove element from scene graph
      void add3dElement(Element*);              // signal 3D-GUI: add new element to scene graph
      void addSubElement(Element*, Element*);   // signal 3D-GUI: add new sub-element to scene graph
      void rootElementChanged(Element*);        // signal 3D-GUI: rebuild scene graph
```

The root of the node tree can be found in `ZCam::topLevel()`:

```cpp
  class ZCam : public QObject
      {
      ...
      Q_PROPERTY(TopLevel* topLevel READ topLevel WRITE setTopLevel NOTIFY topLevelChanged)
      ...
      TopLevel* _topLevel{nullptr};
      ...
```

`setTopLevel(...element...)` triggers the `topLevelChanged()` signal, which
signals the QML part in `ProjectTree.qml`: `base.onRootElementChanged()` that
the project needs to be re-rendered.

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

### Properties

The list of element properties is needed in several places:

- for constructing the QML GUI elements
- for reading/writing the project file

The list of properties available for an element and their attributes are
configured in a JSON string and can be retrieved via
`std::string_view Element3d::properties()`.

#### Property-JSON Format

A "row" consists of one or more property "cells". A "cell" can have the type
"empty" and takes up only space. A "row" has a label and a "cell" an optional
"sublabel". Rows can be arranged in multiple columns ("columns" is optional,
default is 1). A row can be empty "{}" and takes up only space in the GUI.

Example:

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

### Machine Class Hierarchy

The `Machine` class is the virtual base class for all machine types:

```
QObject
└── Machine (virtual)
    ├── Laser (virtual)
    │   ├── LaserBJJCZ   — USB communication (BJJCZ boards)
    │   └── LaserRKQ     — Ethernet communication via libpcap (RKQ-LM-441)
    └── MachineGCode     — G-code CNC machine
```

`Machine::create()` is a factory method that creates the correct concrete
subclass based on the machine type and board type strings. JSON serialization
uses `metaObject()` to ensure the correct metatable of the concrete subclass is
used.

### Laser State Machine

The laser operates through a state machine with the following states:

| State | Description |
| :--- | :--- |
| `Off` | Laser is not initialized |
| `Idle` | Laser is on but not framing or marking |
| `Framing` | Framing thread is running (outlining geometry) |
| `Marking` | Marking thread is running (engraving) |

Transitions are controlled by `init()`, `exit()`, `startFraming()`,
`startMarking()` and `stop()`. The concrete engine methods
(`initEngine()`, `startFramingEngine()`, `stopMarkingEngine()`, etc.) are
implemented by `LaserBJJCZ` and `LaserRKQ`.

### Coding Conventions

- C++23, Qt6, QML
- `PROP(T, name)` / `PROPV(T, name, value)` macros for Q_PROPERTY with NOTIFY
- `nlohmann::json` for serialization
- `std::format`-based logging (logger.h)
- No heavy OOP hierarchies; prefer value semantics and composition
- Every C++ class definition and function/method starts with a header comment
  block
- Every C++ file starts with the standard ZCam copyright header

### Third-Party Code

For convenience, ZCam includes some third-party sources:

- **clipper2** by Angus Johnson — License: [Boost](https://www.boost.org/LICENSE_1_0.txt)
- **tess2** by Mikko Mononen — License: SGI FREE SOFTWARE LICENSE B (Version 2.0, Sept. 18, 2008)
- **libdxfrw** — DXF file reading/writing
- **nanosvg** — SVG file parsing
- **libpcap** — Raw Ethernet frame capture (for RKQ-LM-441 communication)