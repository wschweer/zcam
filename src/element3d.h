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

      PROPV(TessGeometry*, geometry, nullptr)
      PROPV(QString, model, QString("Shape.qml"))
      PROPV(QVector3D, pos, QVector3D(0.0, 0.0, 0.0))
      PROPV(QVector3D, rot, QVector3D(0.0, 0.0, 0.0))
      PROPV(QVector3D, scale, QVector3D(1.0, 1.0, 1.0))
      PROPV(bool, active, true)
      PROPV(bool, selectable, true)
      PROPV(bool, scaleLocked, false)
      PROPV(bool, mirrorX, false)
      PROPV(bool, mirrorY, false)
      PROPV(bool, constantSize, false)
      PROPV(QColor, curColor, QColor("red"))
      PROP(QList<Element3d*>, subElements)

    protected:
      PainterPath painterPath; // editable source path
      PathList _pathList;      // cached processed painterPath

    public slots:
      Q_INVOKABLE virtual void update(int flags = -1) {}

    public:
      Element3d(ZCam*, Element* parent = nullptr);
      };

//---------------------------------------------------------
//   RootElement
//---------------------------------------------------------

class RootElement : public Element3d {
      Q_OBJECT
      QML_ELEMENT
      QML_UNCREATABLE("no no")
      virtual const char* typeName() { return "Root"; }

   public:
      RootElement(ZCam* zcam, Element* parent = nullptr) : Element3d(zcam, parent) {
            setName("");
            }
      };
