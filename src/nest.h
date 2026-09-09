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

#pragma once

#include "group.h"
#include "tessgeometry.h"

#include <QVector2D>

//---------------------------------------------------------
//   Nest
//    A nesting container that arranges its child elements within
//    a rectangular bin.  The bin size defaults to the current
//    machine's maxTravel (X, Y).  The nest() Q_INVOKABLE method
//    runs libnest2d to pack all child Element3d geometry into
//    the bin and applies the resulting transforms (position and
//    rotation) to each child.
//
//    Properties:
//      binSize  – QVector2D, the nesting bin dimensions in mm
//      spacing  – double, minimum spacing between packed items (mm)
//      allowRotation – bool, whether 90-degree rotations are allowed
//      arrange  – bool, whether items are auto-arranged on add
//
//    The nest area is visualised as a rectangle outline in the 3D
//    viewport (drawn via the selection geometry, always visible).
//
//    When the Nest is the current selection the 3D viewport shows
//    four corner handles (Anfasser) — one at each bin corner — that
//    resize the bin by dragging:  the corner opposite the dragged
//    handle stays fixed in world space, binSize and pos are updated
//    accordingly.  Index convention (same corner order as the
//    updateSelectionGeometry() rectangle):
//       0 = (0,0)   bottom-left
//       1 = (w,0)   bottom-right
//       2 = (w,h)   top-right
//       3 = (0,h)   top-left
//---------------------------------------------------------

class Nest : public Group
      {
      Q_OBJECT
      QML_ELEMENT
      QML_UNCREATABLE("no no")

      // Bin size in mm.  Defaults to the machine maxTravel (X, Y).
      Q_PROPERTY(QVector2D binSize READ binSize WRITE set_binSize NOTIFY binSizeChanged)

    public:
      QVector2D binSize() const { return _binSize; }
      void set_binSize(QVector2D v);
    Q_SIGNALS:
      void binSizeChanged();

    protected:
      QVector2D _binSize {QVector2D(100.0, 100.0)};

      PROPV(double, spacing, 1.0)
      PROPV(bool, allowRotation, true)
      PROPV(bool, arrange, false)

      inline static constexpr std::string_view _properties {
         R"json({
                  "class": "Nest",
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
                      "label": "Mop",
                      "cells": [
                        {
                          "name": "mop",
                          "type": "laserLayer",
                          "default": ""
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
                          "max": 1000.0,
                          "precision": 3,
                          "step": 0.1,
                          "bigStep": 1.0,
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
                      "label": "Bin",
                      "cells": [
                        {
                          "name": "binSize",
                          "type": "size",
                          "scriptable": true,
                          "unit": "mm",
                          "default": [
                            100.0,
                            100.0
                          ],
                          "tooltip": "Nesting bin dimensions. Defaults to the machine travel range."
                        }
                      ]
                    },
                    {
                      "label": "Nest",
                      "cells": [
                        {
                          "name": "spacing",
                          "type": "float",
                          "scriptable": true,
                          "sublabel": "spacing",
                          "unit": "mm",
                          "default": 1.0,
                          "min": 0.0,
                          "tooltip": "Minimum spacing between packed items."
                        },
                        {
                          "name": "allowRotation",
                          "type": "bool",
                          "default": true,
                          "sublabel": "rotate",
                          "tooltip": "Allow 90-degree rotations during nesting."
                        }
                      ]
                    },
                    {
                      "label": "Arrange",
                      "cells": [
                        {
                          "name": "arrange",
                          "type": "bool",
                          "default": false,
                          "sublabel": "auto",
                          "tooltip": "Auto-arrange items when added to the nest."
                        }
                      ]
                    }
                  ]
                      })json"};

      void updateSelectionGeometry() override;

    public:
      Nest(ZCam* zcam, Element* parent = nullptr);
      virtual QString typeName() override { return QStringLiteral("nest"); }
      virtual const std::string_view properties() const override { return _properties; }
      Q_INVOKABLE virtual bool visible() const override { return true; }
      Q_INVOKABLE bool draggable() const override { return true; }
      Q_INVOKABLE bool deletable() const override { return true; }
      /// Run the nesting algorithm on all child Element3d elements.
      /// Collects each child's world-space polygon, runs libnest2d
      /// to pack them into the bin, and applies the resulting
      /// translation and rotation to each child.  The operation
      /// is undoable as a single macro.
      /// Returns false when there is nothing to pack (no child
      /// geometry, no valid items or nothing fits into the bin) —
      /// callers can show feedback in that case.
      Q_INVOKABLE bool nest();

      /// Initialise the bin size from the current machine's maxTravel.
      void initBinFromMachine();
      // ── Vertex handles (four bin corners, see class doc) ─────────
      // The handles are shown by the 3D viewport while the Nest is
      // the current selection.  Dragging a handle resizes the bin:
      // setVertexPos keeps the opposite corner fixed in world space
      // and adjusts binSize and pos.  The operation is recorded as
      // an undoable NestBinCommand (see ZCam::endVertexDrag).
      Q_INVOKABLE bool hasHandles() const override { return true; }
      Q_INVOKABLE int vertexCount() const override { return 4; }
      Q_INVOKABLE bool isVertex(int idx) const override;
      Q_INVOKABLE QVector3D vertexPos(int idx) const override;
      Q_INVOKABLE QVector3D vertexWorldPos(int idx) const override;
      Q_INVOKABLE void setVertexPos(int idx, const QVector3D& pos) override;
      };
