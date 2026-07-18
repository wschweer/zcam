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
#include <QHash>
#include <nlohmann/json.hpp>
using json = nlohmann::json;

#include "logger.h"
// #include "macros.h"

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
      Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged)
      Q_PROPERTY(bool expanded READ expanded WRITE setExpanded NOTIFY expandedChanged)

      QList<Element*> _children;
      Element* _parent {nullptr};
      QString _name;
      bool _expanded = false; ///< persistent expand state in TreeView

      // this is a global list of all elements, accessible by name
      static QHash<QString, Element*> names;

    protected:
      ZCam* zcam;

    signals:
      void nameChanged();
      void expandedChanged();

    public:
      bool _saveChildren { true };
      Element(ZCam* zcam, Element* parent = nullptr);
      virtual ~Element();
      Q_INVOKABLE virtual QString typeName() = 0;
      const QList<Element*>& children() const { return _children; }
      QList<Element*>& children() { return _children; }
      Q_INVOKABLE Element* parent() const { return _parent; }
      virtual json toJson() const;
      virtual void fromJson(const json&);
      void addChild(Element* e) {
            _children.push_back(e);
            e->_parent = this;
            e->setParent(this);
            }
      void removeChild(Element* e) {
            _children.removeAll(e);
            if (e->_parent == this)
                  e->_parent = nullptr;
            }
      static void clearProject();
      static Element* byName(const QString& name) { return names.value(name); }
      void setName(QString v);
      QString name() const { return _name; }
      Q_INVOKABLE virtual bool nameEditable() const { return false; }
      bool expanded() const { return _expanded; }
      void setExpanded(bool v) {
            if (v != _expanded) {
                  _expanded = v;
                  emit expandedChanged();
                  }
            }
      ZCam* zcamInstance() const { return zcam; }
      virtual bool saveChildren() const { return _saveChildren; }
      virtual void fixup() {}
      };

//---------------------------------------------------------
//   isType
//---------------------------------------------------------

template <typename T> inline bool isType(QObject* o) {
      return qobject_cast<T*>(o) != nullptr;
      }

template <typename T> inline bool isType(const QObject* o) {
      return qobject_cast<const T*>(o) != nullptr;
      }

//---------------------------------------------------------
//   toType
//---------------------------------------------------------

#ifndef NDEBUG
template <typename T> static inline T* toType(QObject* e) {
      if (isType<T>(e))
            return static_cast<T*>(e);
      Fatal("bad type");
      }

#else
template <typename T> static inline T* toType(QObject* e) {
      return static_cast<T*>(e);
      }

#endif
