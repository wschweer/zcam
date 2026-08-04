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

#include "layersettingmodel.h"
#include <QMetaProperty>
#include <nlohmann/json.hpp>
#include "logger.h"

//---------------------------------------------------------
//   LayerSettingModel
//---------------------------------------------------------

LayerSettingModel::LayerSettingModel(QObject* parent) : QAbstractListModel(parent) {
      }

//---------------------------------------------------------
//   setPass
//---------------------------------------------------------

void LayerSettingModel::setPass(LaserPass* pass) {
      if (_pass == pass)
            return;
      _pass = pass;
      emit passChanged();
      parseProperties();
      }

//---------------------------------------------------------
//   clearPass
//    Allow QML to set the pass pointer to null.  QML cannot
//    assign null to a LaserPass* Q_PROPERTY directly because
//    LaserPass is a Q_GADGET, not a QObject.
//---------------------------------------------------------

void LayerSettingModel::clearPass() {
      if (!_pass)
            return;
      _pass = nullptr;
      emit passChanged();
      parseProperties();
      }

//---------------------------------------------------------
//   parseColumnsBlock

//---------------------------------------------------------
//   parseProperties
//---------------------------------------------------------

void LayerSettingModel::parseProperties() {
      beginResetModel();
      _propertyNames.clear();
      _propertyIsRow.clear();
      _propertyIsColumns.clear();
      _columnCounts.clear();
      _columnItems.clear();
      _subPropNames.clear();
      _rowLabels.clear();
      _title.clear();
      _propertiesJson.clear();

      if (!_pass) {
            endResetModel();
            emit titleChanged();
            emit propertiesJsonChanged();
            return;
            }

      std::string_view propStr = _pass->properties();
      if (propStr.empty()) {
            _title = _pass->name();
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
                  _title = _pass->name();

            // New format: top-level "rows" array
            if (j.contains("rows") && j["rows"].is_array()) {
                  for (const auto& row : j["rows"]) {
                        int rowColumns = row.contains("columns") && row["columns"].is_number_integer()
                                             ? row["columns"].get<int>()
                                             : 1;

                        if (rowColumns > 1) {
                              QList<LayerSettingColumnItem> cols;
                              if (row.contains("cells") && row["cells"].is_array()) {
                                    for (const auto& cell : row["cells"]) {
                                          LayerSettingColumnItem ci;
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
                                                }
                                          else if (cell.contains("cells") && cell["cells"].is_array()) {
                                                // Row cell: has sub-cells instead of a name
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
                                    _columnItems.append(cols);
                                    _subPropNames.append(QStringList {});
                                    _rowLabels.append(QString());
                                    }
                              }
                        else if (row.contains("cells") && row["cells"].is_array()) {
                              QStringList subs;
                              bool hasLine = false;
                              for (const auto& cell : row["cells"]) {
                                    std::string type = cell.contains("type") && cell["type"].is_string()
                                                           ? cell["type"].get<std::string>()
                                                           : "";
                                    if (type == "line") {
                                          hasLine = true;
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
                                    _columnItems.append(QList<LayerSettingColumnItem> {});
                                    _subPropNames.append(subs);
                                    QString rowLabel;
                                    if (row.contains("label") && row["label"].is_string())
                                          rowLabel = QString::fromStdString(row["label"].get<std::string>());
                                    _rowLabels.append(rowLabel);
                                    }
                              else if (hasLine) {
                                    // Row with only "line" cells → separator
                                    _propertyNames.append("line");
                                    _propertyIsRow.append(false);
                                    _propertyIsColumns.append(false);
                                    _columnCounts.append(0);
                                    _columnItems.append(QList<LayerSettingColumnItem> {});
                                    _subPropNames.append(QStringList {});
                                    _rowLabels.append(QString());
                                    }
                              else {
                                    _propertyNames.append("empty");
                                    _propertyIsRow.append(false);
                                    _propertyIsColumns.append(false);
                                    _columnCounts.append(0);
                                    _columnItems.append(QList<LayerSettingColumnItem> {});
                                    _subPropNames.append(QStringList {});
                                    _rowLabels.append(QString());
                                    }
                              }
                        else {
                              _propertyNames.append("empty");
                              _propertyIsRow.append(false);
                              _propertyIsColumns.append(false);
                              _columnCounts.append(0);
                              _columnItems.append(QList<LayerSettingColumnItem> {});
                              _subPropNames.append(QStringList {});
                              _rowLabels.append(QString());
                              }
                        }
                  }
            }
      catch (const nlohmann::json::parse_error& err) {
            Warning("LayerSettingModel JSON parse error: {}", err.what());
            }
      catch (...) {
            Critical("LayerSettingModel json error");
            }

      endResetModel();
      emit titleChanged();
      emit propertiesJsonChanged();
      }

//---------------------------------------------------------
//   rowCount
//---------------------------------------------------------

int LayerSettingModel::rowCount(const QModelIndex& parent) const {
      if (parent.isValid())
            return 0;
      return static_cast<int>(_propertyNames.size());
      }

//---------------------------------------------------------
//   data
//---------------------------------------------------------

QVariant LayerSettingModel::data(const QModelIndex& index, int role) const {
      if (!index.isValid() || index.row() >= static_cast<int>(_propertyNames.size()))
            return {};

      const QString& name = _propertyNames[index.row()];
      bool isRow          = _propertyIsRow[index.row()];

      switch (role) {
            case PropNameRole: return name;
            case PropValueRole: {
                  if (!_pass)
                        return {};
                  const QMetaObject* meta = &LaserPass::staticMetaObject;
                  int idx                 = meta->indexOfProperty(name.toUtf8().constData());
                  if (idx < 0)
                        return {};
                  QMetaProperty mp = meta->property(idx);
                  return mp.readOnGadget(_pass);
                  }
            case IsRowRole: return isRow;
            case SubPropsRole: {
                  QVariantList list;
                  for (const QString& s : _subPropNames[index.row()])
                        list.append(s);
                  return list;
                  }
            case SubValuesRole: {
                  if (!_pass)
                        return {};
                  QVariantList list;
                  const QMetaObject* meta = &LaserPass::staticMetaObject;
                  for (const QString& s : _subPropNames[index.row()]) {
                        int idx = meta->indexOfProperty(s.toUtf8().constData());
                        if (idx >= 0) {
                              QMetaProperty mp = meta->property(idx);
                              list.append(mp.readOnGadget(_pass));
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
            case ColumnItemsRole: {
                  QVariantList list;
                  if (index.row() < _columnItems.size() && _pass) {
                        const QMetaObject* meta = &LaserPass::staticMetaObject;
                        for (const LayerSettingColumnItem& ci : _columnItems[index.row()]) {
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
                                                subVals.append(mp.readOnGadget(_pass));
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
                                          m["propValue"]   = mp.readOnGadget(_pass);
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
//---------------------------------------------------------

bool LayerSettingModel::setData(const QModelIndex& index, const QVariant& value, int role) {
      if (!index.isValid() || index.row() >= static_cast<int>(_propertyNames.size()))
            return false;
      if (role != PropValueRole || !_pass)
            return false;
      if (_propertyIsRow[index.row()])
            return false;

      if (_propertyIsColumns.value(index.row(), false))
            return false;

      const QString& name     = _propertyNames[index.row()];
      const QMetaObject* meta = &LaserPass::staticMetaObject;
      int idx                 = meta->indexOfProperty(name.toUtf8().constData());
      if (idx < 0)
            return false;
      QMetaProperty mp = meta->property(idx);
      if (!mp.writeOnGadget(_pass, value))
            return false;

      emit dataChanged(index, index, {role});
      emit layerDataChanged();
      return true;
      }

//---------------------------------------------------------
//   setSubProperty
//---------------------------------------------------------

bool LayerSettingModel::setSubProperty(int row, const QString& subName, const QVariant& value) {
      if (!_pass || row < 0 || row >= _propertyNames.size())
            return false;
      if (!_propertyIsRow[row])
            return false;

      const QMetaObject* meta = &LaserPass::staticMetaObject;
      int idx                 = meta->indexOfProperty(subName.toUtf8().constData());
      if (idx < 0)
            return false;
      QMetaProperty mp = meta->property(idx);
      if (!mp.writeOnGadget(_pass, value))
            return false;

      QModelIndex qi = index(row, 0);
      emit dataChanged(qi, qi, {SubValuesRole});
      emit layerDataChanged();
      return true;
      }

//---------------------------------------------------------
//   setColumnProperty
//---------------------------------------------------------

bool LayerSettingModel::setColumnProperty(int modelRow, const QString& propName, const QVariant& value) {
      if (!_pass || modelRow < 0 || modelRow >= _propertyNames.size())
            return false;
      if (!_propertyIsColumns.value(modelRow, false))
            return false;

      const QMetaObject* meta = &LaserPass::staticMetaObject;
      int idx                 = meta->indexOfProperty(propName.toUtf8().constData());
      if (idx < 0)
            return false;
      QMetaProperty mp = meta->property(idx);
      if (!mp.writeOnGadget(_pass, value))
            return false;

      QModelIndex qi = index(modelRow, 0);
      emit dataChanged(qi, qi, {ColumnItemsRole});
      emit layerDataChanged();
      return true;
      }

//---------------------------------------------------------
//   elementProperty
//    Read any property value from the current pass by name.
//    Used by the QML PropertyEditor to evaluate the "enabled" keyword.
//---------------------------------------------------------

QVariant LayerSettingModel::elementProperty(const QString& name) const {
      if (!_pass || name.isEmpty())
            return {};
      const QMetaObject* meta = &LaserPass::staticMetaObject;
      int idx                 = meta->indexOfProperty(name.toUtf8().constData());
      if (idx < 0)
            return {};
      QMetaProperty mp = meta->property(idx);
      return mp.readOnGadget(_pass);
      }

//---------------------------------------------------------
//   roleNames
//---------------------------------------------------------

QHash<int, QByteArray> LayerSettingModel::roleNames() const {
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
      return roles;
      }
