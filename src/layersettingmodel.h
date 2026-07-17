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
#include "recipe.h"

//---------------------------------------------------------
//   LayerSettingColumnItem
//---------------------------------------------------------

struct LayerSettingColumnItem {
      QString name;
      bool isRow  = false;
      bool isLine = false;
      QStringList subProps;
      QString rowLabel;
      int colSpan = 1;
      };

//---------------------------------------------------------
//   LayerSettingModel
//    A QAbstractListModel that exposes LaserLayerSetting properties
//    to QML, analog to MachineModel but for the Q_GADGET
//    LaserLayerSetting class.
//---------------------------------------------------------

class LayerSettingModel : public QAbstractListModel
      {
      Q_OBJECT
      QML_ELEMENT
      Q_PROPERTY(LaserPass* pass READ pass WRITE setPass NOTIFY passChanged)
      Q_PROPERTY(QString title READ title NOTIFY titleChanged)
      Q_PROPERTY(QString propertiesJson READ propertiesJson NOTIFY propertiesJsonChanged)

      LaserPass* _pass = nullptr;
      QString _title;
      QString _propertiesJson;

      QStringList _propertyNames;
      QList<bool> _propertyIsRow;
      QList<bool> _propertyIsColumns;
      QList<int> _columnCounts;
      QList<QList<LayerSettingColumnItem>> _columnItems;
      QList<QStringList> _subPropNames;
      QStringList _rowLabels;

      void parseProperties();

    signals:
      void passChanged();
      void titleChanged();
      void propertiesJsonChanged();
      void layerDataChanged();

    public:
      explicit LayerSettingModel(QObject* parent = nullptr);
      LaserPass* pass() const { return _pass; }
      void setPass(LaserPass* pass);
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
      Q_INVOKABLE bool setColumnProperty(int modelRow, const QString& propName, const QVariant& value);
      };