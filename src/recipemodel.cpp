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

#include "recipemodel.h"
#include <QMetaProperty>
#include <nlohmann/json.hpp>
#include "logger.h"

//---------------------------------------------------------
//   RecipeModel
//---------------------------------------------------------

RecipeModel::RecipeModel(QObject* parent) : QAbstractListModel(parent) {
      }

//---------------------------------------------------------
//   connectRecipeNotify
//    Connect all Q_PROPERTY NOTIFY signals of the recipe to
//    onRecipePropertyChanged so external changes are reflected
//    in the QML PropertyEditor immediately.
//---------------------------------------------------------

void RecipeModel::connectRecipeNotify() {
      if (!_recipe)
            return;
      const QMetaObject* meta = _recipe->metaObject();
      for (int i = 0; i < meta->propertyCount(); ++i) {
            QMetaProperty mp = meta->property(i);
            if (!mp.hasNotifySignal())
                  continue;
            QByteArray sig = "2" + mp.notifySignal().methodSignature();
            connect(_recipe, sig, this, SLOT(onRecipePropertyChanged()));
            }
      }

//---------------------------------------------------------
//   setRecipe
//---------------------------------------------------------

void RecipeModel::setRecipe(LaserRecipe* recipe) {
      if (_recipe == recipe)
            return;
      _recipe = recipe;
      emit recipeChanged();
      parseProperties();

      if (_recipe)
            connectRecipeNotify();
      }

//---------------------------------------------------------
//   onRecipePropertyChanged
//    Called when any Q_PROPERTY of the current recipe changes.
//    Refreshes all model rows so QML shows the new values.
//---------------------------------------------------------

void RecipeModel::onRecipePropertyChanged() {
      if (_propertyNames.isEmpty())
            return;
      QModelIndex first = index(0, 0);
      QModelIndex last  = index(rowCount() - 1, 0);
      emit dataChanged(first, last, {PropValueRole, SubValuesRole, ColumnItemsRole});
      emit recipeDataChanged();
      }

//---------------------------------------------------------
//   parseProperties
//    Parse the LaserRecipe::properties() JSON and build the
//    internal model rows, analogous to MachineModel::parseProperties().
//---------------------------------------------------------

void RecipeModel::parseProperties() {
      beginResetModel();
      _propertyNames.clear();
      _propertyIsRow.clear();
      _propertyIsColumns.clear();
      _columnCounts.clear();
      _rowLabelWidths.clear();
      _columnItems.clear();
      _subPropNames.clear();
      _rowLabels.clear();
      _title.clear();
      _propertiesJson.clear();

      if (!_recipe) {
            endResetModel();
            emit titleChanged();
            emit propertiesJsonChanged();
            return;
            }

      std::string propStr = _recipe->properties();
      if (propStr.empty()) {
            _title = _recipe->name();
            endResetModel();
            emit titleChanged();
            emit propertiesJsonChanged();
            return;
            }

      _propertiesJson = QString::fromUtf8(propStr.data(), static_cast<int>(propStr.size()));

      try {
            nlohmann::ordered_json j = nlohmann::ordered_json::parse(propStr);

            if (j.contains("class") && j["class"].is_string())
                  _title = QString::fromStdString(j["class"].get<std::string>());
            else
                  _title = _recipe->name();

            if (j.contains("rows") && j["rows"].is_array()) {
                  for (const auto& row : j["rows"]) {
                        int rowColumns = row.contains("columns") && row["columns"].is_number_integer()
                                             ? row["columns"].get<int>()
                                             : 1;

                        int rowLabelWidth =
                            row.contains("labelWidth") && row["labelWidth"].is_number_integer()
                                ? row["labelWidth"].get<int>()
                                : -1;

                        if (rowColumns > 1) {
                              QList<RecipeColumnItem> cols;
                              if (row.contains("cells") && row["cells"].is_array()) {
                                    for (const auto& cell : row["cells"]) {
                                          RecipeColumnItem ci;
                                          ci.colSpan =
                                              cell.contains("colSpan") && cell["colSpan"].is_number_integer()
                                                  ? cell["colSpan"].get<int>()
                                                  : 1;
                                          std::string type = cell.contains("type") && cell["type"].is_string()
                                                                 ? cell["type"].get<std::string>()
                                                                 : "";
                                          if (type == "line") {
                                                ci.isLine = true;
                                                ci.name   = "line";
                                                if (cell.contains("label") && cell["label"].is_string())
                                                      ci.rowLabel = QString::fromStdString(
                                                          cell["label"].get<std::string>());
                                                }
                                          else if (cell.contains("cells") && cell["cells"].is_array()) {
                                                ci.isRow = true;
                                                ci.name  = "row";
                                                for (const auto& subCell : cell["cells"]) {
                                                      std::string subType =
                                                          subCell.contains("type") &&
                                                                  subCell["type"].is_string()
                                                              ? subCell["type"].get<std::string>()
                                                              : "";
                                                      if (subType == "line" || !subCell.contains("name"))
                                                            continue;
                                                      if (subType == "empty") {
                                                            ci.subProps.append("empty");
                                                            continue;
                                                            }
                                                      ci.subProps.append(QString::fromStdString(
                                                          subCell["name"].get<std::string>()));
                                                      }
                                                if (cell.contains("label") && cell["label"].is_string())
                                                      ci.rowLabel = QString::fromStdString(
                                                          cell["label"].get<std::string>());
                                                }
                                          else if (type == "empty" || !cell.contains("name")) {
                                                ci.isEmpty = true;
                                                ci.name    = "empty";
                                                }
                                          else {
                                                ci.name =
                                                    QString::fromStdString(cell["name"].get<std::string>());
                                                }
                                          cols.append(ci);
                                          }
                                    }
                              if (!cols.isEmpty()) {
                                    _propertyNames.append("columns");
                                    _propertyIsRow.append(false);
                                    _propertyIsColumns.append(true);
                                    _columnCounts.append(rowColumns);
                                    _rowLabelWidths.append(rowLabelWidth);
                                    _columnItems.append(cols);
                                    _subPropNames.append(QStringList {});
                                    _rowLabels.append(QString());
                                    }
                              }
                        else if (row.contains("cells") && row["cells"].is_array()) {
                              QStringList subs;
                              bool hasLine = false;
                              QString lineLabel;
                              for (const auto& cell : row["cells"]) {
                                    std::string type = cell.contains("type") && cell["type"].is_string()
                                                           ? cell["type"].get<std::string>()
                                                           : "";
                                    if (type == "line") {
                                          hasLine = true;
                                          if (cell.contains("label") && cell["label"].is_string())
                                                lineLabel =
                                                    QString::fromStdString(cell["label"].get<std::string>());
                                          continue;
                                          }
                                    if (type == "empty") {
                                          subs.append("empty");
                                          continue;
                                          }
                                    if (!cell.contains("name"))
                                          continue;
                                    subs.append(QString::fromStdString(cell["name"].get<std::string>()));
                                    }
                              if (!subs.isEmpty()) {
                                    _propertyNames.append("row");
                                    _propertyIsRow.append(true);
                                    _propertyIsColumns.append(false);
                                    _columnCounts.append(0);
                                    _rowLabelWidths.append(-1);
                                    _columnItems.append(QList<RecipeColumnItem> {});
                                    _subPropNames.append(subs);
                                    QString rowLabel;
                                    if (row.contains("label") && row["label"].is_string())
                                          rowLabel = QString::fromStdString(row["label"].get<std::string>());
                                    _rowLabels.append(rowLabel);
                                    }
                              else if (hasLine) {
                                    _propertyNames.append("line");
                                    _propertyIsRow.append(false);
                                    _propertyIsColumns.append(false);
                                    _columnCounts.append(0);
                                    _rowLabelWidths.append(-1);
                                    _columnItems.append(QList<RecipeColumnItem> {});
                                    _subPropNames.append(QStringList {});
                                    _rowLabels.append(lineLabel);
                                    }
                              else {
                                    _propertyNames.append("empty");
                                    _propertyIsRow.append(false);
                                    _propertyIsColumns.append(false);
                                    _columnCounts.append(0);
                                    _rowLabelWidths.append(-1);
                                    _columnItems.append(QList<RecipeColumnItem> {});
                                    _subPropNames.append(QStringList {});
                                    _rowLabels.append(QString());
                                    }
                              }
                        else {
                              _propertyNames.append("empty");
                              _propertyIsRow.append(false);
                              _propertyIsColumns.append(false);
                              _columnCounts.append(0);
                              _rowLabelWidths.append(-1);
                              _columnItems.append(QList<RecipeColumnItem> {});
                              _subPropNames.append(QStringList {});
                              _rowLabels.append(QString());
                              }
                        }
                  }
            }
      catch (const nlohmann::json::parse_error& err) {
            Warning("RecipeModel JSON parse error: {}", err.what());
            }
      catch (...) {
            Critical("RecipeModel json error");
            }

      endResetModel();
      emit titleChanged();
      emit propertiesJsonChanged();
      }

//---------------------------------------------------------
//   rowCount
//---------------------------------------------------------

int RecipeModel::rowCount(const QModelIndex& parent) const {
      if (parent.isValid())
            return 0;
      return static_cast<int>(_propertyNames.size());
      }

//---------------------------------------------------------
//   data
//    Read property values from the LaserRecipe via the Qt
//    meta-object system, using read() since LaserRecipe is a QObject.
//---------------------------------------------------------

QVariant RecipeModel::data(const QModelIndex& index, int role) const {
      if (!index.isValid() || index.row() >= static_cast<int>(_propertyNames.size()))
            return {};

      const QString& name = _propertyNames[index.row()];
      bool isRow          = _propertyIsRow[index.row()];

      switch (role) {
            case PropNameRole: return name;
            case PropValueRole: {
                  if (!_recipe)
                        return {};
                  const QMetaObject* meta = _recipe->metaObject();
                  int idx                 = meta->indexOfProperty(name.toUtf8().constData());
                  if (idx >= 0) {
                        QMetaProperty mp = meta->property(idx);
                        QVariant value   = mp.read(_recipe);
                        // For the MachineType enum "machineType" property,
                        // return the human-readable string name so the QML
                        // stringComboDelegate can display it and match it
                        // against machineTypes().
                        if (name == QStringLiteral("machineType") && value.canConvert<int>())
                              return QString::fromUtf8(
                                  machineTypeMap.name(static_cast<MachineType>(value.toInt())));
                        return value;
                        }
                  return {};
                  }
            case IsRowRole: return isRow;
            case SubPropsRole: {
                  QVariantList list;
                  for (const QString& s : _subPropNames[index.row()])
                        list.append(s);
                  return list;
                  }
            case SubValuesRole: {
                  if (!_recipe)
                        return {};
                  QVariantList list;
                  const QMetaObject* meta = _recipe->metaObject();
                  for (const QString& s : _subPropNames[index.row()]) {
                        int idx = meta->indexOfProperty(s.toUtf8().constData());
                        if (idx >= 0) {
                              QMetaProperty mp = meta->property(idx);
                              QVariant v       = mp.read(_recipe);
                              if (s == QStringLiteral("machineType") && v.canConvert<int>())
                                    v = QString::fromUtf8(
                                        machineTypeMap.name(static_cast<MachineType>(v.toInt())));
                              list.append(v);
                              }
                        else
                              list.append(QVariant());
                        }
                  return list;
                  }
            case RowLabelRole:
                  if (index.row() < _rowLabels.size())
                        return _rowLabels[index.row()];
                  return QString();
            case IsColumnsRole: return _propertyIsColumns.value(index.row(), false);
            case ColumnCountRole: return _columnCounts.value(index.row(), 0);
            case LabelWidthRole: return _rowLabelWidths.value(index.row(), -1);
            case ColumnItemsRole: {
                  QVariantList list;
                  if (index.row() < _columnItems.size() && _recipe) {
                        const QMetaObject* meta = _recipe->metaObject();
                        for (const RecipeColumnItem& ci : _columnItems[index.row()]) {
                              QVariantMap m;
                              m["name"]     = ci.name;
                              m["isRow"]    = ci.isRow;
                              m["isLine"]   = ci.isLine;
                              m["isEmpty"]  = ci.isEmpty;
                              m["colSpan"]  = ci.colSpan;
                              m["rowLabel"] = ci.rowLabel;
                              QVariantList subProps;
                              for (const QString& s : ci.subProps)
                                    subProps.append(s);
                              m["subProps"] = subProps;
                              if (ci.isRow) {
                                    QVariantList subVals;
                                    for (const QString& s : ci.subProps) {
                                          int idx = meta->indexOfProperty(s.toUtf8().constData());
                                          if (idx >= 0) {
                                                QMetaProperty mp = meta->property(idx);
                                                QVariant v       = mp.read(_recipe);
                                                if (s == QStringLiteral("machineType") && v.canConvert<int>())
                                                      v = QString::fromUtf8(machineTypeMap.name(
                                                          static_cast<MachineType>(v.toInt())));
                                                subVals.append(v);
                                                }
                                          else
                                                subVals.append(QVariant());
                                          }
                                    m["subValues"] = subVals;
                                    }
                              else if (!ci.isLine) {
                                    int idx = meta->indexOfProperty(ci.name.toUtf8().constData());
                                    if (idx >= 0) {
                                          QMetaProperty mp = meta->property(idx);
                                          QVariant v       = mp.read(_recipe);
                                          if (ci.name == QStringLiteral("machineType") && v.canConvert<int>())
                                                v = QString::fromUtf8(
                                                    machineTypeMap.name(static_cast<MachineType>(v.toInt())));
                                          m["propValue"] = v;
                                          }
                                    }
                              list.append(m);
                              }
                        }
                  return list;
                  }
            default: return {};
            }
      }

//---------------------------------------------------------
//   setData
//    Write a property value back to the LaserRecipe using
//    write() and emit dataChanged.
//---------------------------------------------------------

bool RecipeModel::setData(const QModelIndex& index, const QVariant& value, int role) {
      if (!index.isValid() || index.row() >= static_cast<int>(_propertyNames.size()))
            return false;
      if (role != PropValueRole || !_recipe)
            return false;
      if (_propertyIsRow[index.row()])
            return false;
      if (_propertyIsColumns.value(index.row(), false))
            return false;

      const QString& name     = _propertyNames[index.row()];
      const QMetaObject* meta = _recipe->metaObject();
      int idx                 = meta->indexOfProperty(name.toUtf8().constData());
      if (idx < 0)
            return false;
      QMetaProperty mp = meta->property(idx);
      // For the MachineType enum, convert the string name back to
      // the enum value before writing.
      QVariant writeValue = value;
      if (name == QStringLiteral("machineType") && value.canConvert<QString>()) {
            auto mt = machineTypeMap.type(value.toString().toUtf8().constData());
            if (mt.has_value())
                  writeValue = static_cast<int>(mt.value());
            }
      if (!mp.write(_recipe, writeValue))
            return false;

      emit dataChanged(index, index, {role});
      emit recipeDataChanged();
      return true;
      }

//---------------------------------------------------------
//   setSubProperty
//---------------------------------------------------------

bool RecipeModel::setSubProperty(int row, const QString& subName, const QVariant& value) {
      if (!_recipe || row < 0 || row >= _propertyNames.size())
            return false;
      if (!_propertyIsRow[row])
            return false;

      const QMetaObject* meta = _recipe->metaObject();
      int idx                 = meta->indexOfProperty(subName.toUtf8().constData());
      if (idx < 0)
            return false;
      QMetaProperty mp = meta->property(idx);
      // For the MachineType enum, convert the string name back to
      // the enum value before writing.
      QVariant writeValue = value;
      if (subName == QStringLiteral("machineType") && value.canConvert<QString>()) {
            auto mt = machineTypeMap.type(value.toString().toUtf8().constData());
            if (mt.has_value())
                  writeValue = static_cast<int>(mt.value());
            }
      if (!mp.write(_recipe, writeValue))
            return false;

      QModelIndex qi = index(row, 0);
      emit dataChanged(qi, qi, {SubValuesRole});
      emit recipeDataChanged();
      return true;
      }

//---------------------------------------------------------
//   setColumnProperty
//---------------------------------------------------------

bool RecipeModel::setColumnProperty(int modelRow, const QString& propName, const QVariant& value) {
      if (!_recipe || modelRow < 0 || modelRow >= _propertyNames.size())
            return false;
      if (!_propertyIsColumns.value(modelRow, false))
            return false;

      const QMetaObject* meta = _recipe->metaObject();
      int idx                 = meta->indexOfProperty(propName.toUtf8().constData());
      if (idx < 0)
            return false;
      QMetaProperty mp = meta->property(idx);
      // For the MachineType enum, convert the string name back to
      // the enum value before writing.
      QVariant writeValue = value;
      if (propName == QStringLiteral("machineType") && value.canConvert<QString>()) {
            auto mt = machineTypeMap.type(value.toString().toUtf8().constData());
            if (mt.has_value())
                  writeValue = static_cast<int>(mt.value());
            }
      if (!mp.write(_recipe, writeValue))
            return false;

      QModelIndex qi = index(modelRow, 0);
      emit dataChanged(qi, qi, {ColumnItemsRole});
      emit recipeDataChanged();
      return true;
      }

//---------------------------------------------------------
//   elementProperty
//    Read any property value from the current recipe by name.
//    Used by the QML PropertyEditor to evaluate the "enabled" keyword.
//---------------------------------------------------------

QVariant RecipeModel::elementProperty(const QString& name) const {
      if (!_recipe || name.isEmpty())
            return {};
      const QMetaObject* meta = _recipe->metaObject();
      int idx                 = meta->indexOfProperty(name.toUtf8().constData());
      if (idx < 0)
            return {};
      QMetaProperty mp = meta->property(idx);
      QVariant value   = mp.read(_recipe);
      if (name == QStringLiteral("machineType") && value.canConvert<int>())
            return QString::fromUtf8(machineTypeMap.name(static_cast<MachineType>(value.toInt())));
      return value;
      }

//---------------------------------------------------------
//   roleNames
//---------------------------------------------------------

QHash<int, QByteArray> RecipeModel::roleNames() const {
      QHash<int, QByteArray> roles;
      roles[PropNameRole]    = "propName";
      roles[PropValueRole]   = "propValue";
      roles[IsRowRole]       = "isRow";
      roles[SubPropsRole]    = "subProps";
      roles[SubValuesRole]   = "subValues";
      roles[RowLabelRole]    = "rowLabel";
      roles[IsColumnsRole]   = "isColumns";
      roles[ColumnCountRole] = "columnCount";
      roles[ColumnItemsRole] = "columnItems";
      roles[LabelWidthRole]  = "labelWidth";
      return roles;
      }

//---------------------------------------------------------
//   machineTypes
//    Return the list of available machine type strings.
//---------------------------------------------------------

QStringList RecipeModel::machineTypes() const {
      QStringList result;
      for (std::size_t i = 0; i < machineTypeMap.size(); ++i)
            result.append(QString::fromUtf8(machineTypeMap.nameAt(static_cast<MachineType>(i))));
      return result;
      }