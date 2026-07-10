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
#include <QVector3D>
#include <QFont>
#include <QtQml/qqmlregistration.h>

#include "project.h"
#include "projectmanager.h"
#include "recipe.h"
#include "machines.h"
#include "laser.h"
#include "layer.h"

class Project;
class Element3d;
class TreeModel;

//---------------------------------------------------------
//   Config
//---------------------------------------------------------

class Style : public QObject
      {
      Q_OBJECT
      QML_ELEMENT
      QML_UNCREATABLE("no")

      PROPV(int, iconSize, 32)
      PROPV(QFont, font, QFont("arial"))
      PROPV(QColor, panelBG, QColor("darkGray"))

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
      Q_PROPERTY(ProjectManager* projectManager READ projectManager CONSTANT)

      PROPV(Project*, topLevel, nullptr)
      PROPV(Element3d*, rootElement, nullptr)
      PROPV(Element3d*, currentElement, nullptr)
      PROPV(Element3d*, hoverElement, nullptr)
      PROPV(TreeModel*, treeModel, nullptr)
      PROPV(Machine*, machine, nullptr)
      PROPV(Machines*, machines, nullptr)
      PROPV(Laser*, laser, nullptr)
      PROPV(Recipes*, recipes, nullptr)
      PROPV(QString, currentTool, QString("pointer"))

      Style* _style {nullptr};
      ProjectManager* _projectManager;
      void initAssets();

    signals:
      void styleChanged();
      void remove3dElement(Element3d*); // signal 3d gui to remove an element from the scene graph
      void add3dElement(Element3d*);    // signal 3d gui to add a new element into the scene graph
      void addSubElement(Element3d*,
                         Element3d*); // signal 3d gui to add a new subelement into the scene graph
      //      void rootElementChanged(Element3d*);        // signal 3d gui to rebuild scene graph
      void startDragElement(Element3d*); // signal 3d gui to drag this element

    public:
      explicit ZCam(QObject* parent = nullptr);
      Style* style() const { return _style; }
      static ZCam* create(QQmlEngine*, QJSEngine*);
      void undoChangeProperty(Element*, const char*, QVariant) {}
      ProjectManager* projectManager() const { return _projectManager; }
      void loadAssets();
      Q_INVOKABLE void saveAssets();

      /// Called from QML when an element is dragged in the 3D viewport.
      Q_INVOKABLE void dragged(Element3d* element, const QVector3D& delta, int modifiers);

      /// Called from QML when an element is rotated in the 3D viewport.
      Q_INVOKABLE void rotated(Element3d* element, const QVector3D& deltaRotation, int modifiers);

      /// Called from QML when an element is scaled in the 3D viewport.
      Q_INVOKABLE void scaled(Element3d* element, const QVector3D& scaleFactor, int modifiers);

      /// Called from QML when an element is hovered
      Q_INVOKABLE void hover(Element3d* element);
      Q_INVOKABLE void doubleClick(Element3d* element) {}
      Q_INVOKABLE void mousePress(Element3d* element, int buttons, int modifiers, double x, double y);

      /// Returns a list of all Layer element names in the current project.
      Q_INVOKABLE QStringList layerNames() const;
      /// Returns the Layer* pointer for a given layer name, or nullptr.
      Q_INVOKABLE Layer* layerPtr(const QString& name) const;

      /// Returns a list of all Recipe names from ZCam::recipes.
      Q_INVOKABLE QStringList recipeNames() const;
      /// Returns the Recipe* pointer for a given recipe name, or nullptr.
      Q_INVOKABLE Recipe* recipePtr(const QString& name) const;
      };
