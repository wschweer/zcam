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

#include "propertyjson.h"
#include "logger.h"
namespace propjson {

//---------------------------------------------------------
//   collectPropertyNames
//    Parse the "items" array and extract (name, type) pairs.
//    Handles "row" entries (with sub-properties) and individual
//    named properties.  Entries with type "line" are skipped.
//---------------------------------------------------------

static void collectPropertyNames(const nlohmann::json& items, PropNameList& out) {
      if (!items.is_array())
            return;
      for (const auto& item : items) {
            if (item.contains("columns") && item["columns"].is_object()) {
                  // Recursively parse the items inside the columns block
                  if (item["columns"].contains("items") && item["columns"]["items"].is_array())
                        collectPropertyNames(item["columns"]["items"], out);
                  }
            else if (item.contains("row") && item["row"].is_object()) {
                  for (auto it = item["row"].begin(); it != item["row"].end(); ++it) {
                        std::string type =
                            it.value().contains("type") ? it.value().at("type").get<std::string>() : "";
                        if (type == "line" || type == "empty")
                              continue;
                        out.emplace_back(it.key(), type);
                        }
                  }
            else if (item.contains("name") && item.contains("type")) {
                  std::string name = item["name"].get<std::string>();
                  std::string type = item["type"].get<std::string>();
                  if (type == "line" || type == "empty")
                        continue;
                  out.emplace_back(name, type);
                  }
            }
      }

//---------------------------------------------------------
//   jsonGetString
//---------------------------------------------------------

static std::string jsonGetString(const nlohmann::json& obj, const std::string& key) {
      if (!obj.contains(key) || !obj.at(key).is_string())
            return "";
      return obj.at(key).get<std::string>();
      }

//---------------------------------------------------------
//   parseAllPropertyNames
//---------------------------------------------------------

PropNameList parseAllPropertyNames(std::string_view propStr) {
      PropNameList propNames;
      nlohmann::json j = nlohmann::json::parse(propStr);

      if (j.contains("items"))
            collectPropertyNames(j["items"], propNames);

      // Also handle old-style top-level keys (non-"items" entries)
      for (auto it = j.begin(); it != j.end(); ++it) {
            if (it.key() == "class" || it.key() == "items")
                  continue;
            const auto& val = it.value();
            if (!val.is_object())
                  continue;
            bool isRow = true;
            for (auto sub = val.begin(); sub != val.end(); ++sub) {
                  if (!sub.value().contains("label")) {
                        isRow = false;
                        break;
                        }
                  }
            if (isRow) {
                  for (auto sub = val.begin(); sub != val.end(); ++sub) {
                        std::string type = jsonGetString(sub.value(), "type");
                        if (type == "line" || type == "empty")
                              continue;
                        propNames.emplace_back(sub.key(), type);
                        }
                  }
            else if (val.contains("type")) {
                  propNames.emplace_back(it.key(), jsonGetString(val, "type"));
                  }
            }

      return propNames;
      }

//---------------------------------------------------------
//   readPropertyRaw
//    Read a property from obj using the Qt meta-object system,
//    supporting both QObject (read) and Q_GADGET (readOnGadget).
//---------------------------------------------------------

static QVariant readPropertyRaw(const void* obj, const QMetaObject* meta, bool gadget, int idx) {
      QMetaProperty mp = meta->property(idx);
      if (gadget)
            return mp.readOnGadget(obj);
      return mp.read(static_cast<const QObject*>(obj));
      }

//---------------------------------------------------------
//   writePropertyRaw
//    Write a property to obj using the Qt meta-object system,
//    supporting both QObject (write) and Q_GADGET (writeOnGadget).
//---------------------------------------------------------

static bool writePropertyRaw(void* obj, const QMetaObject* meta, bool gadget, int idx,
                             const QVariant& value) {
      QMetaProperty mp = meta->property(idx);
      if (gadget)
            return mp.writeOnGadget(obj, value);
      return mp.write(static_cast<QObject*>(obj), value);
      }

//---------------------------------------------------------
//   writePropertyToJson
//---------------------------------------------------------

bool writePropertyToJson(nlohmann::json& data, const void* obj, const QMetaObject* meta, bool gadget,
                         const std::string& name, const std::string& type) {
      QByteArray propName = QByteArray::fromStdString(name);
      int idx             = meta->indexOfProperty(propName.constData());
      if (idx < 0)
            return false;
      QMetaProperty mp = meta->property(idx);
      QVariant value   = readPropertyRaw(obj, meta, gadget, idx);

      if (type == "vector3d") {
            QVector3D v        = value.value<QVector3D>();
            nlohmann::json arr = nlohmann::json::array();
            arr.push_back(v.x());
            arr.push_back(v.y());
            arr.push_back(v.z());
            data[name] = arr;
            }
      else if (type == "vector2d") {
            QVector2D v        = value.value<QVector2D>();
            nlohmann::json arr = nlohmann::json::array();
            arr.push_back(v.x());
            arr.push_back(v.y());
            data[name] = arr;
            }
      else if (type == "color") {
            QColor c = value.value<QColor>();
            nlohmann::json j;
            j["r"]     = c.redF();
            j["g"]     = c.greenF();
            j["b"]     = c.blueF();
            data[name] = j;
            }
      else if (type == "bool") {
            data[name] = value.toBool();
            }
      else if (type == "int" || type == "halign" || type == "lockScale" || type == "lockSize") {
            int tid = static_cast<QMetaType::Type>(value.typeId());
            if (tid == QMetaType::Double || tid == QMetaType::Float)
                  data[name] = value.toDouble();
            else
                  data[name] = value.toInt();
            }
      else if (type == "float") {
            int tid = static_cast<QMetaType::Type>(value.typeId());
            if (tid == QMetaType::Int || tid == QMetaType::Short || tid == QMetaType::Long ||
                tid == QMetaType::LongLong || tid == QMetaType::UShort || tid == QMetaType::UInt ||
                tid == QMetaType::ULong || tid == QMetaType::ULongLong)
                  data[name] = value.toInt();
            else
                  data[name] = value.toDouble();
            }
      else {
            // Fallback: store as string
            if (value.canConvert<QString>())
                  data[name] = value.toString().toStdString();
            else
                  data[name] = nullptr;
            }
      return true;
      }

//---------------------------------------------------------
//   readPropertyFromJson
//---------------------------------------------------------

bool readPropertyFromJson(const nlohmann::json& data, void* obj, const QMetaObject* meta, bool gadget,
                          const std::string& name, const std::string& type) {
      if (!data.contains(name))
            return false;
      const nlohmann::json& jval = data.at(name);
      QByteArray propName        = QByteArray::fromStdString(name);
      int idx                    = meta->indexOfProperty(propName.constData());
      if (idx < 0)
            return false;
      QMetaProperty mp = meta->property(idx);

      if (type == "vector3d") {
            QVector3D v;
            v.setX(jval.at(0).get<double>());
            v.setY(jval.at(1).get<double>());
            v.setZ(jval.at(2).get<double>());
            return writePropertyRaw(obj, meta, gadget, idx, QVariant::fromValue(v));
            }
      else if (type == "vector2d") {
            QVector2D v;
            v.setX(jval.at(0).get<double>());
            v.setY(jval.at(1).get<double>());
            return writePropertyRaw(obj, meta, gadget, idx, QVariant::fromValue(v));
            }
      else if (type == "color") {
            QColor c;
            c.setRedF(jval.value("r", 0.0));
            c.setGreenF(jval.value("g", 0.0));
            c.setBlueF(jval.value("b", 0.0));
            return writePropertyRaw(obj, meta, gadget, idx, QVariant::fromValue(c));
            }
      else if (type == "bool") {
            return writePropertyRaw(obj, meta, gadget, idx, QVariant(jval.get<bool>()));
            }
      else if (type == "int" || type == "halign" || type == "lockScale" || type == "lockSize") {
            int tid = mp.metaType().id();
            if (tid == QMetaType::Double || tid == QMetaType::Float)
                  return writePropertyRaw(obj, meta, gadget, idx, QVariant(jval.get<double>()));
            return writePropertyRaw(obj, meta, gadget, idx, QVariant(jval.get<int>()));
            }
      else if (type == "float") {
            int tid = mp.metaType().id();
            if (tid == QMetaType::Int || tid == QMetaType::Short || tid == QMetaType::Long ||
                tid == QMetaType::LongLong || tid == QMetaType::UShort || tid == QMetaType::UInt ||
                tid == QMetaType::ULong || tid == QMetaType::ULongLong)
                  return writePropertyRaw(obj, meta, gadget, idx, QVariant(jval.get<int>()));
            return writePropertyRaw(obj, meta, gadget, idx, QVariant(jval.get<double>()));
            }
      else {
            // Fallback: string types
            return writePropertyRaw(obj, meta, gadget, idx,
                                    QVariant(QString::fromStdString(jval.get<std::string>())));
            }
      }

      } // namespace propjson