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

#include "brepocct.h"   // must come before Qt / project headers

#include <QRectF>
#include <QString>
#include <QVector3D>
#include <QtQml/qqmlregistration.h>
#include <QtQuick3D/qquick3dgeometry.h>

#include <nlohmann/json.hpp>
using json = nlohmann::json;

#include "element3d.h"
#include "macros.h"
#include "brepgeometry.h"
#include "brepedgegeometry.h"

class TopoDS_Shape;

//---------------------------------------------------------
//   BrepElement
//    Element3d that renders a native OpenCASCADE ".brep"
//    file in the 3D viewport.  The element stores the file
//    path, a tessellated triangle mesh (uploaded to
//    QQuick3DGeometry) and a polyline outline derived from
//    the BRep edges for the scene graph / selection box.
//---------------------------------------------------------

class BrepElement : public Element3d
      {
      Q_OBJECT
      QML_ELEMENT
      QML_UNCREATABLE("no no")

      // QML-accessible geometry for the 3D mesh (the BREP file itself)
      Q_PROPERTY(BrepGeometry* brepGeometry READ brepGeometry NOTIFY brepGeometryChanged)
      Q_PROPERTY(BrepEdgeGeometry* edgeGeometry READ edgeGeometry NOTIFY edgeGeometryChanged)

    signals:
      void brepGeometryChanged();
      void edgeGeometryChanged();

    protected:
      inline static constexpr std::string_view _properties {
         R"json({
    "class": "BrepElement",
    "rows": [
        {
            "label": "File",
            "cells": [
                {
                    "name": "brepFilePath",
                    "type": "string",
                    "sublabel": "Path"
                }
            ]
        },
        {
            "label": "Visibility",
            "cells": [
                {
                    "type": "bool",
                    "default": true,
                    "name": "show"
                },
                {
                    "type": "bool",
                    "default": true,
                    "name": "burn"
                }
            ]
        },
        {
            "label": "Pos.",
            "cells": [
                {
                    "name": "pos",
                    "type": "vector3d",
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
                    "min": 0.001,
                    "max": 1000,
                    "default": [
                        1.0,
                        1.0,
                        1.0
                    ]
                }
            ]
        }
    ]
})json"};

      BrepGeometry* _brepGeometry { nullptr };
      BrepEdgeGeometry* _edgeGeometry { nullptr };
      QString _sourcePath;         ///< file path of imported .brep file
      bool _hasShape { false };    ///< true if geometry is loaded
      bool _hasPolyline { false }; ///< true when a 2D outline was built
      PathList _polylinePathList;  ///< 2D outline from the brep edges
      QRectF _worldBBox;           ///< cached world-space bounding box
      QVector3D _meshMin;          ///< cached local mesh bounds (all axes, incl. z)
      QVector3D _meshMax;

      // Tessellation parameters (OCCT defaults are quite coarse)
      double _deflection {0.1};  ///< linear deflection (mm)
      double _angle {0.5};       ///< angular deflection (rad)

    public:
      BrepElement(ZCam*, Element* parent = nullptr);
      ~BrepElement() override;

      virtual QString typeName() override;
      virtual const std::string_view properties() const override;

      void set_brepFilePath(const QString& path);
      QString brepFilePath() const { return _sourcePath; }
      BrepGeometry* brepGeometry() const { return _brepGeometry; }
      BrepEdgeGeometry* edgeGeometry() const { return _edgeGeometry; }

      bool loadFile(const QString& path);
      bool insertIntoProject(ZCam* zcam, Element* parent, int row);

      // The element renders an actual shape on the 3D canvas,
      // so it can be picked and dragged there (the base class
      // defaults both to false, which would hide it from the
      // bounding-box picker).
      Q_INVOKABLE virtual bool visible() const override { return true; }
      Q_INVOKABLE bool draggable() const override { return true; }

      // Returns the cached local bounding box (updated by loadFile()
      // from the tessellated mesh bounds).  Overrides the base
      // implementation, which would fall back to childrenBoundingBox()
      // because a BREP element has no pathList().
      virtual QRectF contentBoundingBox() const override { return _worldBBox; }
      /// Local 3D bounding box including the real z extent of the
      /// tessellated mesh — the flat Element3d default (z = 0) would
      /// float the pick box far away from the visible object whenever
      /// the brep file's z-offset differs from zero.
      virtual void boundingBox3D(QVector3D& bMin, QVector3D& bMax) const override {
            bMin = _meshMin;
            bMax = _meshMax;
            }
      const PathList& polylinePathList() const { return _polylinePathList; }
      bool hasPolyline() const { return _hasPolyline; }
      bool hasShape() const { return _hasShape; }
      /// Raw cached local bounding box for diagnosis.
      QRectF worldBBoxRaw() const { return _worldBBox; }
      /// Builds the 3D wireframe selection brick (12 edges) instead
      /// of the flat 2D bounding-box rectangle used by planar
      /// elements.
      virtual void updateSelectionGeometry() override;
      QString sourcePath() const { return _sourcePath; }

      // Rebuild the GPU geometry and the cached bounding boxes from
      // the source file.  The canvas calls update() whenever the
      // scene is (re)populated — including after a project load, so
      // a BREP element whose file path was only restored from JSON
      // refreshes its mesh and becomes visible and pickable again.
      Q_INVOKABLE virtual void update(int flags = -1) override {
            Q_UNUSED(flags);
            if (!_sourcePath.isEmpty() && (!_hasShape || !_brepGeometry->loaded()))
                  loadFile(_sourcePath);
            // Always (re-)fill the selection line geometry from the
            // current bounding box — the QML bboxOverlay binding
            // references element.selectionGeometry directly and only
            // re-renders when the content changes.
            updateSelectionGeometry();
            }

      virtual json toJson() const override;
      virtual void fromJson(const json& data) override;
      virtual void fixup() override;

      // Static helpers used by the import / boundingBox entry points.
      static bool loadShapeFromFile(const QString& path, TopoDS_Shape& shape);
      static bool buildPolylineFromShape(const TopoDS_Shape& shape, double deflection,
                                         PathList& pathList, QRectF& worldBBox);
      static bool computeMeshBounds(const TopoDS_Shape& shape, double deflection, double angle,
                                    QVector3D& bMin, QVector3D& bMax);
      };
