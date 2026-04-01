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
#include <QQmlEngine>
#include <QJSEngine>
#include <QtQml/qqmlregistration.h>

#include "toplevel.h"

class TopLevel;
class Element3d;

//---------------------------------------------------------
//   Config
//---------------------------------------------------------

class Style : public QObject
      {
      Q_OBJECT
      QML_ELEMENT
      QML_UNCREATABLE("no")

      Q_PROPERTY(int iconSize READ iconSize WRITE setIconSize NOTIFY iconSizeChanged)

      int _iconSize{32};

    signals:
      void iconSizeChanged();

    public:
      explicit Style(QObject* parent = nullptr) : QObject(parent) {}
      int iconSize() const { return _iconSize; }
      void setIconSize(int v) {
            if (v != _iconSize) {
                  _iconSize = v;
                  emit iconSizeChanged();
                  }
            }
      };

//---------------------------------------------------------
//   ZCam
//---------------------------------------------------------

class ZCam : public QObject
      {
      Q_OBJECT
      QML_ELEMENT
      QML_SINGLETON
      Q_PROPERTY(Style* style READ style NOTIFY styleChanged)
      Q_PROPERTY(TopLevel* topLevel READ topLevel WRITE setTopLevel NOTIFY topLevelChanged)
      Q_PROPERTY(Element3d* currentElement READ currentElement WRITE setCurrentElement NOTIFY currentElementChanged)

      Style* _style{nullptr};
      TopLevel* _topLevel{nullptr};
      Element3d* _currentElement{nullptr};

    signals:
      void styleChanged();
      void topLevelChanged();
      void currentElementChanged();

      void remove3dElement(Element3d*);           // signal 3d gui to remove an element from the scene graph
      void add3dElement(Element3d*);              // signal 3d gui to add a new element into the scene graph
      void addSubElement(Element3d*, Element3d*); // signal 3d gui to add a new subelement into the scene graph
      void rootElementChanged(Element3d*);        // signal 3d gui to rebuild scene graph
      void startDragElement(Element3d*);          // signal 3d gui to drag this element

    public:
      explicit ZCam(QObject* parent = nullptr);
      Style* style() const { return _style; }
      TopLevel* topLevel() const { return _topLevel; }
      void setTopLevel(TopLevel* v) {
            if (v != _topLevel) {
                  _topLevel = v;
                  emit topLevelChanged();
                  }
            }
      Element3d* currentElement() const { return _currentElement; }
      void setCurrentElement(Element3d* v) {
            if (v != _currentElement) {
                  _currentElement = v;
                  emit currentElementChanged();
                  }
            }
      static ZCam* create(QQmlEngine*, QJSEngine*);
      void undoChangeProperty(Element*, const char*, QVariant) {}
      };
