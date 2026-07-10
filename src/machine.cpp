//=============================================================================
//  ZCam - manufacturing tool for G-code machines and Fiber Laser
//
//  Copyright (C) 2026 Werner Schweer
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#include "machine.h"
#include "propertyjson.h"
#include "logger.h"

//---------------------------------------------------------
//   toJson
//    Serialize all user-editable properties declared in
//    properties() to JSON, using the shared propjson utilities.
//    Machine is a Q_GADGET, so gadget=true.
//---------------------------------------------------------

json Machine::toJson() const {
      json data;

      std::string_view propStr = properties();
      if (propStr.empty())
            return data;

      std::vector<std::pair<std::string, std::string>> propNames;
      try {
            propNames = propjson::parseAllPropertyNames(propStr);
            }
      catch (const nlohmann::json::parse_error& err) {
            Warning("Machine::toJson: JSON parse error: {}", err.what());
            return data;
            }

      const QMetaObject* meta = &Machine::staticMetaObject;
      for (const auto& [name, type] : propNames)
            propjson::writePropertyToJson(data, this, meta, true, name, type);

      return data;
      }

//---------------------------------------------------------
//   fromJson
//    Deserialize all properties from JSON using the shared
//    propjson utilities.  Machine is a Q_GADGET, so gadget=true.
//---------------------------------------------------------

bool Machine::fromJson(const json& data) {
      std::string_view propStr = properties();
      if (propStr.empty())
            return true;

      try {
            auto propNames = propjson::parseAllPropertyNames(propStr);
            const QMetaObject* meta = &Machine::staticMetaObject;
            for (const auto& [name, type] : propNames)
                  propjson::readPropertyFromJson(data, this, meta, true, name, type);
            }
      catch (const nlohmann::json::parse_error& err) {
            Warning("Machine::fromJson: JSON parse error: {}", err.what());
            return false;
            }
      catch (...) {
            Warning("Machine::fromJson: unknown JSON error");
            return false;
            }
      return true;
      }