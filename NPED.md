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
- **ImageElement : Element3d**: Displays a pixel-based image (PNG, JPEG, BMP,
  GIF, TIFF, WEBP) on the 3D canvas as a textured quad in the XY plane.
  ImagePlaneGeometry provides a unit quad with UVs; ImageTextureData loads the
  image file and uploads it as an RGBA8 texture. The element's scale represents
  the physical size in mm; on import the larger axis defaults to 100 mm with
  aspect-ratio preserved (lockScale = Lock). The filePath property stores the
  source path; the image is reloaded from disk on project load. Import via
  `ZCam::importFile()`, drag-drop on the 3D canvas, or `importImageAt()` for
  positioned placement.
- **ManualPanel (QML)**: Integrated manual viewer. Displays MkDocs-generated
  HTML inside a `WebEngineView`. The HTML is built by CMake from `manual/docs/*.md`
  via `mkdocs build` at configure time, then embedded as a Qt resource
  (`qrc:/manual/`). German is the primary language, English is an automatically
  translated variant. Language switching is done via buttons in the panel.
  Requires `Qt6::WebEngineQuick` (initialised in `main.cpp`).

## File Imports
- **SVG / DXF / BREP**: `ZCam::importFile()` dispatches by suffix to the
  respective importers (`svg.cpp`, `dxfimport.cpp`, `brepimport.cpp`).
- **Images (PNG, JPEG, BMP, GIF, TIFF, WEBP)**: `ImageImport::import()` /
  `ImageImport::importAt()` create an `ImageElement` in the CAD tree.
  Dispatched by suffix from `ZCam::importFile()` and drag-drop on the 3D canvas.
- **IPC-2581 (revision C)**: `importipc2581.cpp` parses the PCB "digital twin"
  XML. All layers become Groups with Polygon children (arcs/primitives are
  flattened) below a new import layer; negative polarity and Cutout geometry is
  imported as red filled polygons. A linked Recipe (laser layer) is added to the
  active fixture. Sniffed by root element so plain `.xml` imports are routed
  correctly (see `ImportIpc2581::isIpc2581File`).

## Coding Conventions
- C++23, Qt6, QML
- use camel case variable names
- abstract object names begin with a capital letter
- `PROP(T, name)` / `PROPV(T, name, value)` macros for Q_PROPERTY with NOTIFY
- `nlohmann::json` for serialization
- `std::format`-based logging (logger.h)

## New c++ classes and functions/methos:
Start every c++ class definition and every function/method with this header:

      #--------------------------------------------------------------------
      #     <function/class name>
      #--------------------------------------------------------------------

A c++ class is structured this way:   private - protected - public.

## New c++ files
Start every c++ file with this file header:

      //=============================================================================
      //  ZCam - manufactoring tool for G-code machines and Fiber Laser
      //
      //  Copyright (C) 2025-2026 Werner Schweer
      //
      //  This program is free software; you can redistribute it and/or modify
      //  it under the terms of the GNU General Public License version 2
      //  as published by the Free Software Foundation and appearing in
      //  the file LICENCE.GPL
      //=============================================================================

## C++ Header files
Protect header files with ```#pragma once```
