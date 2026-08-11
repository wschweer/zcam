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
#include <QVector2D>
#include <QVector3D>
#include <QtMultimedia/qcameradevice.h>
#include <QtMultimedia/qmediadevices.h>
#include "zcam.h"
#include "machines.h"
#include "project.h"
#include "undo.h"
#include "element.h"
#include "group.h"
#include "recipe.h"
#include "laser.h"
#include "scriptengine.h"
#include <nlohmann/json.hpp>

//---------------------------------------------------------
//   InspectorModel
//---------------------------------------------------------

InspectorModel::InspectorModel(QObject* parent) : QAbstractListModel(parent) {
      }

//---------------------------------------------------------
//   isElementBeingDragged
//    Returns true if the inspector's element is currently being
//    dragged/rotated/scaled on the 3D canvas.  During a drag the
//    element's transform properties (pos, rot, scale) are updated
//    directly by ZCam::dragged()/rotated()/scaled() and a single
//    undo command is pushed in endElementDrag().  The inspector
//    must NOT write back through changeProperty() in this interval
//    because the NOTIFY signal from the direct set_pos/set_rot/
//    set_scale triggers dataChanged in the inspector, which causes
//    QML to re-evaluate its bindings and call setData()/setSubProperty()
//    → changeProperty() — a spurious call that corrupts the undo
//    history.
//---------------------------------------------------------

bool InspectorModel::isElementBeingDragged() const {
      if (!_element)
            return false;
      ZCam* zc = _element->zcamInstance();
      if (!zc)
            return false;
      return zc->elementDragElement() == _element;
      }

//---------------------------------------------------------
//   refreshAll
//    Emit dataChanged for every row so QML delegates re-read
//    current property values.  Called after a drag operation
//    ends (ZCam::elementDragEnded) to update the inspector with
//    the final transform values that were suppressed during the drag.
//---------------------------------------------------------

void InspectorModel::refreshAll() {
      if (_propertyNames.isEmpty())
            return;
      // Suppress writeback while QML processes the dataChanged signal.
      // SpinBox delegates round to "decimals" digits, so their onValueChanged
      // may fire with a value that differs from the raw property value.
      // Without this guard, that would call setData() → changeProperty(),
      // creating a spurious undo command.
      _suppressWriteback = true;
      QModelIndex first  = index(0, 0);
      QModelIndex last   = index(_propertyNames.size() - 1, 0);
      emit dataChanged(first, last, {PropValueRole, SubValuesRole, ColumnItemsRole});
      // Clear the flag on the next event-loop iteration so that
      // subsequent user edits are processed normally.
      QMetaObject::invokeMethod(this, [this]() { _suppressWriteback = false; }, Qt::QueuedConnection);
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
            // Connect to ZCam::elementDragEnded so the inspector refreshes
            // after a drag operation.  During the drag, propertyChangedSlot()
            // suppresses dataChanged to prevent a binding loop that would
            // create spurious undo commands.  After the drag ends, we need
            // to refresh all displayed values once.
            ZCam* zc = _element->zcamInstance();
            if (zc) {
                  _dragEndedConnection =
                      QObject::connect(zc, &ZCam::elementDragEnded, [this]() { refreshAll(); });
                  // Connect to the undo stack's undoChanged signal so the
                  // inspector refreshes after undo/redo.  During undo/redo,
                  // propertyChangedSlot() would schedule deferred dataChanged
                  // signals, but since inUndoRedo is cleared before the
                  // deferred signal fires, the resulting QML binding
                  // re-evaluation would call setData() → changeProperty(),
                  // creating a spurious undo command and destroying the
                  // redo stack.  By connecting to undoChanged and calling
                  // refreshAll() (which sets _suppressWriteback), the
                  // deferred dataChanged from propertyChangedSlot() is
                  // suppressed (because _suppressWriteback is checked there
                  // too), and instead a single refreshAll() updates the
                  // display safely.
                  if (zc->project() && zc->project()->undo()) {
                        _undoChangedConnection = QObject::connect(
                            zc->project()->undo(), &UndoStack::undoChanged, [this]() { refreshAll(); });
                        }
                  }
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
      if (_dragEndedConnection) {
            QObject::disconnect(_dragEndedConnection);
            _dragEndedConnection = QMetaObject::Connection();
            }
      if (_undoChangedConnection) {
            QObject::disconnect(_undoChangedConnection);
            _undoChangedConnection = QMetaObject::Connection();
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
            // Skip "empty" entries (new format)
            if (_propertyNames[i] == "empty")
                  continue;
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
                                    auto conn                   = QMetaObject::connect(
                                        _element, signalIdx, this, slotIdx, Qt::AutoConnection);
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
                              auto conn                   = QMetaObject::connect(
                                  _element, signalIdx, this, slotIdx, Qt::AutoConnection);
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
      //
      // However, when the element is being dragged on the 3D canvas,
      // the property is updated directly by ZCam::dragged()/rotated()/
      // scaled() and the undo command is created once in endElementDrag().
      // We must NOT emit dataChanged here, because even though the
      // emission is deferred, it fires after endElementDrag() has
      // cleared the drag state — at which point the setData() guard
      // no longer applies. The deferred dataChanged would trigger QML
      // to re-evaluate bindings and call setData() → changeProperty(),
      // creating a spurious undo command that corrupts the undo history.
      if (isElementBeingDragged())
            return;
      // When _suppressWriteback is set (by refreshAll() during drag
      // end or undo/redo), suppress the deferred dataChanged as well.
      // refreshAll() will emit a single dataChanged for all rows with
      // _suppressWriteback active, so individual deferred emissions
      // from propertyChangedSlot() are unnecessary and would fire
      // after _suppressWriteback has been cleared, causing spurious
      // writeback.
      if (_suppressWriteback)
            return;
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

                  // New format: top-level "rows" array of row objects.
                  // Each row has "label" (optional), "cells" array, and
                  // optional "columns" (int, default 1).
                  // Each cell has "name" (optional for empty), "sublabel" (optional),
                  // "type", and other metadata (min, max, default, etc.).
                  // A cell with type "empty" takes space but renders nothing.
                  // A cell with type "line" renders as a separator.
                  // A cell with no "name" is treated as empty.
                  // A row can be empty "{}" and just takes space.
                  if (key == "rows" && jval.is_array()) {
                        for (const auto& row : jval) {
                              // Check if the row has a "columns" setting
                              int rowColumns = row.contains("columns") && row["columns"].is_number_integer()
                                                   ? row["columns"].get<int>()
                                                   : 1;

                              if (rowColumns > 1) {
                                    // Multi-column row: treat like a columns block
                                    // but parse cells instead of legacy items
                                    QList<ColumnItem> cols;
                                    // Each cell becomes a column item
                                    if (row.contains("cells") && row["cells"].is_array()) {
                                          for (const auto& cell : row["cells"]) {
                                                ColumnItem ci;
                                                ci.colSpan = cell.contains("colSpan") &&
                                                                     cell["colSpan"].is_number_integer()
                                                                 ? cell["colSpan"].get<int>()
                                                                 : 1;
                                                std::string type =
                                                    cell.contains("type") && cell["type"].is_string()
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
                                                      // Row cell: has sub-cells instead of a name
                                                      ci.isRow = true;
                                                      ci.name  = "row";
                                                      for (const auto& subCell : cell["cells"]) {
                                                            std::string subType =
                                                                subCell.contains("type") &&
                                                                        subCell["type"].is_string()
                                                                    ? subCell["type"].get<std::string>()
                                                                    : "";
                                                            if (subType == "line" ||
                                                                !subCell.contains("name"))
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
                                                      ci.isLine  = false;
                                                      ci.name    = "empty";
                                                      }
                                                else {
                                                      ci.name = QString::fromStdString(
                                                          cell["name"].get<std::string>());
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
                                    QString lineLabel;
                                    for (const auto& cell : row["cells"]) {
                                          std::string type = cell.contains("type") && cell["type"].is_string()
                                                                 ? cell["type"].get<std::string>()
                                                                 : "";
                                          if (type == "line") {
                                                hasLine = true;
                                                if (cell.contains("label") && cell["label"].is_string())
                                                      lineLabel = QString::fromStdString(
                                                          cell["label"].get<std::string>());
                                                continue;
                                                }
                                          if (type == "empty") {
                                                subs.append("empty");
                                                continue;
                                                }
                                          if (!cell.contains("name"))
                                                continue;
                                          subs.append(
                                              QString::fromStdString(cell["name"].get<std::string>()));
                                          }
                                    if (!subs.isEmpty()) {
                                          _propertyNames.append("row");
                                          _propertyIsRow.append(true);
                                          _propertyIsColumns.append(false);
                                          _columnCounts.append(0);
                                          _columnItems.append(QList<ColumnItem> {});
                                          _subPropNames.append(subs);
                                          QString rowLabel;
                                          if (row.contains("label") && row["label"].is_string())
                                                rowLabel =
                                                    QString::fromStdString(row["label"].get<std::string>());
                                          _rowLabels.append(rowLabel);
                                          }
                                    else if (hasLine) {
                                          // Row with only "line" cells → separator
                                          _propertyNames.append("line");
                                          _propertyIsRow.append(false);
                                          _propertyIsColumns.append(false);
                                          _columnCounts.append(0);
                                          _columnItems.append(QList<ColumnItem> {});
                                          _subPropNames.append(QStringList {});
                                          _rowLabels.append(lineLabel);
                                          }
                                    else {
                                          // Empty row (no named cells) - add as empty entry
                                          _propertyNames.append("empty");
                                          _propertyIsRow.append(false);
                                          _propertyIsColumns.append(false);
                                          _columnCounts.append(0);
                                          _columnItems.append(QList<ColumnItem> {});
                                          _subPropNames.append(QStringList {});
                                          _rowLabels.append(QString());
                                          }
                                    }
                              else {
                                    // Empty row "{}"
                                    _propertyNames.append("empty");
                                    _propertyIsRow.append(false);
                                    _propertyIsColumns.append(false);
                                    _columnCounts.append(0);
                                    _columnItems.append(QList<ColumnItem> {});
                                    _subPropNames.append(QStringList {});
                                    _rowLabels.append(QString());
                                    }
                              }
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
            case ScriptBoundRole: return isScriptBound(name);
            case SubScriptBoundRole: {
                  QVariantList list;
                  for (const QString& s : _subPropNames[index.row()])
                        list.append(isScriptBound(s));
                  return list;
                  }
            case ScriptTextRole: return scriptFor(name, -1);
            case ScriptErrorRole: return scriptError(name, -1);
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
                              m["isEmpty"]  = ci.isEmpty;
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

      // Suppress write-back while the element is being dragged on
      // the 3D canvas.  See isElementBeingDragged() for details.
      if (isElementBeingDragged())
            return false;

      // Suppress writeback during display refresh (e.g. after a drag
      // or undo/redo).  See _suppressWriteback for details.
      if (_suppressWriteback)
            return false;

      // For row entries, setData is not used directly; sub-properties
      // are written via setSubProperty().
      if (_propertyIsRow[index.row()])
            return false;

      // Script-bound properties are read-only in the inspector.
      // However, for vector properties with only some components bound
      // (e.g. size.x bound but size.y free), we allow the write but
      // preserve the bound components from the current value.
      const QString& propName = _propertyNames[index.row()];
      QVariant effectiveValue;
      if (!mergeBoundComponents(propName, value, effectiveValue))
            return false;

      // For empty entries, setData is not used.
      if (_propertyNames[index.row()] == "empty")
            return false;

      // For columns entries, setData is not used directly; properties
      // inside columns are written via setColumnProperty().
      if (_propertyIsColumns.value(index.row(), false))
            return false;

      const QString& name = _propertyNames[index.row()];
      QVariant oldValue   = _element->property(name.toUtf8().constData());
      if (oldValue == effectiveValue)
            return false;
      // Route through the Project undo system so that every
      // property edit is recorded for undo/redo and marks the project dirty.
      ZCam* zc    = nullptr;
      Element* el = qobject_cast<Element*>(_element);
      if (el)
            zc = el->zcamInstance();
      if (zc && zc->project()) {
            zc->project()->changeProperty(_element, name, effectiveValue);
            // changeProperty() triggers the element's NOTIFY signal,
            // which propertyChangedSlot() catches and defers a
            // dataChanged emission.  We must NOT emit dataChanged
            // here synchronously — that would re-enter QML bindings
            // and create a binding loop.
            return true;
            }
      else {
            if (_element->setProperty(name.toUtf8().constData(), effectiveValue)) {
                  QVariant newValue = _element->property(name.toUtf8().constData());
                  if (oldValue != newValue)
                        emit dataChanged(index, index, {role});
                  return true;
                  }
            return false;
            }
      }

//---------------------------------------------------------
//   mergeBoundComponents
//    Decide whether a user edit of property *propName* may be
//    written, given the script bindings of that property:
//      • no binding             → accept the value as-is
//      • scalar binding ("all") → block the write
//      • partial vector binding (e.g. "0") → accept the write but
//        keep the script-bound components from the current value
//        (the binding re-evaluates on the change and rewrites its
//        component; accepting the free components lets the user
//        edit e.g. size.y while size.x is bound to "size.y * 0.5").
//    Returns false when the write must be blocked or is a no-op.
//---------------------------------------------------------

bool InspectorModel::mergeBoundComponents(
    const QString& propName, const QVariant& value, QVariant& mergedValue) const {
      QString comps = boundComponents(propName);
      if (comps.isEmpty()) {
            mergedValue = value;
            return true;
            }
      if (comps == QStringLiteral("all"))
            return false;

      // Partial binding: only vector properties can proceed.
      QVariant curVal = _element->property(propName.toUtf8().constData());
      QStringList cl  = comps.split(u',');
      if (curVal.metaType().id() == QMetaType::QVector2D && value.metaType().id() == QMetaType::QVector2D) {
            QVector2D cur = curVal.value<QVector2D>();
            QVector2D neu = value.value<QVector2D>();
            if (cl.contains(QStringLiteral("0")))
                  neu.setX(cur.x());
            if (cl.contains(QStringLiteral("1")))
                  neu.setY(cur.y());
            mergedValue = QVariant::fromValue(neu);
            }
      else if (curVal.metaType().id() == QMetaType::QVector3D &&
               value.metaType().id() == QMetaType::QVector3D) {
            QVector3D cur = curVal.value<QVector3D>();
            QVector3D neu = value.value<QVector3D>();
            if (cl.contains(QStringLiteral("0")))
                  neu.setX(cur.x());
            if (cl.contains(QStringLiteral("1")))
                  neu.setY(cur.y());
            if (cl.contains(QStringLiteral("2")))
                  neu.setZ(cur.z());
            mergedValue = QVariant::fromValue(neu);
            }
      else
            // Non-vector partial binding: block.
            return false;
      return mergedValue != curVal;
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

      // Suppress write-back while the element is being dragged on
      // the 3D canvas.  See isElementBeingDragged() for details.
      if (isElementBeingDragged())
            return false;

      // Suppress writeback during display refresh.
      if (_suppressWriteback)
            return false;

      // Script-bound properties are read-only in the inspector.
      // For vector properties with only some components bound, allow
      // the write but keep the bound components from the current value.
      QVariant effectiveValue;
      if (!mergeBoundComponents(subName, value, effectiveValue))
            return false;

      QVariant oldValue = _element->property(subName.toUtf8().constData());
      if (oldValue == effectiveValue)
            return false;

      ZCam* zc    = nullptr;
      Element* el = qobject_cast<Element*>(_element);
      if (el)
            zc = el->zcamInstance();
      if (zc && zc->project()) {
            zc->project()->changeProperty(_element, subName, effectiveValue);
            return true;
            }
      else {
            if (_element->setProperty(subName.toUtf8().constData(), effectiveValue)) {
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
//   elementProperty
//    Read any property value from the current element by name.
//    Used from QML delegates that need to access a property
//    other than the one they are editing (e.g. the multiline
//    sub-delegate needs the "align" property).
//---------------------------------------------------------

QVariant InspectorModel::elementProperty(const QString& name) const {
      if (!_element)
            return {};
      return _element->property(name.toUtf8().constData());
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

      // Suppress write-back while the element is being dragged on
      // the 3D canvas.  See isElementBeingDragged() for details.
      if (isElementBeingDragged())
            return false;

      // Suppress writeback during display refresh.
      if (_suppressWriteback)
            return false;

      // Script-bound properties are read-only in the inspector.
      // For vector properties with only some components bound, allow
      // the write but keep the bound components from the current value.
      QVariant effectiveValue;
      if (!mergeBoundComponents(propName, value, effectiveValue))
            return false;

      QVariant oldValue = _element->property(propName.toUtf8().constData());
      if (oldValue == effectiveValue)
            return false;

      ZCam* zc    = nullptr;
      Element* el = qobject_cast<Element*>(_element);
      if (el)
            zc = el->zcamInstance();
      if (zc && zc->project()) {
            zc->project()->changeProperty(_element, propName, effectiveValue);
            return true;
            }
      else {
            if (_element->setProperty(propName.toUtf8().constData(), effectiveValue)) {
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
      roles[PropNameRole]       = "propName";
      roles[PropValueRole]      = "propValue";
      roles[IsRowRole]          = "isRow";
      roles[SubPropsRole]       = "subProps";
      roles[SubValuesRole]      = "subValues";
      roles[RowLabelRole]       = "rowLabel";
      roles[IsColumnsRole]      = "isColumns";
      roles[ColumnCountRole]    = "columnCount";
      roles[ColumnItemsRole]    = "columnItems";
      roles[ScriptBoundRole]    = "scriptBound";
      roles[SubScriptBoundRole] = "subScriptBound";
      roles[ScriptTextRole]     = "scriptText";
      roles[ScriptErrorRole]    = "scriptError";
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

QString InspectorModel::layerToName(QVariant layer) const {
      Group* ptr = layer.value<Group*>();
      if (!ptr)
            return {};
      return ptr->name();
      }

//---------------------------------------------------------
//   nameToLayer
//    Resolve a name string back to a Layer* pointer.
//---------------------------------------------------------

Group* InspectorModel::nameToLayer(const QString& name) const {
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
//   laserLayerNames
//    Return all LaserLayer names in the current project.
//---------------------------------------------------------

QStringList InspectorModel::laserLayerNames() const {
      if (!_element)
            return {};
      ZCam* zc    = nullptr;
      Element* el = qobject_cast<Element*>(_element);
      if (el)
            zc = el->zcamInstance();
      if (!zc)
            return {};
      return zc->laserLayerNames();
      }

//---------------------------------------------------------
//   laserLayerToName
//    Resolve a LaserLayer* pointer to its name string.
//---------------------------------------------------------

QString InspectorModel::laserLayerToName(QVariant ll) const {
      LaserMop* ptr = ll.value<LaserMop*>();
      if (!ptr)
            return {};
      return ptr->name();
      }

//---------------------------------------------------------
//   nameToLaserLayer
//    Resolve a name string back to a LaserLayer* pointer.
//---------------------------------------------------------

LaserMop* InspectorModel::nameToLaserLayer(const QString& name) const {
      if (!_element || name.isEmpty())
            return nullptr;
      ZCam* zc    = nullptr;
      Element* el = qobject_cast<Element*>(_element);
      if (el)
            zc = el->zcamInstance();
      if (!zc)
            return nullptr;
      return zc->laserLayerPtr(name);
      }

//---------------------------------------------------------
//   recipeToName
//    Resolve a Recipe* pointer to its name string.
//---------------------------------------------------------

QString InspectorModel::recipeToName(QVariant recipe) const {
      LaserRecipe* ptr = recipe.value<LaserRecipe*>();
      if (!ptr)
            return {};
      return ptr->name();
      }

//---------------------------------------------------------
//   nameToRecipe
//    Resolve a name string back to a Recipe* pointer.
//---------------------------------------------------------

LaserRecipe* InspectorModel::nameToRecipe(const QString& name) const {
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
//    Return the list of ParameterType names from laser.h
//    as strings. The order matches the enum values so the
//    QML ComboBox can use the index directly as the enum value.
//---------------------------------------------------------

QStringList InspectorModel::overrideTypeNames() const {
      return {
         QStringLiteral("None"), QStringLiteral("Speed"), QStringLiteral("Power"), QStringLiteral("Interval"),
         QStringLiteral("Frequency"), QStringLiteral("Count"), QStringLiteral("Pulse")};
      }

//---------------------------------------------------------
//   pulsewidthNames
//    Return the list of pulse width values from
//    Laser::pulseTable() as strings for use in a QML ComboBox.
//---------------------------------------------------------

QStringList InspectorModel::pulsewidthNames() const {
      if (!_element)
            return {};
      ZCam* zc    = nullptr;
      Element* el = qobject_cast<Element*>(_element);
      if (el)
            zc = el->zcamInstance();
      if (!zc || !zc->project() || !zc->project()->machine())
            return {};
      // The machine may be a Laser subclass (which provides laserPulseList)
      // or a non-laser Machine (which does not).
      auto* laser = qobject_cast<Laser*>(zc->project()->machine());
      if (!laser)
            return {};
      QStringList sl;
      for (const auto& p : laser->laserPulseList())
            sl << p;
      return sl;
      }

//---------------------------------------------------------
//   joinTypeNames
//    Return the list of Clipper2Lib::JoinType names.
//    The order matches the enum values so the QML ComboBox
//    can use the index directly as the enum value.
//---------------------------------------------------------

QStringList InspectorModel::joinTypeNames() const {
      return {
         QStringLiteral("Square"), QStringLiteral("Bevel"), QStringLiteral("Round"), QStringLiteral("Miter")};
      }

//---------------------------------------------------------
//   endTypeNames
//    Return the list of Clipper2Lib::EndType names.
//    The order matches the enum values so the QML ComboBox
//    can use the index directly as the enum value.
//---------------------------------------------------------

QStringList InspectorModel::endTypeNames() const {
      return {
         QStringLiteral("Polygon"), QStringLiteral("Joined"), QStringLiteral("Butt"),
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

QString InspectorModel::machineToName(QVariant machine) const {
      Machine* ptr = machine.value<Machine*>();
      if (!ptr)
            return {};
      return ptr->name();
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

//---------------------------------------------------------
//   cameraNames
//    Return the descriptions of all available video input
//    devices (Linux webcams) for the "cameraName" combobox.
//---------------------------------------------------------

QStringList InspectorModel::cameraNames() const {
      QStringList names;
      const auto inputs = QMediaDevices::videoInputs();
      for (const auto& dev : inputs)
            names << dev.description();
      return names;
      }

//---------------------------------------------------------
//   isScriptBound
//---------------------------------------------------------

bool InspectorModel::isScriptBound(const QString& propName) const {
      if (!_element)
            return false;
      return !boundComponents(propName).isEmpty();
      }

//---------------------------------------------------------
//   boundComponents
//---------------------------------------------------------

QString InspectorModel::boundComponents(const QString& propName) const {
      if (!_element)
            return {};
      ScriptEngine* se =
          _element->zcamInstance() ? _element->zcamInstance()->scriptEngine() : ScriptEngine::instance();
      if (!se)
            return {};
      return se->boundComponentsQml(_element, propName);
      }

//---------------------------------------------------------
//   scriptFor
//---------------------------------------------------------

QString InspectorModel::scriptFor(const QString& propName, int comp) const {
      if (!_element)
            return {};
      ScriptEngine* se =
          _element->zcamInstance() ? _element->zcamInstance()->scriptEngine() : ScriptEngine::instance();
      if (!se)
            return {};
      return se->scriptForQml(_element, propName, comp);
      }

//---------------------------------------------------------
//   scriptError
//---------------------------------------------------------

QString InspectorModel::scriptError(const QString& propName, int comp) const {
      if (!_element)
            return {};
      ScriptEngine* se =
          _element->zcamInstance() ? _element->zcamInstance()->scriptEngine() : ScriptEngine::instance();
      if (!se)
            return {};
      return se->scriptErrorQml(_element, propName, comp);
      }

//---------------------------------------------------------
//   setScript
//---------------------------------------------------------

void InspectorModel::setScript(const QString& propName, int comp, const QString& script) {
      if (!_element)
            return;
      ScriptEngine* se =
          _element->zcamInstance() ? _element->zcamInstance()->scriptEngine() : ScriptEngine::instance();
      if (!se)
            return;

      // Determine the old script text so the undo command can
      // restore it.  For a scalar binding (comp < 0) the old
      // text is stored in _script; for a component binding it
      // is in _scriptComp[comp].
      QString oldScript;
      if (comp < 0)
            oldScript = _element->scriptProp() == propName ? _element->script() : QString();
      else
            oldScript = _element->scriptCompProp(comp) == propName ? _element->scriptComp(comp) : QString();

      // Route through the project undo stack so the change is
      // recorded and the project is marked dirty.  This ensures
      // that script bindings are saved when the user saves the
      // project, and that the unsaved-changes dialog appears
      // when the user quits without saving.
      Project* proj = _element->zcamInstance() ? _element->zcamInstance()->project() : nullptr;
      if (proj && proj->undo()) {
            proj->undo()->beginMacro();
            proj->undo()->push(new ScriptBindingCommand(
                _element->zcamInstance(), _element, propName, comp, oldScript, script));
            proj->undo()->endMacro();
            }
      else
            se->createBindingQml(_element, propName, comp, script);
      refreshAll();
      }

//---------------------------------------------------------
//   testScript
//---------------------------------------------------------

QVariant InspectorModel::testScript(const QString& script) const {
      ScriptEngine* se = ScriptEngine::instance();
      if (!se)
            return {};
      return se->testScript(script);
      }

//--------------------------------------------------------------------
//     InspectorModel::testScriptWithContext
//--------------------------------------------------------------------

QVariant InspectorModel::testScriptWithContext(const QString& script) const {
      ScriptEngine* se = ScriptEngine::instance();
      if (!se)
            return {};
      if (!_element)
            return se->testScript(script);
      return se->testScriptWithContext(script, _element);
      }

//---------------------------------------------------------
//   removeScript
//---------------------------------------------------------

void InspectorModel::removeScript(const QString& propName) {
      if (!_element)
            return;
      ScriptEngine* se =
          _element->zcamInstance() ? _element->zcamInstance()->scriptEngine() : ScriptEngine::instance();
      if (!se)
            return;

      // Collect all bindings (scalar + component) for this property
      // so the undo command can restore them.
      // For a scalar binding (comp < 0) the script text is in
      // _script; for component bindings it is in _scriptComp[comp].
      Project* proj = _element->zcamInstance() ? _element->zcamInstance()->project() : nullptr;
      if (proj && proj->undo()) {
            proj->undo()->beginMacro();
            // Scalar binding
            if (_element->scriptProp() == propName && !_element->script().isEmpty())
                  proj->undo()->push(new ScriptBindingCommand(
                      _element->zcamInstance(), _element, propName, -1,
                      _element->script(), QString()));
            // Component bindings (x/y/z)
            for (int comp = 0; comp < 3; ++comp) {
                  if (_element->scriptCompProp(comp) == propName && !_element->scriptComp(comp).isEmpty())
                        proj->undo()->push(new ScriptBindingCommand(
                            _element->zcamInstance(), _element, propName, comp,
                            _element->scriptComp(comp), QString()));
                  }
            proj->undo()->endMacro();
            }
      else
            se->removeBindingQml(_element, propName);
      refreshAll();
      }

//--------------------------------------------------------------------
//     InspectorModel::setScriptActive / isScriptActive
//--------------------------------------------------------------------

void InspectorModel::setScriptActive(const QString& propName, bool active) {
      if (!_element)
            return;
      ScriptEngine* se =
          _element->zcamInstance() ? _element->zcamInstance()->scriptEngine() : ScriptEngine::instance();
      if (!se)
            return;
      se->setBindingActive(_element, propName, active);
      // Mark project dirty so the active state is saved.
      Project* proj = _element->zcamInstance() ? _element->zcamInstance()->project() : nullptr;
      if (proj && proj->undo())
            proj->undo()->markDirty();
      refreshAll();
      }

bool InspectorModel::isScriptActive(const QString& propName) const {
      if (!_element)
            return false;
      ScriptEngine* se =
          _element->zcamInstance() ? _element->zcamInstance()->scriptEngine() : ScriptEngine::instance();
      if (!se)
            return false;
      return se->isBindingActive(_element, propName);
      }
