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
#include <QStringList>
#include <QList>
#include <QVariantList>

#include "zcam.h"

//---------------------------------------------------------
//   ConfigColumnItem
//    Describes a single item inside a "columns" block.
//---------------------------------------------------------

struct ConfigColumnItem {
      QString name;
      bool isRow  = false;
      bool isLine = false;
      QStringList subProps;
      QString rowLabel;
      int colSpan = 1;
      };

//---------------------------------------------------------
//   ConfigModel
//    A QAbstractListModel that exposes Config properties to QML,
//    analog to MachineModel but for the QObject-derived Config class.
//    The model wraps a single Config (set via setConfig()) and
//    uses Config::properties() JSON to determine which properties
//    to show and how to render each one.
//
//    Properties can be assigned to categories via the optional
//    "cat" field in the JSON property definition.  The model
//    provides a categories property and a categoryFilter that
//    controls which properties are visible.
//---------------------------------------------------------

class ConfigModel : public QAbstractListModel
      {
      Q_OBJECT
      QML_ELEMENT
      Q_PROPERTY(Config* config READ config WRITE setConfig NOTIFY configChanged)
      Q_PROPERTY(QString title READ title NOTIFY titleChanged)
      Q_PROPERTY(QString propertiesJson READ propertiesJson NOTIFY propertiesJsonChanged)
      Q_PROPERTY(QStringList categories READ categories NOTIFY categoriesChanged)
      Q_PROPERTY(
          QString categoryFilter READ categoryFilter WRITE setCategoryFilter NOTIFY categoryFilterChanged)

    public:
      explicit ConfigModel(QObject* parent = nullptr);
      Config* config() const { return _config; }
      void setConfig(Config* config);
      QString title() const { return _title; }
      QString propertiesJson() const { return _propertiesJson; }
      QStringList categories() const { return _categories; }
      QString categoryFilter() const { return _categoryFilter; }
      void setCategoryFilter(const QString& filter);
      enum Roles {
            PropNameRole = Qt::UserRole + 1,
            PropValueRole,
            IsRowRole,
            SubPropsRole,
            SubValuesRole,
            RowLabelRole,
            IsColumnsRole,
            ColumnCountRole,
            ColumnItemsRole,
            CatRole
            };
      int rowCount(const QModelIndex& parent = QModelIndex()) const override;
      QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
      bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override;
      QHash<int, QByteArray> roleNames() const override;

      Q_INVOKABLE bool setSubProperty(int row, const QString& subName, const QVariant& value);
      Q_INVOKABLE bool setColumnProperty(int modelRow, const QString& propName, const QVariant& value);

      // Called from QML delegates for "machineName" type properties: returns
      // the list of available Machine names from ZCam::machines.
      Q_INVOKABLE QStringList machineNames() const;

    signals:
      void configChanged();
      void titleChanged();
      void propertiesJsonChanged();
      void categoriesChanged();
      void categoryFilterChanged();
      void configDataChanged();

    private:
      void parseProperties();
      void connectPropertySignals();
      void disconnectPropertySignals();

      Config* _config = nullptr;
      QString _title;
      QString _propertiesJson;
      QString _categoryFilter;
      // All parsed property entries (before category filtering)
      struct PropertyEntry {
            QString name;
            QString cat;
            bool isRow      = false;
            bool isColumns  = false;
            int columnCount = 0;
            QStringList subProps;
            QString rowLabel;
            QList<ConfigColumnItem> columnItems;
            };
      QList<PropertyEntry> _allEntries;

      // Indices into _allEntries for the currently visible rows
      QList<int> _visibleIndices;

      QStringList _categories;
      QList<ConfigColumnItem> _emptyColumnItems;
      QList<QMetaObject::Connection> _propertyConnections;
      QHash<int, int> _signalToPropIdx;

    private slots:
      void propertyChangedSlot();
      };