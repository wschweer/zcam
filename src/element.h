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
#include <nlohmann/json.hpp>
using json = nlohmann::json;

#include "logger.h"
#include "macros.h"

class ZCam;

//---------------------------------------------------------
//   Element
//    base class for Element3d, CamElement
//    for use in ProjectTreeView
//---------------------------------------------------------

class Element : public QObject
      {
      Q_OBJECT
      QML_ELEMENT
      QML_UNCREATABLE("no no")

      Q_PROPERTY(QList<Element*> children READ children)
      Q_PROPERTY(QString name READ name NOTIFY nameChanged )

      QList<Element*> _children;
      Element* _parent;
      QString _name;

      // this is a global list of all elements, accessible by name
      static QHash<QString,Element*> names;

    protected:
      ZCam* zcam;

    signals:
      void nameChanged();

    public:
      Element(ZCam* zcam, Element* parent = nullptr);
      virtual ~Element();
      virtual const char* typeName() = 0;

      const QList<Element*>& children() const { return _children; }
      Element* parent() const { return _parent; }
      virtual json toJson() const;
      virtual void fromJson(const json&);
      void addChild(Element* e) {
            _children.push_back(e);
            e->_parent = this;
            }
      void setName(QString v);
      QString name() const { return _name; }
      };
