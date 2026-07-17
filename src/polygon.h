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

enum class PPType : char;

//---------------------------------------------------------
//   Line
//---------------------------------------------------------

class Polygon : public Element3d
      {
      Q_OBJECT
      QML_ELEMENT
      QML_UNCREATABLE("no no")

      inline static constexpr std::string_view _properties {
         R"json({
                  "class": "Line",
                  "items": [
                    {
                      "row": {
                        "show": {
                          "label": "Show",
                          "type": "bool",
                          "default": true
                        },
                        "burn": {
                          "label": "Burn",
                          "type": "bool",
                          "default": true
                        }
                      },
                      "label": "State"
                    },
                    {
                      "name": "color",
                      "label": "Color",
                      "type": "color",
                      "default": "green"
                    },
                    {
                      "name": "pos",
                      "label": "Pos.",
                      "type": "vector3d",
                      "unit": "mm",
                      "default": [
                        0.0,
                        0.0,
                        0.0
                      ]
                    },
                    {
                      "name": "rot",
                      "label": "Rot.",
                      "type": "vector3d",
                      "unit": "°",
                      "min": 0.0,
                      "max": 360,
                      "default": [
                        0.0,
                        0.0,
                        0.0
                      ]
                    },
                    {
                      "name": "scale",
                      "label": "Scale",
                      "type": "vector3d",
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
                    },
                    {
                      "name": "lockScale",
                      "label": "Lock",
                      "type": "lockScale",
                      "default": 2
                    },
                    {
                      "name": "line",
                      "type": "line"
                    },
                    {
                      "row": {
                        "fill": {
                          "label": "fill",
                          "type": "bool",
                          "default": true
                        },
                        "lineWidth": {
                          "label": "width",
                          "type": "float",
                          "default": 0.5
                        }
                      },
                      "label": "Line"
                    },
                    {
                      "row": {
                        "joinType": {
                          "label": "Join",
                          "type": "lineJoin",
                          "default": 0
                        },
                        "endType": {
                          "label": "End",
                          "type": "lineEnd",
                          "default": 0
                        }
                      },
                      "label": " "
                    }
                  ]
                      })json"};

      int currentVertex = -1;
      QRectF _bbox;
      virtual PathList createPath() { return painterPath.toPathList(); }
      bool _drawing {false};
      int _previewVertex {-1};   ///< index of the preview vertex being dragged
      int _selectedSegment {-1}; ///< index of the selected segment (-1 = none)

      void updateSelectionGeometry() override;

    public:
      Polygon(ZCam*, Element* parent = nullptr);
      ~Polygon();
      virtual QString typeName() override { return QStringLiteral("polygon"); }
      void update(int flags = ~0) override;
      virtual json toJson() const override;
      virtual void fromJson(const json&) override;

      virtual int makeSpline(int idx);
      void clear() { painterPath.clear(); }
      void setPainterPath(const PainterPath& pp) { painterPath = pp; }
      void addVertex(const Vec2d& p);
      void drag(int idx, const QVector3D& delta);
      Q_INVOKABLE QVector3D vertexPos(int idx) const override;
      Q_INVOKABLE QVector3D vertexWorldPos(int idx) const override;
      Q_INVOKABLE void setVertexPos(int idx, const QVector3D& pos) override;
      void lineTo(const Vec2d& p) { painterPath.lineTo(p); }
      void moveTo(const Vec2d& p) { painterPath.moveTo(p); }
      bool canClose(const Vec2d& p) const;
      const Vec2d& startPos() const { return painterPath[0].pos; }
      int vertices() const { return painterPath.size(); }
      Q_INVOKABLE int vertexCount() const override { return painterPath.size(); }
      Q_INVOKABLE bool isVertex(int idx) const override;
      Q_INVOKABLE bool isControlPoint(int idx) const override;
      virtual const std::string_view properties() const override { return _properties; }
      Q_INVOKABLE bool nameEditable() const override { return true; }
      Q_INVOKABLE virtual bool visible() const override { return true; }
      Q_INVOKABLE bool draggable() const override { return true; }
      Q_INVOKABLE bool deletable() const override { return true; }
      Q_INVOKABLE bool hasHandles() const override { return true; }
      // Interactive drawing support
      Q_INVOKABLE void startDrawing(const QVector2D& p);
      Q_INVOKABLE void continueDrawing(const QVector2D& p);
      /// Ctrl+Click: fix the preview vertex at p and create a cubic
      /// bezier segment from the last fixed vertex to p with
      /// auto-computed control points.  The control points are
      /// placed at 1/3 and 2/3 along the line and can be dragged
      /// afterwards via vertex handles.
      //      Q_INVOKABLE void continueDrawingBezier(const QVector2D& p);
      Q_INVOKABLE void finishDrawing();
      Q_INVOKABLE bool isDrawing() const { return _drawing; }
      Q_INVOKABLE void updatePreview(const QVector2D& p);

      //--- segment selection ---
      /// Returns the number of selectable segments in the polygon.
      /// A segment is a logical edge between two consecutive vertices,
      /// skipping bezier control-point entries (CurveToData1/Data2).
      Q_INVOKABLE int segmentCount() const;
      /// Returns the currently selected segment index, or -1 if none.
      Q_INVOKABLE int selectedSegment() const { return _selectedSegment; }
      /// Selects the segment at the given index (-1 clears the selection).
      Q_INVOKABLE void setSelectedSegment(int idx);
      /// Clears any segment selection.
      Q_INVOKABLE void clearSegmentSelection() { setSelectedSegment(-1); }
      /// Returns the world-space midpoint of the given segment.
      Q_INVOKABLE QVector3D segmentMidpoint(int segIdx) const;
      /// Finds the segment whose world-space midpoint is closest to the
      /// given world position.  Returns the segment index or -1 if the
      /// polygon has fewer than 2 vertices.
      Q_INVOKABLE int findNearestSegment(const QVector3D& worldPos) const;
      /// Returns true if the given vertex index belongs to the currently
      /// selected segment (i.e. it is one of the two endpoint vertices).
      Q_INVOKABLE bool isVertexInSelectedSegment(int idx) const;
      };
