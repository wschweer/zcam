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

#include "inspector_model.h"
#include <QMetaProperty>
#include "zcam.h"
#include "machines.h"
#include "projectmanager.h"
#include "element.h"
#include "layer.h"
#include "recipe.h"
#include "laserengine.h"
#include <nlohmann/json.hpp>

//---------------------------------------------------------
//   InspectorModel
//---------------------------------------------------------

InspectorModel::InspectorModel(QObject* parent) : QAbstractListModel(parent) {
      }

//---------------------------------------------------------
//   setElement
//---------------------------------------------------------

void InspectorModel::setElement(Element3d* element) {
      if (_element == element)
            return;
      disconnectPropertySignals();
      _element = element;
      emit elementChanged();
      parseProperties();
      connectPropertySignals();

      // Connect to nameChanged so the title updates when the element is renamed
      if (_element) {
            _nameChangedConnection =
                QObject::connect(_element, &Element::nameChanged, [this]() { updateTitle(); });
            }
      }

void InspectorModel::disconnectPropertySignals() {
      for (auto& conn : _propertyConnections)
            if (conn)
                  QObject::disconnect(conn);
      _propertyConnections.clear();
      _signalToPropIdx.clear();
      if (_nameChangedConnection) {
            QObject::disconnect(_nameChangedConnection);
            _nameChangedConnection = QMetaObject::Connection();
            }
      }

//---------------------------------------------------------
//   updateTitle
//    Rebuild the title string from the stored class name and
//    the element's current name.  Called when the element is
//    renamed (nameChanged signal) so the inspector header stays
//    in sync.
//---------------------------------------------------------

void InspectorModel::updateTitle() {
      if (!_element)
            return;
      // Rebuild the title using the current element name.
      // The class portion was already stored in _title before
      // the separator; we extract it and append the new name.
      int dashPos = _title.indexOf(QChar(0x2013)); // en-dash
      if (dashPos >= 0) {
            // Keep everything up to and including " – " then append the new name
            _title = _title.left(dashPos + 2) + QStringLiteral(" ") + _element->name();
            }
      else
            _title = _element->name();
      emit titleChanged();
      }

//---------------------------------------------------------
//   connectPropertySignals
//    Connect to the NOTIFY signal of every property that the
//    inspector is currently displaying, so that external changes
//    (e.g. from 3D interactions, undo/redo, or programmatic
//    property writes) are reflected in the inspector panel.
//---------------------------------------------------------

void InspectorModel::connectPropertySignals() {
      if (!_element)
            return;

      const QMetaObject* meta = _element->metaObject();
      for (int i = 0; i < _propertyNames.size(); ++i) {
            if (_propertyIsRow[i]) {
                  // For row entries, connect the NOTIFY signal of each sub-property.
                  for (const QString& subName : _subPropNames[i]) {
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
                            QMetaObject::connect(_element, signalIdx, this, slotIdx, Qt::AutoConnection);
                        _propertyConnections.append(conn);
                        }
                  }
            else if (_propertyIsColumns.value(i, false)) {
                  // For columns entries, connect the NOTIFY signal of every
                  // property inside each column item.
                  for (const ColumnItem& ci : _columnItems[i]) {
                        if (ci.isRow) {
                              for (const QString& subName : ci.subProps) {
                                    QByteArray name = subName.toUtf8();
                                    int idx         = meta->indexOfProperty(name.constData());
                                    if (idx < 0)
                                          continue;
                                    QMetaProperty mp = meta->property(idx);
                                    if (!mp.hasNotifySignal())
                                          continue;
                                    QMetaMethod notifySignal = mp.notifySignal();
                                    int signalIdx            = notifySignal.methodIndex();
                                    int slotIdx = this->metaObject()->indexOfMethod("propertyChangedSlot()");
                                    if (slotIdx < 0)
                                          continue;
                                    _signalToPropIdx[signalIdx] = i;
                                    auto conn = QMetaObject::connect(_element, signalIdx, this, slotIdx,
                                                                     Qt::AutoConnection);
                                    _propertyConnections.append(conn);
                                    }
                              }
                        else if (!ci.isLine) {
                              QByteArray name = ci.name.toUtf8();
                              int idx         = meta->indexOfProperty(name.constData());
                              if (idx < 0)
                                    continue;
                              QMetaProperty mp = meta->property(idx);
                              if (!mp.hasNotifySignal())
                                    continue;
                              QMetaMethod notifySignal = mp.notifySignal();
                              int signalIdx            = notifySignal.methodIndex();
                              int slotIdx = this->metaObject()->indexOfMethod("propertyChangedSlot()");
                              if (slotIdx < 0)
                                    continue;
                              _signalToPropIdx[signalIdx] = i;
                              auto conn = QMetaObject::connect(_element, signalIdx, this, slotIdx,
                                                               Qt::AutoConnection);
                              _propertyConnections.append(conn);
                              }
                        }
                  }
            else {
                  QByteArray name = _propertyNames[i].toUtf8();
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
                  auto conn = QMetaObject::connect(_element, signalIdx, this, slotIdx, Qt::AutoConnection);
                  _propertyConnections.append(conn);
                  }
            }
      }

//---------------------------------------------------------
//   propertyChangedSlot
//    Generic slot connected to every NOTIFY signal of the
//    watched element.  Uses senderSignalIndex() to look up
//    which property changed and emits dataChanged so the QML
//    delegate refreshes its displayed value.
//---------------------------------------------------------

void InspectorModel::propertyChangedSlot() {
      int sigIdx = senderSignalIndex();
      auto it    = _signalToPropIdx.constFind(sigIdx);
      if (it == _signalToPropIdx.constEnd())
            return;
      int row = it.value();
      if (row < 0 || row >= _propertyNames.size())
            return;
      // Defer the dataChanged emission to the next event-loop iteration.
      //
      // When the user edits a value in the QML delegate, the call chain is:
      //   QML onValueChanged → setModelValue() → setData()
      //     → changeProperty() → cmd->redo() → setProperty()
      //       → NOTIFY signal → propertyChangedSlot() [HERE]
      //
      // If we emit dataChanged synchronously here, QML immediately
      // re-evaluates the propValue binding (which is bound to
      // model.propValue), which updates modelValue, which triggers
      // onModelValueChanged → value = modelValue → onValueChanged →
      // setModelValue() → … creating a binding loop.
      //
      // By deferring with Qt::QueuedConnection the signal is delivered
      // after the current QML handler stack unwinds, breaking the loop.
      QMetaObject::invokeMethod(
          this,
          [this, row]() {
                if (row < 0 || row >= _propertyNames.size())
                      return;
                QModelIndex idx = index(row, 0);
                emit dataChanged(idx, idx, {PropValueRole, SubValuesRole});
                },
          Qt::QueuedConnection);
      }

//---------------------------------------------------------
//   parseProperties
//---------------------------------------------------------

static void parseColumnsBlock(const nlohmann::ordered_json& block, QList<ColumnItem>& items) {
      int colCount =
          block.contains("count") && block["count"].is_number_integer() ? block["count"].get<int>() : 2;
      // colCount is handled by the caller; here we parse the items
      if (!block.contains("items") || !block["items"].is_array())
            return;
      for (const auto& item : block["items"]) {
            if (item.contains("row") && item["row"].is_object()) {
                  const auto& rowObj = item["row"];
                  ColumnItem ci;
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
                  ColumnItem ci;
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

void InspectorModel::parseProperties() {
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

      if (!_element) {
            endResetModel();
            emit titleChanged();
            emit propertiesJsonChanged();
            return;
            }

      std::string_view propStr = _element->properties();
      if (propStr.empty()) {
            // No inspector definition for this element type; show just the name.
            _title = _element->name();
            endResetModel();
            emit titleChanged();
            emit propertiesJsonChanged();
            return;
            }

      _propertiesJson = QString::fromUtf8(propStr.data(), static_cast<int>(propStr.size()));

      try {
            nlohmann::ordered_json j = nlohmann::ordered_json::parse(propStr);

            // Build title from "class" + element name
            if (j.contains("class") && j["class"].is_string())
                  _title = QString::fromStdString(j["class"].get<std::string>()) + QStringLiteral(" – ") +
                           _element->name();
            else
                  _title = _element->name();

            for (auto it = j.begin(); it != j.end(); ++it) {
                  const std::string& key = it.key();
                  if (key == "class")
                        continue;
                  const auto& jval = it.value();

                  // New format: "items" is an ordered array of entries.
                  // Each entry is one of:
                  //   { "row":  { subName: { "label": ..., ... }, ... } }   - a row group
                  //   { "name": "propName", "label": ..., "type": ..., ... } - a single property
                  //     A property with "type": "line" renders as a separator.
                  //   { "columns": { "count": N, "items": [...] } }   - a multi-column block
                  // This allows arbitrary interleaving of row groups, separators,
                  // individual properties, and multi-column blocks in any order.
                  if (key == "items" && jval.is_array()) {
                        for (const auto& item : jval) {
                              if (item.contains("columns") && item["columns"].is_object()) {
                                    const auto& colsBlock = item["columns"];
                                    int colCount =
                                        colsBlock.contains("count") && colsBlock["count"].is_number_integer()
                                            ? colsBlock["count"].get<int>()
                                            : 2;
                                    QList<ColumnItem> cols;
                                    parseColumnsBlock(colsBlock, cols);
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
                                          _columnItems.append(QList<ColumnItem> {});
                                          _subPropNames.append(subs);
                                          // Extract optional "label" for the row itself
                                          QString rowLabel;
                                          if (item.contains("label") && item["label"].is_string())
                                                rowLabel =
                                                    QString::fromStdString(item["label"].get<std::string>());
                                          _rowLabels.append(rowLabel);
                                          }
                                    }
                              else if (item.contains("name")) {
                                    _propertyNames.append(
                                        QString::fromStdString(item["name"].get<std::string>()));
                                    _propertyIsRow.append(false);
                                    _propertyIsColumns.append(false);
                                    _columnCounts.append(0);
                                    _columnItems.append(QList<ColumnItem> {});
                                    _subPropNames.append(QStringList {});
                                    _rowLabels.append(QString());
                                    }
                              }
                        }
                  // Old format (backward compat): named keys at top level.
                  else if (jval.is_object()) {
                        bool isRow = true;
                        for (auto subIt = jval.begin(); subIt != jval.end(); ++subIt) {
                              if (!subIt.value().contains("label")) {
                                    isRow = false;
                                    break;
                                    }
                              }
                        if (isRow) {
                              _propertyNames.append(QString::fromStdString(key));
                              _propertyIsRow.append(true);
                              _propertyIsColumns.append(false);
                              _columnCounts.append(0);
                              _columnItems.append(QList<ColumnItem> {});
                              QStringList subs;
                              for (auto subIt = jval.begin(); subIt != jval.end(); ++subIt)
                                    subs.append(QString::fromStdString(subIt.key()));
                              _subPropNames.append(subs);
                              _rowLabels.append(QString());
                              }
                        else {
                              if (!jval.contains("label"))
                                    continue;
                              _propertyNames.append(QString::fromStdString(key));
                              _propertyIsRow.append(false);
                              _propertyIsColumns.append(false);
                              _columnCounts.append(0);
                              _columnItems.append(QList<ColumnItem> {});
                              _subPropNames.append(QStringList {});
                              _rowLabels.append(QString());
                              }
                        }
                  else {
                        if (!jval.contains("label"))
                              continue;
                        _propertyNames.append(QString::fromStdString(key));
                        _propertyIsRow.append(false);
                        _propertyIsColumns.append(false);
                        _columnCounts.append(0);
                        _columnItems.append(QList<ColumnItem> {});
                        _subPropNames.append(QStringList {});
                        _rowLabels.append(QString());
                        }
                  }
            }
      catch (const nlohmann::json::parse_error& err) {
            Warning("JSON parse error: {}", err.what());
            }
      catch (const nlohmann::json::type_error& err) {
            Warning("JSON type error: {}", err.what());
            }
      catch (...) {
            Critical("json error");
            }

      endResetModel();
      emit titleChanged();
      emit propertiesJsonChanged();
      }

//---------------------------------------------------------
//   rowCount
//---------------------------------------------------------

int InspectorModel::rowCount(const QModelIndex& parent) const {
      if (parent.isValid())
            return 0;
      return static_cast<int>(_propertyNames.size());
      }

//---------------------------------------------------------
//   data
//---------------------------------------------------------

QVariant InspectorModel::data(const QModelIndex& index, int role) const {
      if (!index.isValid() || index.row() >= static_cast<int>(_propertyNames.size()))
            return {};

      const QString& name = _propertyNames[index.row()];
      bool isRow          = _propertyIsRow[index.row()];

      switch (role) {
            case PropNameRole: return name;
            case PropValueRole:
                  if (_element)
                        return _element->property(name.toUtf8().constData());
                  return {};
            case IsRowRole: return isRow;
            case SubPropsRole: {
                  QVariantList list;
                  for (const QString& s : _subPropNames[index.row()])
                        list.append(s);
                  return list;
                  }
            case SubValuesRole: {
                  if (!_element)
                        return {};
                  QVariantList list;
                  for (const QString& s : _subPropNames[index.row()])
                        list.append(_element->property(s.toUtf8().constData()));
                  return list;
                  }
            case RowLabelRole:
                  if (index.row() < _rowLabels.size())
                        return _rowLabels[index.row()];
                  return QString();
            case IsColumnsRole: return _propertyIsColumns.value(index.row(), false);
            case ColumnCountRole: return _columnCounts.value(index.row(), 0);
            case ColumnItemsRole: {
                  // Serialize the column items as a list of QVariantMaps for QML
                  QVariantList list;
                  if (index.row() < _columnItems.size()) {
                        for (const ColumnItem& ci : _columnItems[index.row()]) {
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
                              // Include sub-values for row-type column items
                              if (ci.isRow && _element) {
                                    QVariantList subVals;
                                    for (const QString& s : ci.subProps)
                                          subVals.append(_element->property(s.toUtf8().constData()));
                                    m["subValues"] = subVals;
                                    }
                              // Include the property value for single-property column items
                              if (!ci.isRow && !ci.isLine && _element)
                                    m["propValue"] = _element->property(ci.name.toUtf8().constData());
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

bool InspectorModel::setData(const QModelIndex& index, const QVariant& value, int role) {
      if (!index.isValid() || index.row() >= static_cast<int>(_propertyNames.size()))
            return false;

      if (role != PropValueRole || !_element)
            return false;

      // For row entries, setData is not used directly; sub-properties
      // are written via setSubProperty().
      if (_propertyIsRow[index.row()])
            return false;

      // For columns entries, setData is not used directly; properties
      // inside columns are written via setColumnProperty().
      if (_propertyIsColumns.value(index.row(), false))
            return false;

      const QString& name = _propertyNames[index.row()];
      QVariant oldValue   = _element->property(name.toUtf8().constData());
      if (oldValue == value)
            return false;
      // Route through the ProjectManager undo system so that every
      // property edit is recorded for undo/redo and marks the project dirty.
      ZCam* zc    = nullptr;
      Element* el = qobject_cast<Element*>(_element);
      if (el)
            zc = el->zcamInstance();
      if (zc && zc->projectManager()) {
            zc->projectManager()->changeProperty(_element, name, value);
            // changeProperty() triggers the element's NOTIFY signal,
            // which propertyChangedSlot() catches and defers a
            // dataChanged emission.  We must NOT emit dataChanged
            // here synchronously — that would re-enter QML bindings
            // and create a binding loop.
            return true;
            }
      else {
            if (_element->setProperty(name.toUtf8().constData(), value)) {
                  QVariant newValue = _element->property(name.toUtf8().constData());
                  if (oldValue != newValue)
                        emit dataChanged(index, index, {role});
                  return true;
                  }
            return false;
            }
      }

//---------------------------------------------------------
//   setSubProperty
//    Set a sub-property that is part of a row entry.
//    The "row" parameter is the model row index of the row entry.
//---------------------------------------------------------

bool InspectorModel::setSubProperty(int row, const QString& subName, const QVariant& value) {
      if (!_element || row < 0 || row >= _propertyNames.size())
            return false;
      if (!_propertyIsRow[row])
            return false;

      QVariant oldValue = _element->property(subName.toUtf8().constData());
      if (oldValue == value)
            return false;

      ZCam* zc    = nullptr;
      Element* el = qobject_cast<Element*>(_element);
      if (el)
            zc = el->zcamInstance();
      if (zc && zc->projectManager()) {
            zc->projectManager()->changeProperty(_element, subName, value);
            return true;
            }
      else {
            if (_element->setProperty(subName.toUtf8().constData(), value)) {
                  QVariant newValue = _element->property(subName.toUtf8().constData());
                  if (oldValue != newValue) {
                        QModelIndex idx = index(row, 0);
                        emit dataChanged(idx, idx, {SubValuesRole});
                        }
                  return true;
                  }
            return false;
            }
      }

//---------------------------------------------------------
//   setColumnProperty
//    Set a property that is inside a "columns" block.
//    The modelRow is the row in the model for the columns block.
//    The propName is the property name inside the column item.
//---------------------------------------------------------

bool InspectorModel::setColumnProperty(int modelRow, const QString& propName, const QVariant& value) {
      if (!_element || modelRow < 0 || modelRow >= _propertyNames.size())
            return false;
      if (!_propertyIsColumns.value(modelRow, false))
            return false;

      QVariant oldValue = _element->property(propName.toUtf8().constData());
      if (oldValue == value)
            return false;

      ZCam* zc    = nullptr;
      Element* el = qobject_cast<Element*>(_element);
      if (el)
            zc = el->zcamInstance();
      if (zc && zc->projectManager()) {
            zc->projectManager()->changeProperty(_element, propName, value);
            return true;
            }
      else {
            if (_element->setProperty(propName.toUtf8().constData(), value)) {
                  QModelIndex idx = index(modelRow, 0);
                  emit dataChanged(idx, idx, {ColumnItemsRole});
                  return true;
                  }
            return false;
            }
      }

//---------------------------------------------------------
//   roleNames
//---------------------------------------------------------

QHash<int, QByteArray> InspectorModel::roleNames() const {
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

//---------------------------------------------------------
//   layerNames
//    Return all Layer names in the current project.
//---------------------------------------------------------

QStringList InspectorModel::layerNames() const {
      if (!_element)
            return {};
      ZCam* zc    = nullptr;
      Element* el = qobject_cast<Element*>(_element);
      if (el)
            zc = el->zcamInstance();
      if (!zc)
            return {};
      return zc->layerNames();
      }

//---------------------------------------------------------
//   recipeNames
//    Return all Recipe names from ZCam::recipes.
//---------------------------------------------------------

QStringList InspectorModel::recipeNames() const {
      if (!_element)
            return {};
      ZCam* zc    = nullptr;
      Element* el = qobject_cast<Element*>(_element);
      if (el)
            zc = el->zcamInstance();
      if (!zc)
            return {};
      return zc->recipeNames();
      }

//---------------------------------------------------------
//   layerToName
//    Resolve a Layer* pointer to its name string.
//---------------------------------------------------------

QString InspectorModel::layerToName(Layer* layer) const {
      if (!layer)
            return {};
      return layer->name();
      }

//---------------------------------------------------------
//   nameToLayer
//    Resolve a name string back to a Layer* pointer.
//---------------------------------------------------------

Layer* InspectorModel::nameToLayer(const QString& name) const {
      if (!_element || name.isEmpty())
            return nullptr;
      ZCam* zc    = nullptr;
      Element* el = qobject_cast<Element*>(_element);
      if (el)
            zc = el->zcamInstance();
      if (!zc)
            return nullptr;
      return zc->layerPtr(name);
      }

//---------------------------------------------------------
//   recipeToName
//    Resolve a Recipe* pointer to its name string.
//---------------------------------------------------------

QString InspectorModel::recipeToName(Recipe* recipe) const {
      if (!recipe)
            return {};
      return recipe->name();
      }

//---------------------------------------------------------
//   nameToRecipe
//    Resolve a name string back to a Recipe* pointer.
//---------------------------------------------------------

Recipe* InspectorModel::nameToRecipe(const QString& name) const {
      if (!_element || name.isEmpty())
            return nullptr;
      ZCam* zc    = nullptr;
      Element* el = qobject_cast<Element*>(_element);
      if (el)
            zc = el->zcamInstance();
      if (!zc)
            return nullptr;
      return zc->recipePtr(name);
      }

//---------------------------------------------------------
//   overrideTypeNames
//    Return the list of ParameterType names from laserengine.h
//    as strings. The order matches the enum values so the
//    QML ComboBox can use the index directly as the enum value.
//---------------------------------------------------------

QStringList InspectorModel::overrideTypeNames() const {
      return {QStringLiteral("None"),     QStringLiteral("Speed"),     QStringLiteral("Power"),
              QStringLiteral("Interval"), QStringLiteral("Frequency"), QStringLiteral("Count"),
              QStringLiteral("Pulse")};
      }

//---------------------------------------------------------
//   pulsewidthNames
//    Return the list of pulse width values from
//    LaserEngine::pulseTable() as strings for use in a QML ComboBox.
//---------------------------------------------------------

QStringList InspectorModel::pulsewidthNames() const {
      if (!_element)
            return {};
      ZCam* zc    = nullptr;
      Element* el = qobject_cast<Element*>(_element);
      if (el)
            zc = el->zcamInstance();
      if (!zc || !zc->laser() || !zc->laser()->engine())
            return {};
      QStringList sl;
      for (const auto& p : zc->laser()->engine()->pulseTable())
            sl << QString::number(p.pulseWidth);
      return sl;
      }

//---------------------------------------------------------
//   joinTypeNames
//    Return the list of Clipper2Lib::JoinType names.
//    The order matches the enum values so the QML ComboBox
//    can use the index directly as the enum value.
//---------------------------------------------------------

QStringList InspectorModel::joinTypeNames() const {
      return {QStringLiteral("Square"), QStringLiteral("Bevel"), QStringLiteral("Round"),
              QStringLiteral("Miter")};
      }

//---------------------------------------------------------
//   endTypeNames
//    Return the list of Clipper2Lib::EndType names.
//    The order matches the enum values so the QML ComboBox
//    can use the index directly as the enum value.
//---------------------------------------------------------

QStringList InspectorModel::endTypeNames() const {
      return {QStringLiteral("Polygon"), QStringLiteral("Joined"), QStringLiteral("Butt"),
              QStringLiteral("Square"), QStringLiteral("Round")};
      }

//---------------------------------------------------------
//   lockScaleNames
//    Return the list of LockScaleMode names.
//    The order matches the enum values so the QML delegate
//    can use the index directly as the enum value.
//---------------------------------------------------------

QStringList InspectorModel::lockScaleNames() const {
      return {QStringLiteral("Off"), QStringLiteral("Lock"), QStringLiteral("Square")};
      }

//---------------------------------------------------------
//   framingTypeNames
//    Return the list of FramingType names.
//    The order matches the enum values so the QML ComboBox
//    can use the index directly as the enum value.
//---------------------------------------------------------

QStringList InspectorModel::framingTypeNames() const {
      return {QStringLiteral("BoundingBox"), QStringLiteral("ConvexHull")};
      }

//---------------------------------------------------------
//   lockSizeNames
//    Return the list of LockScaleMode names for lockSize properties.
//    Uses the same enum values as lockScale.
//---------------------------------------------------------

QStringList InspectorModel::lockSizeNames() const {
      return {QStringLiteral("Off"), QStringLiteral("Lock"), QStringLiteral("Square")};
      }

//---------------------------------------------------------
//   machineNames
//    Return all Machine names from ZCam::machines.
//---------------------------------------------------------

QStringList InspectorModel::machineNames() const {
      if (!_element)
            return {};
      ZCam* zc    = nullptr;
      Element* el = qobject_cast<Element*>(_element);
      if (el)
            zc = el->zcamInstance();
      if (!zc || !zc->machines())
            return {};
      return zc->machines()->machinesModel();
      }

//---------------------------------------------------------
//   machineToName
//    Resolve a Machine* pointer to its name string.
//---------------------------------------------------------

QString InspectorModel::machineToName(Machine* machine) const {
      if (!machine)
            return {};
      return machine->name();
      }

//---------------------------------------------------------
//   nameToMachine
//    Resolve a name string back to a Machine* pointer.
//---------------------------------------------------------

Machine* InspectorModel::nameToMachine(const QString& name) const {
      if (!_element || name.isEmpty())
            return nullptr;
      ZCam* zc    = nullptr;
      Element* el = qobject_cast<Element*>(_element);
      if (el)
            zc = el->zcamInstance();
      if (!zc || !zc->machines())
            return nullptr;
      QStringList model = zc->machines()->machinesModel();
      int idx           = model.indexOf(name);
      if (idx < 0)
            return nullptr;
      return zc->machines()->machine(idx);
      }
