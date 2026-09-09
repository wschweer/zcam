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

#include <QList>

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
                      "label": "Line",
                      "cells": [
                        {
                          "type": "bool",
                          "default": true,
                          "name": "fill",
                          "sublabel": "fill"
                        },
                        {
                          "type": "float",
                          "scriptable": true,
                          "default": 0.5,
                          "name": "lineWidth",
                          "sublabel": "width"
                        }
                      ]
                    },
                    {
                      "label": " ",
                      "cells": [
                        {
                          "type": "lineJoin",
                          "default": 0,
                          "name": "joinType",
                          "sublabel": "Join"
                        },
                        {
                          "type": "lineEnd",
                          "default": 0,
                          "name": "endType",
                          "sublabel": "End"
                        }
                      ]
                    }
                  ]
                      })json"};

      int currentVertex = -1;
      QRectF _bbox;
      virtual PathList createPath() { return painterPath.toPathList(); }
      bool _drawing {false};
      int _previewVertex {-1};   ///< index of the preview vertex being dragged
      int _selectedSegment {-1}; ///< index of the focused segment (-1 = none)
      int _hoveredSegment {-1};  ///< index of the hovered segment (-1 = none)
      /// Additional segments selected by the lasso.  Kept as a set so the
      /// lasso can select multiple segments of one polygon at once while
      /// _selectedSegment continues to track the "focused" (single-click)
      /// segment used by keyboard-driven editing ('b', 'p').
      QList<int> _lassoSelectedSegments;

      void updateSelectionGeometry() override;
      void buildSegmentLine(Clipper2Lib::PathsD& lines, int segIdx) const;
      /// Rebuilds the dashed association lines connecting each bezier
      /// segment's anchor points to its control points so the user can
      /// see which control points belong to which segment.
      void updateControlHandles();
      /// Returns the union of the focused segment (_selectedSegment) and
      /// the lasso-selected segments (_lassoSelectedSegments) as a sorted,
      /// deduplicated list of segment indices.
      QList<int> selectedSegmentIndices() const;

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
      void cubicTo(const Vec2d& p1, const Vec2d& p2, const Vec2d& p3) { painterPath.cubicTo(p1, p2, p3); }
      void appendPainterPath(const PainterPath& other, bool skipFirstMoveTo = false) {
            size_t start = skipFirstMoveTo && !other.empty() ? 1 : 0;
            for (size_t i = start; i < other.size(); ++i)
                  painterPath.push_back(other[i]);
            }
      void moveTo(const Vec2d& p) { painterPath.moveTo(p); }
      /// Read-only access to the editable source path (used by the
      /// SVG export to serialize lines and cubic beziers exactly).
      const PainterPath& painterPathData() const { return painterPath; }
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
      /// Fix the preview vertex at p and insert a cubic bezier segment
      /// from the last fixed vertex to p (instead of a straight line, see
      /// continueDrawing()).  The control points are auto-placed at 1/3
      /// and 2/3 along the chord and can be dragged afterwards via the
      /// control-point handles.  Used for Ctrl+click and 'b' mode in the
      /// polygon tool.
      Q_INVOKABLE void continueDrawingBezier(const QVector2D& p);
      Q_INVOKABLE void finishDrawing();
      Q_INVOKABLE bool isDrawing() const { return _drawing; }
      Q_INVOKABLE void updatePreview(const QVector2D& p);

      //--- segment selection ---
      /// Returns the number of selectable segments in the polygon.
      /// A segment is a logical edge between two consecutive vertices,
      /// skipping bezier control-point entries (CurveToData1/Data2).
      Q_INVOKABLE int segmentCount() const;
      /// Returns the currently selected (focused) segment index, or -1 if none.
      /// When a lasso selected multiple segments, this returns the first one.
      Q_INVOKABLE int selectedSegment() const { return _selectedSegment; }
      /// Returns true if any segment is selected (focused or lasso).
      Q_INVOKABLE bool hasSelectedSegment() const {
            return _selectedSegment >= 0 || !_lassoSelectedSegments.isEmpty();
            }
      /// Selects the segment at the given index (-1 clears the selection).
      /// Also clears any lasso-selected segments (single-click focus).
      Q_INVOKABLE void setSelectedSegment(int idx);
      /// Clears any segment selection (focused and lasso).
      Q_INVOKABLE void clearSegmentSelection();
      /// Selects the given list of segments via lasso selection.
      /// Pass an empty list to clear all lasso-selected segments (and the
      /// focused segment).  If one or more segments are selected, the
      /// first becomes the focused segment (selectedSegment).  Existing
      /// lasso selection and focus are fully replaced.
      Q_INVOKABLE void setLassoSelectedSegments(const QList<int>& idxs);
      /// Returns the list of segment indices selected by the lasso.
      Q_INVOKABLE QList<int> lassoSelectedSegments() const { return _lassoSelectedSegments; }
      /// Returns the world-space midpoint of the given segment.
      Q_INVOKABLE QVector3D segmentMidpoint(int segIdx) const;
      /// Finds the segment whose world-space midpoint is closest to the
      /// given world position.  Returns the segment index or -1 if the
      /// polygon has fewer than 2 vertices.
      Q_INVOKABLE int findNearestSegment(const QVector3D& worldPos) const;
      /// Returns the currently hovered segment index, or -1 if none.
      Q_INVOKABLE int hoveredSegment() const { return _hoveredSegment; }
      /// Sets the hovered segment index (-1 clears).  Updates the
      /// selection geometry overlay so the hovered segment is shown
      /// as a coloured highlight while the mouse hovers over it.
      Q_INVOKABLE void setHoveredSegment(int idx);
      /// Returns true if the given vertex index belongs to the currently
      /// selected segment (i.e. it is one of the two endpoint vertices).
      Q_INVOKABLE bool isVertexInSelectedSegment(int idx) const;

      //--- segment editing ---
      /// Convert the selected line segment to a cubic bezier segment.
      /// The control points are auto-placed at 1/3 and 2/3 along the
      /// chord so the curve initially looks identical to the straight
      /// line but can be shaped by dragging the control-point handles.
      /// No-op if no segment is selected or the selected segment is
      /// already a curve.  The change is undoable.
      Q_INVOKABLE void convertSelectedSegmentToBezier();
      /// Split the selected line segment at its midpoint by inserting
      /// a new vertex (LineTo) at the geometric midpoint.  No-op if no
      /// segment is selected or the selected segment is a curve.
      /// The change is undoable.
      Q_INVOKABLE void splitSelectedSegment();

      //--- geometry optimization ---
      /// Simplify the polygon geometry, in two passes:
      ///   1) A cubic-bezier segment whose control points are collinear
      ///      with its chord (i.e. it actually draws a straight line)
      ///      is replaced by a single straight line segment.
      ///   2) Consecutive straight line segments whose anchor points
      ///      (A → B → C) are collinear are merged into one segment
      ///      (A → C), removing the redundant intermediate vertex B.
      /// No-op (the undo stack is left untouched and the project is not
      /// marked dirty) if nothing can be simplified.  When a change is
      /// made it is recorded as a single undoable command.
      Q_INVOKABLE void optimize();
      };
