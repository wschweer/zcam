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

#include "element3d.h"

//---------------------------------------------------------
//   FramingType
//    Controls how the framing contour is generated:
//      BoundingBox – axis-aligned rectangle around all geometry
//      ConvexHull   – convex hull polygon around all geometry
//      Rectangle    – a user-defined, user-editable rectangle
//                     (centered at the element origin; positioned and
//                     resized like a normal Rectangle element)
//---------------------------------------------------------

enum class FramingType : int { BoundingBox = 0, ConvexHull = 1, Rectangle = 2 };

//---------------------------------------------------------
//   Framing
//    Creates the framing contour for the fiber laser.  In
//    BoundingBox / ConvexHull mode the contour is derived
//    automatically from the CAM geometry.  In Rectangle mode
//    the user draws/edits a plain rectangle (position, rotation,
//    scale and size) directly on the 3-D canvas; the laser frames
//    exactly that user-defined outline.
//---------------------------------------------------------

class Framing : public Element3d
      {
      Q_OBJECT
      QML_ELEMENT
      QML_UNCREATABLE("")

      // Custom size property (like Rectangle): exposed through the "size"
      // inspector cell so the value can be edited and (de)serialised.
      Q_PROPERTY(QVector2D size READ size WRITE set_size NOTIFY sizeChanged)

    public:
      QVector2D size() const { return _size; }
      void set_size(QVector2D v);

    Q_SIGNALS:
      void sizeChanged();

    protected:
      QVector2D _size {QVector2D(300.0, 200.0)};
      PROPV(int, framingType, static_cast<int>(FramingType::ConvexHull))

      inline static constexpr std::string_view _properties {
         R"json({
                  "class": "Framing",
                  "rows": [
                    {
                      "label": " ",
                      "cells": [
                        {
                          "type": "bool",
                          "default": true,
                          "name": "show",
                          "sublabel": "Show"
                        },
                        {
                          "type": "empty",
                          "default": true,
                          "name": "burn",
                          "sublabel": "Burn"
                        }
                      ]
                    },
                    {
                      "label": "Type",
                      "cells": [
                        {
                          "name": "framingType",
                          "type": "framingType",
                          "default": 1
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
                          "unit": "\u00b0",
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
                      "label": "Size",
                      "cells": [
                        {
                          "name": "size",
                          "type": "size",
                          "scriptable": true,
                          "unit": "mm",
                          "default": [
                            300.0,
                            200.0
                          ]
                        }
                      ]
                    },
                    {
                      "label": "Line",
                      "cells": [
                        {
                          "name": "lineWidth",
                          "type": "float",
                          "scriptable": true,
                          "sublabel": "width",
                          "default": 0.0
                        }
                      ]
                    }
                  ]
                      })json"};

    public:
      //--------------------------------------------------------------------
      //     Framing
      //--------------------------------------------------------------------
      Framing(ZCam* w, Element* parent = nullptr);
      virtual QString typeName() override { return QStringLiteral("framing"); }
      virtual const std::string_view properties() const override { return _properties; }
      //--------------------------------------------------------------------
      //     update
      //    Rebuild the framing contour according to the selected type and
      //    recompute the world-space laser contour.
      //--------------------------------------------------------------------
      virtual void update(int flags = ~0) override;
      //--------------------------------------------------------------------
      //     worldContour
      //    The framing contour in work-field (world) space — the geometry
      //    the laser actually moves.  For the auto types the element
      //    transform is the identity, so this equals the local path; for
      //    the Rectangle type it applies the element's pos/rot/scale and,
      //    if configured, the camera projection.  Computed in update().
      //--------------------------------------------------------------------
      const Clipper2Lib::PathD& worldContour() const { return _worldContour; }
      //--------------------------------------------------------------------
      //     createPath
      //    Build the centered rectangle outline in local coordinates.
      //--------------------------------------------------------------------
      void createPath();
      bool isClosed() const;
      // On-canvas visibility.
      Q_INVOKABLE virtual bool visible() const override { return true; }
      // The Framing outline is a child of the hidden-by-default Cam, but it
      // must always render and be selectable on the 3-D canvas (independent
      // of the laser-layer preview), so it opts out of the ancestor
      // visibility rule.  The Fixture/laser-layer subtree is unaffected.
      virtual bool ancestorsShow() const override { return true; }
      //--------------------------------------------------------------------
      //     isRectMode
      //    True when the framing is the user-defined, user-editable
      //    rectangle.  Only in this mode is the outline interactive on the
      //    canvas (draggable + corner handles); the BoundingBox / ConvexHull
      //    modes are a non-interactive reference overlay.
      //--------------------------------------------------------------------
      bool isRectMode() const { return framingType() == static_cast<int>(FramingType::Rectangle); }
      // Editable like a Rectangle (Rectangle mode only): the body can be
      // dragged to reposition it.
      Q_INVOKABLE bool draggable() const override { return isRectMode(); }
      // Editable like a Rectangle (Rectangle mode only): four corner handles,
      // all draggable.
      Q_INVOKABLE bool hasHandles() const override { return isRectMode(); }
      Q_INVOKABLE int vertexCount() const override { return isRectMode() ? 4 : 0; }
      Q_INVOKABLE bool isVertex(int idx) const override;
      Q_INVOKABLE QVector3D vertexPos(int idx) const override;
      Q_INVOKABLE QVector3D vertexWorldPos(int idx) const override;
      Q_INVOKABLE void setVertexPos(int idx, const QVector3D& pos) override;

    private:
      bool _suppressUpdate {false};     ///< guard to batch updates during setVertexPos
      Clipper2Lib::PathD _worldContour; ///< laser contour in work-field space
      };
