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
#include "laser.h"
#include "laser_bjjcz.h"
#include "laser_rkq.h"
#include "machinegcode.h"
#include "propertyjson.h"
#include "logger.h"

//---------------------------------------------------------
//   toJson
//    Serialize all user-editable properties declared in
//    properties() to JSON, using the shared propjson utilities.
//    Machine is a QObject, so gadget=false.
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

      const QMetaObject* meta = metaObject();
      for (const auto& [name, type] : propNames)
            propjson::writePropertyToJson(data, this, meta, false, name, type);

      return data;
      }

//---------------------------------------------------------
//   fromJson
//    Deserialize all properties from JSON using the shared
//    propjson utilities.  Machine is a QObject, so gadget=false.
//---------------------------------------------------------

bool Machine::fromJson(const json& data) {
      // Read type and boardType first, because properties() depends on
      // type() to determine which property set to return.  Without this,
      // properties() would use the default type() (empty → Q-switched) and
      // miss type-specific properties like minFreq/maxFreq/ticklePulse/etc.
      // for UV lasers, causing them to keep their default values on load.
      if (data.contains("type") && data["type"].is_string()) {
            QString t = QString::fromStdString(data["type"].get<std::string>());
            // Migration: map legacy type names
            if (t == QStringLiteral("Fiber Laser"))
                  t = QStringLiteral("Q-switched Laser");
            set_type(t);
            }
      if (data.contains("boardType") && data["boardType"].is_string())
            set_boardType(QString::fromStdString(data["boardType"].get<std::string>()));

      std::string_view propStr = properties();
      if (propStr.empty())
            return true;

      try {
            auto propNames          = propjson::parseAllPropertyNames(propStr);
            const QMetaObject* meta = metaObject();
            for (const auto& [name, type] : propNames)
                  propjson::readPropertyFromJson(data, this, meta, false, name, type);

            // Migration: rename legacy machine type names to current ones.
            // This handles the case where readPropertyFromJson read the
            // raw (unmigrated) "Fiber Laser" value from JSON.
            if (type() == QStringLiteral("Fiber Laser"))
                  set_type(QStringLiteral("Q-switched Laser"));
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

//---------------------------------------------------------
//   createMachine
//    Factory: create a concrete Machine subclass based on the
//    machine type string.  Laser types create the appropriate
//    Laser subclass based on boardType; GCode CNC creates a
//    MachineGCode.
//---------------------------------------------------------

Machine* Machine::create(ZCam* zc, const QString& machineType, const QString& boardType) {
      if (machineType == QStringLiteral("GCode CNC"))
            return new MachineGCode(zc);
      // All laser types — create the appropriate Laser subclass
      // based on the board type.  The specific Laser variant is
      // chosen here so that board-specific code (USB/Ethernet) is
      // compiled into the right subclass.
      if (boardType == QStringLiteral("RKQ-LM-441"))
            return new LaserRKQ(zc);
      // Default: BJJCZ board
      return new LaserBJJCZ(zc);
      }