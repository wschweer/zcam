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

#include "configmodel.h"
#include "zcam.h"
#include <QMetaProperty>
#include <nlohmann/json.hpp>
#include "logger.h"

//---------------------------------------------------------
//   ConfigModel
//---------------------------------------------------------

ConfigModel::ConfigModel(QObject* parent) : QAbstractListModel(parent) {
      }

//---------------------------------------------------------
//   setConfig
//---------------------------------------------------------

void ConfigModel::setConfig(Config* config) {
      if (_config == config)
            return;
      disconnectPropertySignals();
      _config = config;
      emit configChanged();
      parseProperties();
      connectPropertySignals();
      }

//---------------------------------------------------------
//   setCategoryFilter
//---------------------------------------------------------

void ConfigModel::setCategoryFilter(const QString& filter) {
      if (_categoryFilter == filter)
            return;
      _categoryFilter = filter;
      emit categoryFilterChanged();

      // Rebuild the visible indices list
      beginResetModel();
      _visibleIndices.clear();
      for (int i = 0; i < _allEntries.size(); ++i) {
            const PropertyEntry& entry = _allEntries[i];
            if (_categoryFilter.isEmpty() || entry.cat == _categoryFilter)
                  _visibleIndices.append(i);
            }
      endResetModel();
      }

//---------------------------------------------------------
//   connectPropertySignals
//    Connect to the NOTIFY signal of every property that the
//    config panel is currently displaying, so that external
//    changes are reflected.
//---------------------------------------------------------

void ConfigModel::connectPropertySignals() {
      if (!_config)
            return;

      const QMetaObject* meta = _config->metaObject();
      for (int i = 0; i < _allEntries.size(); ++i) {
            const PropertyEntry& entry = _allEntries[i];
            if (entry.isRow || entry.isColumns) {
                  // For row/columns entries, connect sub-properties
                  for (const QString& subName : entry.subProps) {
                        QByteArray name = subName.toUtf8();
                        int idx         = meta->indexOfProperty(name.constData());
                        if (idx < 0)
                              continue;
                        QMetaProperty mp = meta->property(idx);
                        if (!mp.hasNotifySignal())
                              continue;
                        QMetaMethod notifySignal = mp.notifySignal();
                        int signalIdx            = notifySignal.methodIndex();
                        int slotIdx              = this->metaObject()->indexOfMethod("propertyChangedSlot()");
                        if (slotIdx < 0)
                              continue;
                        _signalToPropIdx[signalIdx] = i;
                        auto conn =
                            QMetaObject::connect(_config, signalIdx, this, slotIdx, Qt::AutoConnection);
                        _propertyConnections.append(conn);
                        }
                  }
            else {
                  QByteArray name = entry.name.toUtf8();
                  int idx         = meta->indexOfProperty(name.constData());
                  if (idx < 0)
                        continue;
                  QMetaProperty mp = meta->property(idx);
                  if (!mp.hasNotifySignal())
                        continue;
                  QMetaMethod notifySignal = mp.notifySignal();
                  int signalIdx            = notifySignal.methodIndex();
                  int slotIdx              = this->metaObject()->indexOfMethod("propertyChangedSlot()");
                  if (slotIdx < 0)
                        continue;
                  _signalToPropIdx[signalIdx] = i;
                  auto conn = QMetaObject::connect(_config, signalIdx, this, slotIdx, Qt::AutoConnection);
                  _propertyConnections.append(conn);
                  }
            }
      }

void ConfigModel::disconnectPropertySignals() {
      for (auto& conn : _propertyConnections)
            if (conn)
                  QObject::disconnect(conn);
      _propertyConnections.clear();
      _signalToPropIdx.clear();
      }

//---------------------------------------------------------
//   propertyChangedSlot
//---------------------------------------------------------

void ConfigModel::propertyChangedSlot() {
      int sigIdx = senderSignalIndex();
      auto it    = _signalToPropIdx.constFind(sigIdx);
      if (it == _signalToPropIdx.constEnd())
            return;
      int allIdx = it.value();
      // Find the visible row for this all-entry index
      int visRow = _visibleIndices.indexOf(allIdx);
      if (visRow < 0)
            return;
      QMetaObject::invokeMethod(
          this,
          [this, visRow]() {
                if (visRow < 0 || visRow >= _visibleIndices.size())
                      return;
                QModelIndex idx = index(visRow, 0);
                emit dataChanged(idx, idx, {PropValueRole, SubValuesRole});
                },
          Qt::QueuedConnection);
      }

//---------------------------------------------------------
//   parseConfigColumnsBlock
//    Parse the items inside a "columns" block, analog to
//    parseMachineColumnsBlock in machinemodel.cpp.
//---------------------------------------------------------

static void parseConfigColumnsBlock(const nlohmann::ordered_json& block, QList<ConfigColumnItem>& items) {
      if (!block.contains("items") || !block["items"].is_array())
            return;
      for (const auto& item : block["items"]) {
            if (item.contains("row") && item["row"].is_object()) {
                  const auto& rowObj = item["row"];
                  ConfigColumnItem ci;
                  ci.isRow          = true;
                  ci.name           = "row";
                  ci.colSpan        = item.contains("colSpan") && item["colSpan"].is_number_integer()
                                          ? item["colSpan"].get<int>()
                                          : 1;
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
                  ConfigColumnItem ci;
                  ci.name    = QString::fromStdString(item["name"].get<std::string>());
                  ci.colSpan = item.contains("colSpan") && item["colSpan"].is_number_integer()
                                   ? item["colSpan"].get<int>()
                                   : 1;
                  if (item.contains("type") && item["type"].is_string() &&
                      item["type"].get<std::string>() == "line")
                        ci.isLine = true;
                  items.append(ci);
                  }
            }
      }

//---------------------------------------------------------
//   parseProperties
//---------------------------------------------------------

void ConfigModel::parseProperties() {
      beginResetModel();
      _allEntries.clear();
      _visibleIndices.clear();
      _categories.clear();
      _title.clear();
      _propertiesJson.clear();

      if (!_config) {
            endResetModel();
            emit titleChanged();
            emit propertiesJsonChanged();
            emit categoriesChanged();
            return;
            }

      std::string_view propStr = _config->properties();
      if (propStr.empty()) {
            _title = QStringLiteral("Config");
            endResetModel();
            emit titleChanged();
            emit propertiesJsonChanged();
            emit categoriesChanged();
            return;
            }

      _propertiesJson = QString::fromUtf8(propStr.data(), static_cast<int>(propStr.size()));

      try {
            nlohmann::ordered_json j = nlohmann::ordered_json::parse(propStr);

            if (j.contains("class") && j["class"].is_string())
                  _title = QString::fromStdString(j["class"].get<std::string>());
            else
                  _title = QStringLiteral("Config");

            if (j.contains("items") && j["items"].is_array()) {
                  for (const auto& item : j["items"]) {
                        if (item.contains("columns") && item["columns"].is_object()) {
                              const auto& colsBlock = item["columns"];
                              PropertyEntry entry;
                              entry.name      = "columns";
                              entry.isColumns = true;
                              entry.columnCount =
                                  colsBlock.contains("count") && colsBlock["count"].is_number_integer()
                                      ? colsBlock["count"].get<int>()
                                      : 2;
                              if (item.contains("cat") && item["cat"].is_string())
                                    entry.cat = QString::fromStdString(item["cat"].get<std::string>());
                              else if (colsBlock.contains("cat") && colsBlock["cat"].is_string())
                                    entry.cat = QString::fromStdString(colsBlock["cat"].get<std::string>());
                              else if (colsBlock.contains("items") && colsBlock["items"].is_array() &&
                                       !colsBlock["items"].empty()) {
                                    const auto& firstColItem = colsBlock["items"][0];
                                    if (firstColItem.contains("cat") && firstColItem["cat"].is_string())
                                          entry.cat =
                                              QString::fromStdString(firstColItem["cat"].get<std::string>());
                                    }
                              // Parse column sub-items into columnItems
                              parseConfigColumnsBlock(colsBlock, entry.columnItems);
                              // Also collect sub-property names for signal connections
                              for (const auto& ci : entry.columnItems) {
                                    if (ci.isRow)
                                          for (const QString& s : ci.subProps)
                                                entry.subProps.append(s);
                                    else if (!ci.isLine)
                                          entry.subProps.append(ci.name);
                                    }
                              _allEntries.append(entry);
                              }
                        else if (item.contains("row") && item["row"].is_object()) {
                              const auto& rowObj = item["row"];
                              PropertyEntry entry;
                              entry.name        = "row";
                              entry.isRow       = true;
                              bool allHaveLabel = true;
                              for (auto subIt = rowObj.begin(); subIt != rowObj.end(); ++subIt) {
                                    if (!subIt.value().contains("label")) {
                                          allHaveLabel = false;
                                          break;
                                          }
                                    entry.subProps.append(QString::fromStdString(subIt.key()));
                                    }
                              if (!allHaveLabel || entry.subProps.isEmpty())
                                    continue;
                              if (item.contains("label") && item["label"].is_string())
                                    entry.rowLabel = QString::fromStdString(item["label"].get<std::string>());
                              // outer cat takes precedence; fall back to first sub-property cat
                              if (item.contains("cat") && item["cat"].is_string())
                                    entry.cat = QString::fromStdString(item["cat"].get<std::string>());
                              else if (!entry.subProps.isEmpty()) {
                                    const auto& firstSub = rowObj[entry.subProps[0].toStdString()];
                                    if (firstSub.contains("cat") && firstSub["cat"].is_string())
                                          entry.cat =
                                              QString::fromStdString(firstSub["cat"].get<std::string>());
                                    }
                              _allEntries.append(entry);
                              }
                        else if (item.contains("name")) {
                              PropertyEntry entry;
                              entry.name = QString::fromStdString(item["name"].get<std::string>());
                              if (item.contains("cat") && item["cat"].is_string())
                                    entry.cat = QString::fromStdString(item["cat"].get<std::string>());
                              _allEntries.append(entry);
                              }
                        } // end for (const auto& item : j["items"])
                  } // end if (j.contains("items"))

            // Build categories list preserving first-occurrence order
            // Skip entries with empty cat to avoid a blank category
            for (const auto& entry : _allEntries)
                  if (!entry.cat.isEmpty() && !_categories.contains(entry.cat))
                        _categories.append(entry.cat);
            }
      catch (const nlohmann::json::parse_error& err) {
            Warning("ConfigModel JSON parse error: {}", err.what());
            }
      catch (...) {
            Critical("ConfigModel json error");
            }

      // Build visible indices: show all if no filter is set
      for (int i = 0; i < _allEntries.size(); ++i)
            if (_categoryFilter.isEmpty() || _allEntries[i].cat == _categoryFilter)
                  _visibleIndices.append(i);

      endResetModel();
      emit titleChanged();
      emit propertiesJsonChanged();
      emit categoriesChanged();
      }

//---------------------------------------------------------
//   rowCount
//---------------------------------------------------------

int ConfigModel::rowCount(const QModelIndex& parent) const {
      if (parent.isValid())
            return 0;
      return static_cast<int>(_visibleIndices.size());
      }

//---------------------------------------------------------
//   data
//---------------------------------------------------------

QVariant ConfigModel::data(const QModelIndex& index, int role) const {
      if (!index.isValid() || index.row() >= static_cast<int>(_visibleIndices.size()))
            return {};

      int allIdx                 = _visibleIndices[index.row()];
      const PropertyEntry& entry = _allEntries[allIdx];

      switch (role) {
            case PropNameRole: return entry.name;
            case PropValueRole: {
                  if (!_config)
                        return {};
                  return _config->property(entry.name.toUtf8().constData());
                  }
            case IsRowRole: return entry.isRow;
            case SubPropsRole: {
                  QVariantList list;
                  for (const QString& s : entry.subProps)
                        list.append(s);
                  return list;
                  }
            case SubValuesRole: {
                  if (!_config)
                        return {};
                  QVariantList list;
                  for (const QString& s : entry.subProps)
                        list.append(_config->property(s.toUtf8().constData()));
                  return list;
                  }
            case RowLabelRole: return entry.rowLabel;
            case IsColumnsRole: return entry.isColumns;
            case ColumnCountRole: return entry.columnCount;
            case ColumnItemsRole: {
                  QVariantList list;
                  if (index.row() < _visibleIndices.size() && _config) {
                        int allIdx2      = _visibleIndices[index.row()];
                        const auto& cols = _allEntries[allIdx2].columnItems;
                        for (const ConfigColumnItem& ci : cols) {
                              QVariantMap m;
                              m["name"]     = ci.name;
                              m["isRow"]    = ci.isRow;
                              m["isLine"]   = ci.isLine;
                              m["colSpan"]  = ci.colSpan;
                              m["rowLabel"] = ci.rowLabel;
                              QVariantList subProps;
                              for (const QString& s : ci.subProps)
                                    subProps.append(s);
                              m["subProps"] = subProps;
                              if (ci.isRow) {
                                    QVariantList subVals;
                                    for (const QString& s : ci.subProps)
                                          subVals.append(_config->property(s.toUtf8().constData()));
                                    m["subValues"] = subVals;
                                    }
                              else if (!ci.isLine) {
                                    m["propValue"] = _config->property(ci.name.toUtf8().constData());
                                    }
                              list.append(m);
                              }
                        }
                  return list;
                  }
            case CatRole: return entry.cat;
            default: return {};
            }
      }

//---------------------------------------------------------
//   setData
//---------------------------------------------------------

bool ConfigModel::setData(const QModelIndex& index, const QVariant& value, int role) {
      if (!index.isValid() || index.row() >= static_cast<int>(_visibleIndices.size()))
            return false;
      if (role != PropValueRole || !_config)
            return false;

      int allIdx                 = _visibleIndices[index.row()];
      const PropertyEntry& entry = _allEntries[allIdx];

      if (entry.isRow || entry.isColumns)
            return false;

      QVariant oldValue = _config->property(entry.name.toUtf8().constData());
      if (oldValue == value)
            return false;

      if (_config->setProperty(entry.name.toUtf8().constData(), value)) {
            emit dataChanged(index, index, {role});
            emit configDataChanged();
            return true;
            }
      return false;
      }

//---------------------------------------------------------
//   setSubProperty
//---------------------------------------------------------

bool ConfigModel::setSubProperty(int row, const QString& subName, const QVariant& value) {
      if (!_config || row < 0 || row >= _visibleIndices.size())
            return false;
      int allIdx = _visibleIndices[row];
      if (!_allEntries[allIdx].isRow)
            return false;

      QVariant oldValue = _config->property(subName.toUtf8().constData());
      if (oldValue == value)
            return false;

      if (_config->setProperty(subName.toUtf8().constData(), value)) {
            QModelIndex qi = index(row, 0);
            emit dataChanged(qi, qi, {SubValuesRole});
            emit configDataChanged();
            return true;
            }
      return false;
      }

//---------------------------------------------------------
//   setColumnProperty
//---------------------------------------------------------

bool ConfigModel::setColumnProperty(int modelRow, const QString& propName, const QVariant& value) {
      if (!_config || modelRow < 0 || modelRow >= _visibleIndices.size())
            return false;
      int allIdx = _visibleIndices[modelRow];
      if (!_allEntries[allIdx].isColumns)
            return false;

      QVariant oldValue = _config->property(propName.toUtf8().constData());
      if (oldValue == value)
            return false;

      if (_config->setProperty(propName.toUtf8().constData(), value)) {
            QModelIndex qi = index(modelRow, 0);
            emit dataChanged(qi, qi, {ColumnItemsRole});
            emit configDataChanged();
            return true;
            }
      return false;
      }

//---------------------------------------------------------
//   machineNames
//    Return all Machine names from ZCam::machines.
//---------------------------------------------------------

QStringList ConfigModel::machineNames() const {
      if (!_config)
            return {};
      ZCam* zc = qobject_cast<ZCam*>(_config->parent());
      if (!zc || !zc->machines())
            return {};
      return zc->machines()->machinesModel();
      }

//---------------------------------------------------------
//   roleNames
//---------------------------------------------------------

QHash<int, QByteArray> ConfigModel::roleNames() const {
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
      roles[CatRole]         = "cat";
      return roles;
      }