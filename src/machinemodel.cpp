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

#include "machinemodel.h"
#include <QMetaProperty>
#include <nlohmann/json.hpp>
#include "logger.h"

//---------------------------------------------------------
//   MachineModel
//---------------------------------------------------------

MachineModel::MachineModel(QObject* parent) : QAbstractListModel(parent) {
      }

//---------------------------------------------------------
//   setMachine
//---------------------------------------------------------

void MachineModel::setMachine(Machine* machine) {
      if (_machine == machine)
            return;
      _machine = machine;
      emit machineChanged();
      parseProperties();
      }

//---------------------------------------------------------
//   parseProperties
//    Parse the Machine::properties() JSON and build the internal
//    model rows, analog to InspectorModel::parseProperties().
//---------------------------------------------------------

static void parseMachineColumnsBlock(const nlohmann::ordered_json& block, QList<MachineColumnItem>& items) {
      if (!block.contains("items") || !block["items"].is_array())
            return;
      for (const auto& item : block["items"]) {
            if (item.contains("row") && item["row"].is_object()) {
                  const auto& rowObj = item["row"];
                  MachineColumnItem ci;
                  ci.isRow = true;
                  ci.name = "row";
                  ci.colSpan = item.contains("colSpan") && item["colSpan"].is_number_integer() ? item["colSpan"].get<int>() : 1;
                  bool allHaveLabel = true;
                  for (auto subIt = rowObj.begin(); subIt != rowObj.end(); ++subIt) {
                        if (!subIt.value().contains("label")) {
                              allHaveLabel = false;
                              break;
                              }
                        ci.subProps.append(QString::fromStdString(subIt.key()));
                        }
                  if (!allHaveLabel || ci.subProps.isEmpty())
                        continue;
                  if (item.contains("label") && item["label"].is_string())
                        ci.rowLabel = QString::fromStdString(item["label"].get<std::string>());
                  items.append(ci);
                  }
            else if (item.contains("name")) {
                  MachineColumnItem ci;
                  ci.name = QString::fromStdString(item["name"].get<std::string>());
                  ci.colSpan = item.contains("colSpan") && item["colSpan"].is_number_integer() ? item["colSpan"].get<int>() : 1;
                  if (item.contains("type") && item["type"].is_string() && item["type"].get<std::string>() == "line")
                        ci.isLine = true;
                  items.append(ci);
                  }
            }
      }

void MachineModel::parseProperties() {
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

      if (!_machine) {
            endResetModel();
            emit titleChanged();
            emit propertiesJsonChanged();
            return;
            }

      std::string_view propStr = _machine->properties();
      if (propStr.empty()) {
            _title = _machine->name();
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
                  _title = _machine->name();

            if (j.contains("items") && j["items"].is_array()) {
                  for (const auto& item : j["items"]) {
                        if (item.contains("columns") && item["columns"].is_object()) {
                              const auto& colsBlock = item["columns"];
                              int colCount = colsBlock.contains("count") && colsBlock["count"].is_number_integer() ? colsBlock["count"].get<int>() : 2;
                              QList<MachineColumnItem> cols;
                              parseMachineColumnsBlock(colsBlock, cols);
                              if (!cols.isEmpty()) {
                                    _propertyNames.append("columns");
                                    _propertyIsRow.append(false);
                                    _propertyIsColumns.append(true);
                                    _columnCounts.append(colCount);
                                    _columnItems.append(cols);
                                    _subPropNames.append(QStringList {});
                                    _rowLabels.append(QString());
                                    }
                              }
                        else if (item.contains("row") && item["row"].is_object()) {
                              const auto& rowObj = item["row"];
                              QStringList subs;
                              bool allHaveLabel = true;
                              for (auto subIt = rowObj.begin(); subIt != rowObj.end(); ++subIt) {
                                    if (!subIt.value().contains("label")) {
                                          allHaveLabel = false;
                                          break;
                                          }
                                    subs.append(QString::fromStdString(subIt.key()));
                                    }
                              if (allHaveLabel && !subs.isEmpty()) {
                                    _propertyNames.append("row");
                                    _propertyIsRow.append(true);
                                    _propertyIsColumns.append(false);
                                    _columnCounts.append(0);
                                    _columnItems.append(QList<MachineColumnItem> {});
                                    _subPropNames.append(subs);
                                    QString rowLabel;
                                    if (item.contains("label") && item["label"].is_string())
                                          rowLabel = QString::fromStdString(item["label"].get<std::string>());
                                    _rowLabels.append(rowLabel);
                                    }
                              }
                        else if (item.contains("name")) {
                              _propertyNames.append(QString::fromStdString(item["name"].get<std::string>()));
                              _propertyIsRow.append(false);
                              _propertyIsColumns.append(false);
                              _columnCounts.append(0);
                              _columnItems.append(QList<MachineColumnItem> {});
                              _subPropNames.append(QStringList {});
                              _rowLabels.append(QString());
                              }
                        }
                  }
            }
      catch (const nlohmann::json::parse_error& err) {
            Warning("MachineModel JSON parse error: {}", err.what());
            }
      catch (...) {
            Critical("MachineModel json error");
            }

      endResetModel();
      emit titleChanged();
      emit propertiesJsonChanged();
      }

//---------------------------------------------------------
//   rowCount
//---------------------------------------------------------

int MachineModel::rowCount(const QModelIndex& parent) const {
      if (parent.isValid())
            return 0;
      return static_cast<int>(_propertyNames.size());
      }

//---------------------------------------------------------
//   data
//    Read property values from the Machine via the Qt meta-object
//    system, using readOnGadget() since Machine is a Q_GADGET.
//---------------------------------------------------------

QVariant MachineModel::data(const QModelIndex& index, int role) const {
      if (!index.isValid() || index.row() >= static_cast<int>(_propertyNames.size()))
            return {};

      const QString& name = _propertyNames[index.row()];
      bool isRow          = _propertyIsRow[index.row()];

      switch (role) {
            case PropNameRole: return name;
            case PropValueRole: {
                  if (!_machine)
                        return {};
                  const QMetaObject* meta = &Machine::staticMetaObject;
                  int idx                 = meta->indexOfProperty(name.toUtf8().constData());
                  if (idx < 0)
                        return {};
                  QMetaProperty mp = meta->property(idx);
                  return mp.readOnGadget(_machine);
                  }
            case IsRowRole: return isRow;
            case SubPropsRole: {
                  QVariantList list;
                  for (const QString& s : _subPropNames[index.row()])
                        list.append(s);
                  return list;
                  }
            case SubValuesRole: {
                  if (!_machine)
                        return {};
                  QVariantList list;
                  const QMetaObject* meta = &Machine::staticMetaObject;
                  for (const QString& s : _subPropNames[index.row()]) {
                        int idx = meta->indexOfProperty(s.toUtf8().constData());
                        if (idx >= 0) {
                              QMetaProperty mp = meta->property(idx);
                              list.append(mp.readOnGadget(_machine));
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
            case IsColumnsRole:
                  return _propertyIsColumns.value(index.row(), false);
            case ColumnCountRole:
                  return _columnCounts.value(index.row(), 0);
            case ColumnItemsRole: {
                  QVariantList list;
                  if (index.row() < _columnItems.size() && _machine) {
                        const QMetaObject* meta = &Machine::staticMetaObject;
                        for (const MachineColumnItem& ci : _columnItems[index.row()]) {
                              QVariantMap m;
                              m["name"] = ci.name;
                              m["isRow"] = ci.isRow;
                              m["isLine"] = ci.isLine;
                              m["colSpan"] = ci.colSpan;
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
                                                subVals.append(mp.readOnGadget(_machine));
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
                                          m["propValue"] = mp.readOnGadget(_machine);
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
//    Write a property value back to the Machine using
//    writeOnGadget() and emit dataChanged.
//---------------------------------------------------------

bool MachineModel::setData(const QModelIndex& index, const QVariant& value, int role) {
      if (!index.isValid() || index.row() >= static_cast<int>(_propertyNames.size()))
            return false;
      if (role != PropValueRole || !_machine)
            return false;
      if (_propertyIsRow[index.row()])
            return false;

      // For columns entries, setData is not used directly; properties
      // inside columns are written via setColumnProperty().
      if (_propertyIsColumns.value(index.row(), false))
            return false;

      const QString& name     = _propertyNames[index.row()];
      const QMetaObject* meta = &Machine::staticMetaObject;
      int idx                 = meta->indexOfProperty(name.toUtf8().constData());
      if (idx < 0)
            return false;
      QMetaProperty mp = meta->property(idx);
      if (!mp.writeOnGadget(_machine, value))
            return false;

      emit dataChanged(index, index, {role});
      emit machineDataChanged();
      return true;
      }

//---------------------------------------------------------
//   setSubProperty
//---------------------------------------------------------

bool MachineModel::setSubProperty(int row, const QString& subName, const QVariant& value) {
      if (!_machine || row < 0 || row >= _propertyNames.size())
            return false;
      if (!_propertyIsRow[row])
            return false;

      const QMetaObject* meta = &Machine::staticMetaObject;
      int idx                 = meta->indexOfProperty(subName.toUtf8().constData());
      if (idx < 0)
            return false;
      QMetaProperty mp = meta->property(idx);
      if (!mp.writeOnGadget(_machine, value))
            return false;

      QModelIndex qi = index(row, 0);
      emit dataChanged(qi, qi, {SubValuesRole});
      emit machineDataChanged();
      return true;
      }

//---------------------------------------------------------
//   setColumnProperty
//---------------------------------------------------------

bool MachineModel::setColumnProperty(int modelRow, const QString& propName, const QVariant& value) {
      if (!_machine || modelRow < 0 || modelRow >= _propertyNames.size())
            return false;
      if (!_propertyIsColumns.value(modelRow, false))
            return false;

      const QMetaObject* meta = &Machine::staticMetaObject;
      int idx                 = meta->indexOfProperty(propName.toUtf8().constData());
      if (idx < 0)
            return false;
      QMetaProperty mp = meta->property(idx);
      if (!mp.writeOnGadget(_machine, value))
            return false;

      QModelIndex qi = index(modelRow, 0);
      emit dataChanged(qi, qi, {ColumnItemsRole});
      emit machineDataChanged();
      return true;
      }

//---------------------------------------------------------
//   roleNames
//---------------------------------------------------------

QHash<int, QByteArray> MachineModel::roleNames() const {
      QHash<int, QByteArray> roles;
      roles[PropNameRole]  = "propName";
      roles[PropValueRole] = "propValue";
      roles[IsRowRole]     = "isRow";
      roles[SubPropsRole]  = "subProps";
      roles[SubValuesRole] = "subValues";
      roles[RowLabelRole]  = "rowLabel";
      roles[IsColumnsRole] = "isColumns";
      roles[ColumnCountRole] = "columnCount";
      roles[ColumnItemsRole] = "columnItems";
      return roles;
      }

//---------------------------------------------------------
//   machineTypes
//    Return the list of available machine type strings.
//---------------------------------------------------------

QStringList MachineModel::machineTypes() const {
      QStringList result;
      for (const auto& t : ::machineTypes)
            result.append(QString::fromStdString(t));
      return result;
      }