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

#include <QRectF>
#include "macros.h"
#include "element.h"
#include "tessgeometry.h"
#include "painterpath.h"

static constexpr double FONT_SCALE    = 0.352778 * .1;
static constexpr double FONT_SCALE_UP = 10.0;

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
      PROPV(QVector3D, scale, QVector3D(1.0, 1.0, 1.0))
      PROPV(bool, selectable, true)
      PROPV(bool, scaleLocked, false)
      PROPV(bool, mirrorX, false)
      PROPV(bool, mirrorY, false)
      PROPV(bool, constantSize, false)
      PROPV(int, pickLevel, 0)
      PROP(QList<Element3d*>, subElements)

      Q_PROPERTY(TessGeometry* selectionGeometry READ selectionGeometry NOTIFY selectionGeometryChanged)
      Q_PROPERTY(bool ancestorsShow READ ancestorsShow NOTIFY ancestorsShowChanged)
      Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged)
      Q_PROPERTY(QColor curColor READ curColor NOTIFY curColorChanged)

      TessGeometry* _selectionGeometry {nullptr};
      QColor _color;

    signals:
      void selectionGeometryChanged();
      void ancestorsShowChanged();
      void colorChanged();
      void curColorChanged();

    protected:
      PainterPath painterPath; // editable source path
      PathList _pathList;      // cached processed painterPath
      void updateSelectionGeometry();

    public slots:
      Q_INVOKABLE virtual void update(int flags = -1) {}

    public:
      Element3d(ZCam*, Element* parent = nullptr);
      virtual json toJson() const override;
      virtual void fromJson(const json& json) override;
      virtual const std::string_view properties() const { return ""; }
      // if visible, the show flag can be toggled by user
      Q_INVOKABLE virtual bool visible() const { return false; }
      // Returns true when every ancestor Element3d has show == true.
      // Used to grey-out child visibility icons when a parent is hidden.
      bool ancestorsShow() const;

      QRectF boundingBox() const;
      TessGeometry* selectionGeometry() const { return _selectionGeometry; }
      QColor color() const { return _color; }
      void setColor(const QColor&);
      Q_INVOKABLE QColor curColor() const;
      };

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
