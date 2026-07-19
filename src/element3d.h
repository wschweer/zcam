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

#include <QObject>
#include <QtQml/qqmlregistration.h>
#include <QVector3D>
#include <QColor>
#include <QMatrix4x4>
#include <QRectF>
#include "macros.h"
#include "element.h"
#include "tessgeometry.h"
#include "painterpath.h"

static constexpr double FONT_SCALE    = 0.352778 * .1;
static constexpr double FONT_SCALE_UP = 10.0;

//---------------------------------------------------------
//   LockScaleMode
//    Controls how scale changes are propagated across axes:
//      Off    – each axis scales independently
//      Lock   – preserve the current aspect ratio; changing one
//               axis updates the other two proportionally
//      Square – force xScale == yScale == zScale at all times
//---------------------------------------------------------
enum class LockScaleMode : int { Off = 0, Lock = 1, Square = 2 };
Q_DECLARE_METATYPE(LockScaleMode)

//---------------------------------------------------------
//   Element3d
//---------------------------------------------------------

class Element3d : public Element
      {
      Q_OBJECT
      QML_ELEMENT
      QML_UNCREATABLE("no no")

      PROPV(bool, show, true)
      PROPV(bool, burn, true)
      PROPV(TessGeometry*, geometry, nullptr)
      PROPV(QString, model, QString("Shape.qml"))
      PROPV(QVector3D, pos, QVector3D(0.0, 0.0, 0.0))
      PROPV(QVector3D, rot, QVector3D(0.0, 0.0, 0.0))
      // Custom scale property: WRITE routes through set_scaleAR() which
      // corrects the value according to the lockScale aspect-ratio mode.
      Q_PROPERTY(QVector3D scale READ scale WRITE set_scaleAR NOTIFY scaleChanged)

    public:
      QVector3D scale() const { return _scale; }
      void set_scaleAR(QVector3D v);
      void set_scale(QVector3D v) {
            if (v != _scale) {
                  _scale = v;
                  emit scaleChanged();
                  }
            }
    Q_SIGNALS:
      void scaleChanged();

    protected:
      QVector3D _scale {QVector3D(1.0, 1.0, 1.0)};

      PROPV(bool, selectable, true)
      PROPV(int, lockScale, static_cast<int>(LockScaleMode::Square))
      PROPV(bool, mirrorX, false)
      PROPV(bool, mirrorY, false)
      PROPV(bool, constantSize, false)
      PROPV(bool, fill, false)
      PROPV(int, pickLevel, 0)
      PROPV(double, lineWidth, 0.0)
      PROPV(int, endType, 0)
      PROPV(int, joinType, 0)
      PROP(QList<Element3d*>, subElements)

      Q_PROPERTY(TessGeometry* selectionGeometry READ selectionGeometry NOTIFY selectionGeometryChanged)
      Q_PROPERTY(bool ancestorsShow READ ancestorsShow NOTIFY ancestorsShowChanged)
      Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged)
      Q_PROPERTY(QColor curColor READ curColor NOTIFY curColorChanged)
      Q_PROPERTY(int vertexRevision READ vertexRevision NOTIFY vertexRevisionChanged)

      TessGeometry* _selectionGeometry {nullptr};
      QColor _color;
      PathList _pathList;
      mutable QMatrix4x4 _matrix;
      int _vertexRevision {0};
      bool _batching {false}; ///< suppresses vertexRevisionChanged during batch updates

    protected:
      // Static, class-bound flag: only elements whose class sets this to true
      // can be interactively dragged on the 3D canvas.  Subclasses override
      // s_draggable and draggable() to opt in.
      static constexpr bool s_draggable = false;
      // Static, class-bound flag: only elements whose class sets this to true
      // can be deleted from the project tree. Subclasses override
      // s_deletable and deletable() to opt in.
      static constexpr bool s_deletable = false;
      PainterPath painterPath; // editable source path
      virtual void updateSelectionGeometry();
      mutable bool _matrixDirty {true};
      // return true if lineWidth() is zero
      bool thinLine() const { return qFuzzyCompare(lineWidth(), 0.0); }
    signals:
      void selectionGeometryChanged();
      void ancestorsShowChanged();
      void colorChanged();
      void curColorChanged();
      void vertexRevisionChanged();

    public slots:
      Q_INVOKABLE virtual void update(int flags = -1) {}

    public:
      Element3d(ZCam*, Element* parent = nullptr);
      virtual json toJson() const override;
      virtual void fromJson(const json& json) override;
      virtual const std::string_view properties() const { return ""; }
      // if visible, the show flag can be toggled by user
      Q_INVOKABLE virtual bool visible() const { return false; }
      // Static, class-bound flag: only elements whose class sets this to true
      // can be interactively dragged on the 3D canvas.
      Q_INVOKABLE virtual bool draggable() const { return s_draggable; }
      // Static, class-bound flag: only elements whose class sets this to true
      // can be deleted from the project tree.
      Q_INVOKABLE virtual bool deletable() const { return s_deletable; }
      // Returns true if this element exposes editable vertex handles
      // in the 3D viewport.  Subclasses with handles (e.g. Polygon,
      // Rectangle) override this to return true.
      Q_INVOKABLE virtual bool hasHandles() const { return false; }
      // Returns the number of handle positions.  Subclasses with
      // handles override this.
      Q_INVOKABLE virtual int vertexCount() const { return 0; }
      // Returns true if the handle at idx is directly draggable.
      Q_INVOKABLE virtual bool isVertex(int idx) const {
            (void)idx;
            return false;
            }
      // Returns true if the handle at idx is a bezier control point
      // (as opposed to a regular vertex).  Used by QML to render
      // control-point handles in a different colour.
      Q_INVOKABLE virtual bool isControlPoint(int idx) const {
            (void)idx;
            return false;
            }
      // Returns the local position of the idx-th handle as a QVector3D.
      Q_INVOKABLE virtual QVector3D vertexPos(int idx) const {
            (void)idx;
            return {};
            }
      // Returns the world (root) position of the idx-th handle.
      Q_INVOKABLE virtual QVector3D vertexWorldPos(int idx) const {
            (void)idx;
            return {};
            }
      // Sets the local position of the idx-th handle.
      Q_INVOKABLE virtual void setVertexPos(int idx, const QVector3D& pos) {
            (void)idx;
            (void)pos;
            }
      // Returns the vertex revision counter (incremented on every change).
      int vertexRevision() const { return _vertexRevision; }
      // Begin/end a batch update: suppresses vertexRevisionChanged from
      // pos/rot/scale signal handlers until endBatchUpdate() is called.
      // Use when multiple property changes should result in a single update.
      void beginBatchUpdate() { _batching = true; }
      void endBatchUpdate() {
            _batching = false;
            ++_vertexRevision;
            emit vertexRevisionChanged();
            }
      // Returns true when every ancestor Element3d has show == true.
      // Used to grey-out child visibility icons when a parent is hidden.
      bool ancestorsShow() const;

      QRectF boundingBox() const;
      /// Returns the axis-aligned bounding box in world (root) coordinates
      /// by transforming the local boundingBox() through globalMatrix().
      QRectF worldBoundingBox() const;
      /// Returns true if the given world-space point (x, y) lies inside
      /// this element's world bounding box.
      bool containsWorldPoint(double x, double y) const;
      TessGeometry* selectionGeometry() const { return _selectionGeometry; }
      QColor color() const { return _color; }
      void setColor(const QColor&);
      Q_INVOKABLE QColor curColor() const;
      const PathList& pathList() const { return _pathList; }
      PathList& pathList() { return _pathList; }
      const QMatrix4x4& matrix() const;
      /// Returns the full transformation matrix from this element's local
      /// coordinate system to the root (project) coordinate system by
      /// multiplying all parent matrices left-to-right:
      ///   global = rootMatrix * ... * parentMatrix * localMatrix
      QMatrix4x4 globalMatrix() const;
      void strokeAndFill();
      };

extern void closePath(PathList& _pathList);

//---------------------------------------------------------
//   RootElement
//---------------------------------------------------------

class RootElement : public Element3d
      {
      Q_OBJECT
      QML_ELEMENT
      QML_UNCREATABLE("no no")
      virtual QString typeName() { return QStringLiteral("Root"); }

    public:
      RootElement(ZCam* zcam, Element* parent = nullptr) : Element3d(zcam, parent) { setName(""); }
      };

extern void closePath(PathList& _pathList);
