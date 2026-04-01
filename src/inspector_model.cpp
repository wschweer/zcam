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
      _element = element;
      emit elementChanged();
      parseProperties();
      }

//---------------------------------------------------------
//   inferTypeFromMeta
//    Determine the property type string from QMetaObject
//    when the JSON does not supply an explicit "type".
//---------------------------------------------------------

static QString inferTypeFromMeta(const QMetaObject* meta, const char* propName) {
      int idx = meta->indexOfProperty(propName);
      if (idx == -1)
            return QStringLiteral("string");
      QMetaProperty mp = meta->property(idx);
      switch (mp.typeId()) {
            case QMetaType::Double:
            case QMetaType::Float: return QStringLiteral("float");
            case QMetaType::Int:
            case QMetaType::UInt:
            case QMetaType::Long:
            case QMetaType::LongLong: return QStringLiteral("int");
            case QMetaType::Bool: return QStringLiteral("bool");
            default: return QStringLiteral("string");
            }
      }

//---------------------------------------------------------
//   parseProperties
//---------------------------------------------------------

void InspectorModel::parseProperties() {
      beginResetModel();
      _properties.clear();
      _title.clear();

      if (!_element) {
            endResetModel();
            emit titleChanged();
            return;
            }

      std::string_view propStr;
      if (qobject_cast<Text*>(_element)) {
            propStr = Text::getProperties();
            }
      else {
            // No inspector definition for this element type; show just the name.
            _title = _element->name();
            endResetModel();
            emit titleChanged();
            return;
            }

      try {
            nlohmann::ordered_json j = nlohmann::ordered_json::parse(propStr);

            // Build title from "class" + element name
            if (j.contains("class") && j["class"].is_string())
                  _title = QString::fromStdString(j["class"].get<std::string>()) + QStringLiteral(" – ") + _element->name();
            else
                  _title = _element->name();

            const QMetaObject* meta = _element->metaObject();

            for (auto it = j.begin(); it != j.end(); ++it) {
                  const std::string& key = it.key();
                  if (key == "class")
                        continue;

                  const auto& jval = it.value();

                  // Skip entries that don't carry a label – they are not meant to be shown.
                  if (!jval.contains("label"))
                        continue;

                  PropEntry entry;
                  entry.name  = QString::fromStdString(key);
                  entry.label = QString::fromStdString(jval["label"].get<std::string>());

                  // Resolve type
                  if (jval.contains("type") && jval["type"].is_string())
                        entry.type = QString::fromStdString(jval["type"].get<std::string>());
                  else
                        entry.type = inferTypeFromMeta(meta, key.c_str());

                  // Optional unit
                  if (jval.contains("unit") && jval["unit"].is_string())
                        entry.unit = QString::fromStdString(jval["unit"].get<std::string>());

                  // Optional range
                  if (jval.contains("min"))
                        entry.minVal = jval["min"].get<double>();
                  if (jval.contains("max"))
                        entry.maxVal = jval["max"].get<double>();

                  _properties.append(entry);
                  }
            }
      catch (...) {
            // Silently ignore JSON parse errors
            }

      endResetModel();
      emit titleChanged();
      }

//---------------------------------------------------------
//   rowCount
//---------------------------------------------------------

int InspectorModel::rowCount(const QModelIndex& parent) const {
      if (parent.isValid())
            return 0;
      return static_cast<int>(_properties.size());
      }

//---------------------------------------------------------
//   data
//---------------------------------------------------------

QVariant InspectorModel::data(const QModelIndex& index, int role) const {
      if (!index.isValid() || index.row() >= static_cast<int>(_properties.size()))
            return {};

      const PropEntry& entry = _properties[index.row()];

      switch (role) {
            case PropNameRole: return entry.name;
            case PropLabelRole: return entry.label;
            case PropTypeRole: return entry.type;
            case PropValueRole:
                  if (_element)
                        return _element->property(entry.name.toUtf8().constData());
                  return {};
            case PropUnitRole: return entry.unit;
            case PropMinRole: return entry.minVal;
            case PropMaxRole: return entry.maxVal;
            default: return {};
            }
      }

//---------------------------------------------------------
//   setData
//---------------------------------------------------------

bool InspectorModel::setData(const QModelIndex& index, const QVariant& value, int role) {
      if (!index.isValid() || index.row() >= static_cast<int>(_properties.size()))
            return false;

      if (role == PropValueRole && _element) {
            const PropEntry& entry = _properties[index.row()];
            // Convert the incoming string value to the correct type before setting
            QVariant coerced = value;
            if (entry.type == QStringLiteral("float") || entry.type == QStringLiteral("int")) {
                  if (value.typeId() == QMetaType::QString) {
                        bool ok = false;
                        if (entry.type == QStringLiteral("float")) {
                              double d = value.toString().toDouble(&ok);
                              if (ok)
                                    coerced = d;
                              }
                        else {
                              int i = value.toString().toInt(&ok);
                              if (ok)
                                    coerced = i;
                              }
                        }
                  }
            if (_element->setProperty(entry.name.toUtf8().constData(), coerced)) {
                  emit dataChanged(index, index, {role});
                  return true;
                  }
            }
      return false;
      }

//---------------------------------------------------------
//   roleNames
//---------------------------------------------------------

QHash<int, QByteArray> InspectorModel::roleNames() const {
      QHash<int, QByteArray> roles;
      roles[PropNameRole]  = "propName";
      roles[PropLabelRole] = "propLabel";
      roles[PropTypeRole]  = "propType";
      roles[PropValueRole] = "propValue";
      roles[PropUnitRole]  = "propUnit";
      roles[PropMinRole]   = "propMin";
      roles[PropMaxRole]   = "propMax";
      return roles;
      }
