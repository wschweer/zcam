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
#include <QPointer>
#include "machine.h"

//---------------------------------------------------------
//   ColumnItem
//    Describes a single item inside a "columns" block.
//---------------------------------------------------------

struct MachineColumnItem {
      QString name;
      bool isRow = false;
      bool isLine = false;
      QStringList subProps;
      QString rowLabel;
      int colSpan = 1;
      };

//---------------------------------------------------------
//   MachineModel
//    A QAbstractListModel that exposes Machine properties to QML,
//    analog to InspectorModel but for the QObject Machine class.
//    The model wraps a single Machine (set via setMachine()) and
//    uses the Machine::properties() JSON to determine which
//    properties to show and how to render each one.
//---------------------------------------------------------

class MachineModel : public QAbstractListModel
      {
      Q_OBJECT
      QML_ELEMENT
      Q_PROPERTY(Machine* machine READ machine WRITE setMachine NOTIFY machineChanged)
      Q_PROPERTY(QString title READ title NOTIFY titleChanged)
      Q_PROPERTY(QString propertiesJson READ propertiesJson NOTIFY propertiesJsonChanged)

    public:
      explicit MachineModel(QObject* parent = nullptr);
      Machine* machine() const { return _machine; }
      void setMachine(Machine* machine);
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

      Q_INVOKABLE bool setSubProperty(int row, const QString& subName, const QVariant& value);

      // Called from QML to set a property inside a "columns" block.
      Q_INVOKABLE bool setColumnProperty(int modelRow, const QString& propName, const QVariant& value);

      // Returns the list of available machine type strings for "machineType"
      // property delegates in QML.
      Q_INVOKABLE QStringList machineTypes() const;

    signals:
      void machineChanged();
      void titleChanged();
      void propertiesJsonChanged();
      void machineDataChanged();

    private:
      void parseProperties();

      QPointer<Machine> _machine;
      QString _title;
      QString _propertiesJson;

      QStringList _propertyNames;
      QList<bool> _propertyIsRow;
      QList<bool> _propertyIsColumns;
      QList<int> _columnCounts;
      QList<QList<MachineColumnItem>> _columnItems;
      QList<QStringList> _subPropNames;
      QStringList _rowLabels;
      };