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
#include "projectmanager.h"
#include "recipe.h"
#include "machine.h"

class TopLevel;
class Element3d;
class TreeModel;
class Laser;

//---------------------------------------------------------
//   Config
//---------------------------------------------------------

class Style : public QObject
      {
      Q_OBJECT
      QML_ELEMENT
      QML_UNCREATABLE("no")

      PROPV(int, iconSize, 32)

    public:
      explicit Style(QObject* parent = nullptr) : QObject(parent) {}
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
      PROPV (TopLevel*, topLevel, nullptr)
      PROPV (Element3d*, rootElement, nullptr)
      PROPV (Element3d*, currentElement, nullptr)
      PROPV (TreeModel*, treeModel, nullptr)
      PROPV (Machine*, machine, nullptr)
      PROPV (Machines*, machines, nullptr)
      PROPV (Laser*, laser, nullptr)
      PROPV (Recipes*, recipes, nullptr)
      Q_PROPERTY(ProjectManager* projectManager READ projectManager CONSTANT)

      Style* _style{nullptr};
      ProjectManager* _projectManager;
      void initAssets();

    signals:
      void styleChanged();

      void remove3dElement(Element3d*);           // signal 3d gui to remove an element from the scene graph
      void add3dElement(Element3d*);              // signal 3d gui to add a new element into the scene graph
      void addSubElement(Element3d*, Element3d*); // signal 3d gui to add a new subelement into the scene graph
//      void rootElementChanged(Element3d*);        // signal 3d gui to rebuild scene graph
      void startDragElement(Element3d*);          // signal 3d gui to drag this element

    public:
      explicit ZCam(QObject* parent = nullptr);
      Style* style() const { return _style; }
      static ZCam* create(QQmlEngine*, QJSEngine*);
      void undoChangeProperty(Element*, const char*, QVariant) {}
      ProjectManager* projectManager() const { return _projectManager; }
      void loadAssets();
      Q_INVOKABLE void saveAssets();
      };
