# NPED — ZCam Project Summary

## Overview
ZCam is a Qt6/QML-based manufacturing tool for G-code CNC machines and fiber laser
engraving. It manages projects with CAD geometry, CAM processing, laser/CNC machine
configurations, recipes, and a 3D viewport.

## Build
```bash
cd build && cmake .. && cmake --build .
```
Or use the Ninja build system (build.ninja is pre-generated).

## Key Architecture
- **Machine** (QObject): Base class for all machine types. Contains shared properties
  (travel, precision, galvo params, etc.). Virtual class.
- **Laser : Machine**: Laser-specific machine with framing/marking state machine,
  background threads, and an engine (LaserEngine) for board communication.
- **LaserBJJCZ : Laser**, **LaserRKQ : Laser**: Concrete laser implementations.
- **MachineGCode : Machine**: G-code CNC machine (new).
- **Machines**: Container that loads/saves machine JSON files.
- **Project**: Top-level element owning CAD, CAM, Fixture, undo stack, and Machine.
- **InspectorModel / MachineModel**: QAbstractListModel exposing properties to QML.
- **Property JSON**: Each class defines a `properties()` JSON string describing
  the GUI layout (rows/cells format).
- **CameraElement : Element3d**: Manages an attached Linux webcam (V4L2/Qt
  Multimedia). The camera device is selected from the list of available inputs
  (combobox in the inspector, `cameraName` type), the live image is shown in the
  inspector (`cameraView` type, zoomable/pannable) and drives the camera overlay
  in the XY plane (pos/rot/overlaySize + trapezX/trapezY keystone correction via
  CameraOverlayGeometry). CameraTextureData binds to the element for the 3D
  overlay texture; visibility gates camera capture.

## Coding Conventions
- C++23, Qt6, QML
- `PROP(T, name)` / `PROPV(T, name, value)` macros for Q_PROPERTY with NOTIFY
- `nlohmann::json` for serialization
- `std::format`-based logging (logger.h)
- No heavy OOP hierarchies; prefer value semantics and composition