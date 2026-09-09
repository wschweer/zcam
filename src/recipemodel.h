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
#include "recipe.h"

//---------------------------------------------------------
//   RecipeColumnItem
//    Describes a single item inside a "columns" block.
//---------------------------------------------------------

struct RecipeColumnItem {
      QString name;
      bool isRow   = false;
      bool isLine  = false;
      bool isEmpty = false;
      QStringList subProps;
      QString rowLabel;
      int colSpan    = 1;
      int labelWidth = -1;
      };

//---------------------------------------------------------
//   RecipeModel
//    A QAbstractListModel that exposes LaserRecipe properties to QML,
//    analogous to MachineModel but for the QObject LaserRecipe class.
//    The model wraps a single LaserRecipe (set via setRecipe()) and
//    uses the LaserRecipe::properties() JSON to determine which
//    properties to show and how to render each one.
//---------------------------------------------------------

class RecipeModel : public QAbstractListModel
      {
      Q_OBJECT
      QML_ELEMENT
      Q_PROPERTY(LaserRecipe* recipe READ recipe WRITE setRecipe NOTIFY recipeChanged)
      Q_PROPERTY(QString title READ title NOTIFY titleChanged)
      Q_PROPERTY(QString propertiesJson READ propertiesJson NOTIFY propertiesJsonChanged)

    public:
      explicit RecipeModel(QObject* parent = nullptr);
      LaserRecipe* recipe() const { return _recipe; }
      void setRecipe(LaserRecipe* recipe);
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
            ColumnItemsRole,
            LabelWidthRole
            };
      int rowCount(const QModelIndex& parent = QModelIndex()) const override;
      QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
      bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override;
      QHash<int, QByteArray> roleNames() const override;

      Q_INVOKABLE bool setSubProperty(int row, const QString& subName, const QVariant& value);
      Q_INVOKABLE bool setColumnProperty(int modelRow, const QString& propName, const QVariant& value);
      Q_INVOKABLE QVariant elementProperty(const QString& name) const;

      // Returns the list of available machine type strings for "machineType"
      // property delegates in QML.
      Q_INVOKABLE QStringList machineTypes() const;

    signals:
      void recipeChanged();
      void titleChanged();
      void propertiesJsonChanged();
      void recipeDataChanged();

    private Q_SLOTS:
      void onRecipePropertyChanged();

    private:
      void parseProperties();
      void connectRecipeNotify();

      QPointer<LaserRecipe> _recipe;
      QString _title;
      QString _propertiesJson;

      QStringList _propertyNames;
      QList<bool> _propertyIsRow;
      QList<bool> _propertyIsColumns;
      QList<int> _columnCounts;
      QList<int> _rowLabelWidths;
      QList<QList<RecipeColumnItem>> _columnItems;
      QList<QStringList> _subPropNames;
      QStringList _rowLabels;
      };