//=============================================================================
//  ZCam - manufacturing tool for G-code machines and Fiber Laser
//
//  Copyright (C) 2025-2026 Werner Schweer
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#pragma once

#include "element3d.h"
#include "stock.h"
#include "tessgeometry.h"
#include "clipper2/clipper.h"

#include <QColor>
#include <QList>
#include <QVector2D>

//---------------------------------------------------------
//   Cam
//---------------------------------------------------------

class Cam : public Element3d
      {
      Q_OBJECT

      PROPV(double, raster, 1.0)
      PROPV(int, panelRows, 1)
      PROPV(int, panelColumns, 1)
      PROPV(double, panelHDistance, 0.0)
      PROPV(double, panelVDistance, 0.0)
      PROPV(Stock*, stock, nullptr)
      PROPV(bool, perspective, false)                   ///< project with central (perspective) projection
      PROPV(double, projectionHeight, 1000.0)           ///< viewpoint height [mm] above the z=0 plane
      PROPV(QVector2D, viewCenter, QVector2D(0.0, 0.0)) ///< foot point (x,y) [mm] of the viewpoint on z=0

      // Colours for each LaserMop layer, in geometry-subset order.
      // Populated by updateCam(); used by CamShape.qml to build per-layer
      // materials so each Mop's CAM data is drawn in its own colour.
      QList<QColor> _layerColors;
      Q_PROPERTY(QList<QColor> layerColors READ layerColors NOTIFY layerColorsChanged)

      inline static constexpr std::string_view _properties {R"({
    "class": "Cam",
    "rows": [
        {
            "label": "State",
            "cells": [
                {
                    "type": "bool",
                    "default": true,
                    "name": "show",
                    "sublabel": "Show"
                },
                {
                    "type": "bool",
                    "default": true,
                    "name": "burn",
                    "sublabel": "Burn"
                }
            ]
        },
        {
            "label": "Pos.",
            "cells": [
                {
                    "name": "pos",
                    "type": "vector3d",
                    "scriptable": true,
                    "unit": "mm",
                    "default": [
                        0.0,
                        0.0,
                        0.0
                    ]
                }
            ]
        },
        {
            "label": "Rot.",
            "cells": [
                {
                    "name": "rot",
                    "type": "vector3d",
                    "scriptable": true,
                    "unit": "°",
                    "min": 0.0,
                    "max": 360,
                    "default": [
                        0.0,
                        0.0,
                        0.0
                    ]
                }
            ]
        },
        {
            "label": "Scale",
            "cells": [
                {
                    "name": "scale",
                    "type": "scale",
                    "scriptable": true,
                    "min": 0.001,
                    "max": 1000,
                    "default": [
                        1.0,
                        1.0,
                        1.0
                    ]
                }
            ]
        },
        {
            "label": "Lock",
            "cells": [
                {
                    "name": "lockScale",
                    "type": "lockScale",
                    "default": 2
                }
            ]
        },
        {
            "cells": [
                {
                    "name": "line",
                    "type": "line"
                }
            ]
        },
        {
            "label": "Panels",
            "cells": [
                {
                    "type": "int",
                    "scriptable": true,
                    "min": 1,
                    "max": 100,
                    "default": 1,
                    "name": "panelRows",
                    "sublabel": "Rows"
                },
                {
                    "type": "int",
                    "scriptable": true,
                    "min": 1,
                    "max": 100,
                    "default": 1,
                    "name": "panelColumns",
                    "sublabel": "Cols"
                }
            ]
        },
        {
            "label": "Dist.",
            "cells": [
                {
                    "type": "float",
                    "scriptable": true,
                    "unit": "mm",
                    "min": 0.0,
                    "max": 50.0,
                    "default": 0.0,
                    "name": "panelHDistance",
                    "sublabel": "H"
                },
                {
                    "type": "float",
                    "scriptable": true,
                    "unit": "mm",
                    "min": 0.0,
                    "max": 50.0,
                    "default": 0.0,
                    "name": "panelVDistance",
                    "sublabel": "V"
                }
            ]
        },
        {
            "cells": [
                {
                    "name": "line",
                    "type": "line"
                }
            ]
        },
        {
            "label": "3D",
            "cells": [
                {
                    "type": "bool",
                    "default": false,
                    "name": "perspective",
                    "sublabel": " "
                },
                {
                    "name": "cameraCapture",
                    "type": "cameraCapture",
                    "sublabel": " "
                },
                {
                    "type": "float",
                    "scriptable": true,
                    "unit": "mm",
                    "min": 1.0,
                    "max": 100000.0,
                    "default": 1000.0,
                    "name": "projectionHeight",
                    "sublabel": "h"
                }
            ]
        },
        {
            "label": "Center",
            "cells": [
                {
                    "name": "viewCenter",
                    "type": "vector2d",
                    "scriptable": true,
                    "unit": "mm",
                    "default": [
                        0.0,
                        0.0
                    ]
                }
            ]
        }
    ]
                  })"};

    signals:
      void panelChanged();
      void layerColorsChanged();

    public:
      Cam(ZCam*, Element* parent = nullptr);
      virtual QString typeName() override { return QStringLiteral("cam"); }
      virtual const std::string_view properties() const override { return _properties; }
      Q_INVOKABLE virtual bool visible() const override { return true; }
      /// Returns the per-layer Mop colours, in subset order.
      QList<QColor> layerColors() const { return _layerColors; }
      /// Recalculate all cam data (panel layout, fixture, laser layers).
      /// Collects geometry from all LaserLayer children, arranges them
      /// in a panel grid (rows × columns with distance offsets), and
      /// stores the combined line geometry in this Cam's _geometry.
      Clipper2Lib::PathD convexHull() const;
      Clipper2Lib::RectD boundingBox() const;
      /// Recalculate all cam data (panel layout, fixture, laser layers).
      /// This is expensive and must be triggered explicitly — either by
      /// the manual refresh button (ZCam::refreshCam) or at startup.
      /// Property changes only set the camDirty flag so the user knows
      /// a refresh is pending.
      Q_INVOKABLE void updateCam();
      /// Adopt the live view-camera from the 3D canvas (View3DPanel).
      /// Reads the camera eye / root scale mirrored into ZCam by
      /// ZCam::updateViewCamera(), derives viewCenter (the perpendicular
      /// foot of the camera on z=0 in root-local mm) and projectionHeight,
      /// assigns both properties, marks the cam data dirty and triggers a
      /// recalculation so the laser projection matches the canvas view.
      Q_INVOKABLE void grabCameraView();
      };