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
#include "machine.h"
#include "logger.h"
namespace propjson {

//---------------------------------------------------------
//   roundToPrecision
//    Round a floating point value to the given number of decimal
//    places when precision >= 0.  Used for serialisation so that
//    values declared with a specific precision in properties() are
//    stored with at least that many digits and are not silently
//    truncated by downstream formatting.
//---------------------------------------------------------

static double roundToPrecision(double value, int precision) {
      if (precision < 0)
            return value;
      const double factor = std::pow(10.0, precision);
      return std::round(value * factor) / factor;
      }

//---------------------------------------------------------
//   collectCellPropertyNames
//    Extract (name, type) pairs from a single cell.
//    Handles nested cells (row within columns).
//---------------------------------------------------------

static void collectCellPropertyNames(const nlohmann::json& cell, PropNameList& out) {
      // Check for nested cells (row within columns)
      if (cell.contains("cells") && cell["cells"].is_array()) {
            for (const auto& subCell : cell["cells"]) {
                  if (subCell.contains("name") && subCell.contains("type")) {
                        std::string name = subCell["name"].get<std::string>();
                        std::string type = subCell["type"].get<std::string>();
                        if (type == "line" || type == "empty")
                              continue;
                        out.emplace_back(name, type);
                        }
                  }
            }
      else if (cell.contains("name") && cell.contains("type")) {
            std::string name = cell["name"].get<std::string>();
            std::string type = cell["type"].get<std::string>();
            if (type != "line" && type != "empty")
                  out.emplace_back(name, type);
            }
      }

//---------------------------------------------------------
//   parseAllPropertyNames
//    Parse the properties() JSON definition and return a list of
//    (propertyName, type) pairs.
//    Uses the "rows"/"cells" format.
//---------------------------------------------------------

PropNameList parseAllPropertyNames(std::string_view propStr) {
      PropNameList propNames;
      nlohmann::json j = nlohmann::json::parse(propStr);

      if (j.contains("rows") && j["rows"].is_array()) {
            for (const auto& row : j["rows"]) {
                  if (row.contains("cells") && row["cells"].is_array())
                        for (const auto& cell : row["cells"])
                              collectCellPropertyNames(cell, propNames);
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

static bool writePropertyRaw(
    void* obj, const QMetaObject* meta, bool gadget, int idx, const QVariant& value) {
      QMetaProperty mp = meta->property(idx);
      if (gadget)
            return mp.writeOnGadget(obj, value);
      return mp.write(static_cast<QObject*>(obj), value);
      }

//---------------------------------------------------------
//   writePropertyToJson
//---------------------------------------------------------

bool writePropertyToJson(nlohmann::json& data, const void* obj, const QMetaObject* meta, bool gadget,
    const std::string& name, const std::string& type, int precision) {
      QByteArray propName = QByteArray::fromStdString(name);
      int idx             = meta->indexOfProperty(propName.constData());
      if (idx < 0)
            return false;
      QMetaProperty mp = meta->property(idx);
      QVariant value   = readPropertyRaw(obj, meta, gadget, idx);

      if (type == "vector3d" || type == "scale") {
            QVector3D v        = value.value<QVector3D>();
            nlohmann::json arr = nlohmann::json::array();
            arr.push_back(roundToPrecision(v.x(), precision));
            arr.push_back(roundToPrecision(v.y(), precision));
            arr.push_back(roundToPrecision(v.z(), precision));
            data[name] = arr;
            }
      else if (type == "vector2d" || type == "size") {
            QVector2D v        = value.value<QVector2D>();
            nlohmann::json arr = nlohmann::json::array();
            arr.push_back(roundToPrecision(v.x(), precision));
            arr.push_back(roundToPrecision(v.y(), precision));
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
      else if (type == "bool" || type == "fontStyle") {
            data[name] = value.toBool();
            }
      else if (type == "int" || type == "halign" || type == "lockScale" || type == "lockSize" ||
               type == "mopColor") {
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
                  data[name] = roundToPrecision(value.toDouble(), precision);
            }
      else if (type == "machineType") {
            // MachineType enum — serialize as its human-readable string name.
            // The property is a MachineType enum; we convert via the QMetaProperty
            // to int, then look up the name in machineTypeMap.
            int enumVal   = value.toInt();
            auto typeName = machineTypeMap.name(static_cast<MachineType>(enumVal));
            data[name]    = std::string(typeName);
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

      if (type == "vector3d" || type == "scale") {
            QVector3D v;
            v.setX(jval.at(0).get<double>());
            v.setY(jval.at(1).get<double>());
            v.setZ(jval.at(2).get<double>());
            return writePropertyRaw(obj, meta, gadget, idx, QVariant::fromValue(v));
            }
      else if (type == "vector2d" || type == "size") {
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
      else if (type == "bool" || type == "fontStyle") {
            return writePropertyRaw(obj, meta, gadget, idx, QVariant(jval.get<bool>()));
            }
      else if (type == "int" || type == "halign" || type == "lockScale" || type == "lockSize" ||
               type == "mopColor") {
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
      else if (type == "machineType") {
            // MachineType enum — deserialize from its human-readable string name.
            // Look up the name in machineTypeMap and write the enum value.
            std::string nameStr = jval.get<std::string>();
            auto mt             = machineTypeMap.type(std::string_view(nameStr));
            if (mt)
                  return writePropertyRaw(obj, meta, gadget, idx, QVariant(static_cast<int>(*mt)));
            return false;
            }
      else {
            // Fallback: string types
            return writePropertyRaw(
                obj, meta, gadget, idx, QVariant(QString::fromStdString(jval.get<std::string>())));
            }
      }

//---------------------------------------------------------
//   precisionForName
//    Walk the properties() JSON definition and return the declared
//    precision for the named property.  Handles the rows/cells format
//    including nested sub-cells.  Returns -1 if the property is not
//    found or has no precision field.
//---------------------------------------------------------

static void collectCellPrecision(const nlohmann::json& cell, const std::string& name, int& out) {
      if (out >= 0)
            return;
      if (cell.contains("name") && cell["name"].is_string() && cell["name"].get<std::string>() == name &&
          cell.contains("precision") && cell["precision"].is_number_integer()) {
            out = cell["precision"].get<int>();
            return;
            }
      if (cell.contains("cells") && cell["cells"].is_array())
            for (const auto& subCell : cell["cells"])
                  collectCellPrecision(subCell, name, out);
      }

int precisionForName(std::string_view propStr, const std::string& name) {
      int precision = -1;
      try {
            nlohmann::json j = nlohmann::json::parse(propStr);
            if (j.contains("rows") && j["rows"].is_array()) {
                  for (const auto& row : j["rows"]) {
                        if (row.contains("cells") && row["cells"].is_array())
                              for (const auto& cell : row["cells"])
                                    collectCellPrecision(cell, name, precision);
                        }
                  }
            }
      catch (const nlohmann::json::parse_error& err) {
            Warning("precisionForName: JSON parse error: {}", err.what());
            }
      return precision;
      }

//---------------------------------------------------------
//   collectCellDefaultScript
//    Walk a cell (and any nested sub-cells) looking for the
//    "script" metadata for the given property name.
//---------------------------------------------------------

static void collectCellDefaultScript(const nlohmann::json& cell, const std::string& name, std::string& out) {
      if (!out.empty())
            return;
      if (cell.contains("name") && cell["name"].is_string() && cell["name"].get<std::string>() == name &&
          cell.contains("script") && cell["script"].is_string()) {
            out = cell["script"].get<std::string>();
            return;
            }
      if (cell.contains("cells") && cell["cells"].is_array())
            for (const auto& subCell : cell["cells"])
                  collectCellDefaultScript(subCell, name, out);
      }

//---------------------------------------------------------
//   defaultScriptForName
//---------------------------------------------------------

std::string defaultScriptForName(std::string_view propStr, const std::string& name) {
      std::string script;
      try {
            nlohmann::json j = nlohmann::json::parse(propStr);
            if (j.contains("rows") && j["rows"].is_array()) {
                  for (const auto& row : j["rows"]) {
                        if (row.contains("cells") && row["cells"].is_array())
                              for (const auto& cell : row["cells"])
                                    collectCellDefaultScript(cell, name, script);
                        }
                  }
            }
      catch (const nlohmann::json::parse_error& err) {
            Warning("defaultScriptForName: JSON parse error: {}", err.what());
            }
      return script;
      }

//---------------------------------------------------------
//   collectAllCellDefaultScripts
//    Walk a cell (and any nested sub-cells) and collect all
//    (name, script) pairs that declare a "script" metadata.
//---------------------------------------------------------

static void collectAllCellDefaultScripts(const nlohmann::json& cell, std::vector<DefaultScript>& out) {
      if (cell.contains("name") && cell["name"].is_string() && cell.contains("script") &&
          cell["script"].is_string()) {
            std::string name   = cell["name"].get<std::string>();
            std::string script = cell["script"].get<std::string>();
            if (!script.empty())
                  out.emplace_back(name, script);
            }
      if (cell.contains("cells") && cell["cells"].is_array())
            for (const auto& subCell : cell["cells"])
                  collectAllCellDefaultScripts(subCell, out);
      }

//---------------------------------------------------------
//   allDefaultScripts
//---------------------------------------------------------

std::vector<DefaultScript> allDefaultScripts(std::string_view propStr) {
      std::vector<DefaultScript> scripts;
      try {
            nlohmann::json j = nlohmann::json::parse(propStr);
            if (j.contains("rows") && j["rows"].is_array()) {
                  for (const auto& row : j["rows"]) {
                        if (row.contains("cells") && row["cells"].is_array())
                              for (const auto& cell : row["cells"])
                                    collectAllCellDefaultScripts(cell, scripts);
                        }
                  }
            }
      catch (const nlohmann::json::parse_error& err) {
            Warning("allDefaultScripts: JSON parse error: {}", err.what());
            }
      return scripts;
      }

//---------------------------------------------------------
//   collectCellTooltip
//    Walk a cell (and any nested sub-cells) looking for the
//    "tooltip" metadata for the given property name.
//---------------------------------------------------------

static void collectCellTooltip(const nlohmann::json& cell, const std::string& name, std::string& out) {
      if (!out.empty())
            return;
      if (cell.contains("name") && cell["name"].is_string() && cell["name"].get<std::string>() == name &&
          cell.contains("tooltip") && cell["tooltip"].is_string()) {
            out = cell["tooltip"].get<std::string>();
            return;
            }
      if (cell.contains("cells") && cell["cells"].is_array())
            for (const auto& subCell : cell["cells"])
                  collectCellTooltip(subCell, name, out);
      }

//---------------------------------------------------------
//   tooltipForName
//---------------------------------------------------------

std::string tooltipForName(std::string_view propStr, const std::string& name) {
      std::string tooltip;
      try {
            nlohmann::json j = nlohmann::json::parse(propStr);
            if (j.contains("rows") && j["rows"].is_array()) {
                  for (const auto& row : j["rows"]) {
                        if (row.contains("cells") && row["cells"].is_array())
                              for (const auto& cell : row["cells"])
                                    collectCellTooltip(cell, name, tooltip);
                        }
                  }
            }
      catch (const nlohmann::json::parse_error& err) {
            Warning("tooltipForName: JSON parse error: {}", err.what());
            }
      return tooltip;
      }

      } // namespace propjson
