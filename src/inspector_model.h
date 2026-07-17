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
#include <QAbstractListModel>
#include <QQmlEngine>
#include <QList>
#include <QStringList>
#include <QVariantList>
#include "element3d.h"
#include "text.h"
#include "layer.h"
#include "recipe.h"
#include "machine.h"

//---------------------------------------------------------
//   ColumnItem
//    Describes a single item inside a "columns" block.
//---------------------------------------------------------

struct ColumnItem {
      QString name;
      bool isRow  = false;
      bool isLine = false;
      QStringList subProps;
      QString rowLabel;
      int colSpan = 1;
      };

//---------------------------------------------------------
//   InspectorModel
//---------------------------------------------------------

class InspectorModel : public QAbstractListModel
      {
      Q_OBJECT
      QML_ELEMENT
      Q_PROPERTY(Element3d* element READ element WRITE setElement NOTIFY elementChanged)
      Q_PROPERTY(QString title READ title NOTIFY titleChanged)
      Q_PROPERTY(QString propertiesJson READ propertiesJson NOTIFY propertiesJsonChanged)

    public:
      explicit InspectorModel(QObject* parent = nullptr);
      Element3d* element() const { return _element; }
      void setElement(Element3d* element);
      QString title() const { return _title; }
      QString propertiesJson() const { return _propertiesJson; }
      enum Roles {
            PropNameRole = Qt::UserRole + 1,
            PropValueRole,
            IsRowRole,
            SubPropsRole,
            SubValuesRole,
            RowLabelRole,
            IsColumnsRole,
            ColumnCountRole,
            ColumnItemsRole
            };
      int rowCount(const QModelIndex& parent = QModelIndex()) const override;
      QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
      bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override;
      QHash<int, QByteArray> roleNames() const override;

      // Called from QML to set a sub-property within a row entry.
      Q_INVOKABLE bool setSubProperty(int row, const QString& subName, const QVariant& value);

      // Called from QML to set a property inside a "columns" block.
      Q_INVOKABLE bool setColumnProperty(int modelRow, const QString& propName, const QVariant& value);

      // Called from QML delegates for "layer" type properties: returns
      // the list of available Layer names in the current project.
      Q_INVOKABLE QStringList layerNames() const;

      // Called from QML delegates for "recipe" type properties: returns
      // the list of available Recipe names from ZCam::recipes.
      Q_INVOKABLE QStringList recipeNames() const;

      // Resolve a Layer* pointer to its name (for display in ComboBox).
      Q_INVOKABLE QString layerToName(Layer* layer) const;

      // Resolve a name back to a Layer* pointer.
      Q_INVOKABLE Layer* nameToLayer(const QString& name) const;

      // Resolve a Recipe* pointer to its name (for display in ComboBox).
      Q_INVOKABLE QString recipeToName(Recipe* recipe) const;

      // Resolve a name back to a Recipe* pointer.
      Q_INVOKABLE Recipe* nameToRecipe(const QString& name) const;

      // Called from QML delegates for "override" type properties: returns
      // the list of available ParameterType names from laserengine.h.
      Q_INVOKABLE QStringList overrideTypeNames() const;

      // Called from QML delegates for "pulsewidth" type properties: returns
      // the list of pulse width values from LaserEngine::pulseTable().
      Q_INVOKABLE QStringList pulsewidthNames() const;

      // Called from QML delegates for "lineJoin" type properties: returns
      // the list of Clipper2Lib::JoinType names.
      Q_INVOKABLE QStringList joinTypeNames() const;

      // Called from QML delegates for "lineEnd" type properties: returns
      // the list of Clipper2Lib::EndType names.
      Q_INVOKABLE QStringList endTypeNames() const;

      // Called from QML delegates for "lockScale" type properties: returns
      // the list of LockScaleMode names.  The order matches the enum values
      // so the QML delegate can use the index directly.
      Q_INVOKABLE QStringList lockScaleNames() const;

      // Called from QML delegates for "framingType" properties: returns
      // the list of FramingType names.  The order matches the enum values
      // so the QML delegate can use the index directly.
      Q_INVOKABLE QStringList framingTypeNames() const;

      // Called from QML delegates for "lockSize" type properties: returns
      // the list of LockScaleMode names (same modes as lockScale).
      // The order matches the enum values so the QML delegate can use
      // the index directly.
      Q_INVOKABLE QStringList lockSizeNames() const;

      // Called from QML delegates for "machine" type properties: returns
      // the list of available Machine names from ZCam::machines.
      Q_INVOKABLE QStringList machineNames() const;

      // Resolve a Machine* pointer to its name (for display in ComboBox).
      Q_INVOKABLE QString machineToName(Machine* machine) const;

      // Resolve a name back to a Machine* pointer.
      Q_INVOKABLE Machine* nameToMachine(const QString& name) const;

    signals:
      void elementChanged();
      void titleChanged();
      void propertiesJsonChanged();

    private:
      void parseProperties();
      void connectPropertySignals();
      void disconnectPropertySignals();

      Element3d* _element = nullptr;
      QString _title;
      QString _propertiesJson;

      // Each entry in the model is either a simple property or a "row" grouping
      // multiple sub-properties on one line.  For simple properties, _propertyNames
      // holds the name and _propertyIsRow is false, _subPropNames is empty.
      // For row entries, _propertyNames holds the key under which the row object
      // was found (e.g. "row"), _propertyIsRow is true, and _subPropNames holds
      // the list of sub-property names contained in that row.
      QStringList _propertyNames;
      QList<bool> _propertyIsRow;
      QList<bool> _propertyIsColumns;
      QList<int> _columnCounts;
      QList<QList<ColumnItem>> _columnItems;
      QList<QStringList> _subPropNames;
      QStringList _rowLabels; // label for row entries (empty for non-row entries)
      QList<QMetaObject::Connection> _propertyConnections;
      QHash<int, int> _signalToPropIdx; // signal method index -> property row
      QMetaObject::Connection _nameChangedConnection;

      void updateTitle();

    private slots:
      void propertyChangedSlot();
      };