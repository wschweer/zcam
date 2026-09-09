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
#include "engine.h"
#include "laser.h"
#include "laser_bjjcz.h"
#include "laser_rkq.h"
#include "machinegcode.h"
#include "propertyjson.h"
#include "logger.h"

//---------------------------------------------------------
//   Machine
//---------------------------------------------------------

Machine::Machine(ZCam* zc, QObject* parent) : QObject(parent), zcam(zc) {
      }

//---------------------------------------------------------
//   ~Machine
//---------------------------------------------------------

Machine::~Machine() {
      delete _engine;
      _engine = nullptr;
      }

//---------------------------------------------------------
//   toJson
//    Serialize shared Machine properties + engine-specific
//    properties to JSON.
//---------------------------------------------------------

json Machine::toJson() const {
      json data;

      // ── shared properties (declared in Machine itself) ──────────
      // Write the base set of properties that every Machine has,
      // regardless of engine type.  These are the properties declared
      // via PROP/PROPV in machine.h.
            {
            // Build a minimal properties() JSON for the shared properties.
            // The actual property list is obtained from the engine, which
            // includes the shared properties at the top of its JSON layout.
            // We rely on the engine's properties() to list ALL properties
            // (shared + engine-specific) and serialise them all through
            // the meta-object system of the Machine + Engine hierarchy.
            }

      std::string propStr;
      if (_engine)
            propStr = _engine->properties();
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

      // Serialise shared Machine properties via the Machine meta-object.
      const QMetaObject* machineMeta = metaObject();
      for (const auto& [name, type] : propNames) {
            // Try the Machine's own meta-object first (shared properties).
            int idx = machineMeta->indexOfProperty(name.c_str());
            if (idx >= 0) {
                  propjson::writePropertyToJson(
                      data, this, machineMeta, false, name, type, propjson::precisionForName(propStr, name));
                  }
            else if (_engine) {
                  // Engine-specific property (e.g. galvo, laser delays, IO pins).
                  const QMetaObject* engineMeta = _engine->metaObject();
                  int eidx                      = engineMeta->indexOfProperty(name.c_str());
                  if (eidx >= 0)
                        propjson::writePropertyToJson(data, _engine, engineMeta, false, name, type,
                            propjson::precisionForName(propStr, name));
                  }
            }

      return data;
      }

//---------------------------------------------------------
//   fromJson
//    Deserialize shared Machine properties + engine-specific
//    properties from JSON.
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
            set_typeFromName(t);
            }
      if (data.contains("boardType") && data["boardType"].is_string())
            set_boardType(QString::fromStdString(data["boardType"].get<std::string>()));

      // Create the engine if not yet present (type/boardType are now set).
      if (!_engine)
            createEngine();

      std::string propStr;
      if (_engine)
            propStr = _engine->properties();
      if (propStr.empty())
            return true;

      try {
            auto propNames                 = propjson::parseAllPropertyNames(propStr);
            const QMetaObject* machineMeta = metaObject();
            const QMetaObject* engineMeta  = _engine ? _engine->metaObject() : nullptr;

            for (const auto& [name, type] : propNames) {
                  // Try the Machine's own meta-object first (shared properties).
                  int idx = machineMeta->indexOfProperty(name.c_str());
                  if (idx >= 0) {
                        propjson::readPropertyFromJson(data, this, machineMeta, false, name, type);
                        }
                  else if (engineMeta) {
                        int eidx = engineMeta->indexOfProperty(name.c_str());
                        if (eidx >= 0)
                              propjson::readPropertyFromJson(data, _engine, engineMeta, false, name, type);
                        }
                  }

            // Migration: rename legacy machine type names to current ones.
            // This handles the case where readPropertyFromJson read the
            // raw (unmigrated) "Fiber Laser" value from JSON.
            if (machineTypeName() == QStringLiteral("Fiber Laser"))
                  set_typeFromName(QStringLiteral("Q-switched Laser"));
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
//   set_typeFromName
//    Set the machine type from its human-readable string name.
//    Performs a lookup in the machineTypeMap.  If the name is
//    not found, the type is left unchanged.
//---------------------------------------------------------

void Machine::set_typeFromName(const QString& name) {
      auto sv = name.toUtf8();
      auto mt = machineTypeMap.type(std::string_view(sv.constData(), sv.size()));
      if (mt)
            set_type(*mt);
      }

//---------------------------------------------------------
//   createEngine
//    Create or replace the Engine based on the current type()
//    and boardType().
//---------------------------------------------------------

void Machine::createEngine() {
      delete _engine;
      _engine = Engine::create(this, _type, _boardType);
      emit engineChanged();
      }

//---------------------------------------------------------
//   changeType
//    Change the machine type dynamically.  Creates a new Engine
//    of the appropriate subclass, replacing the old one.  The
//    shared properties (travel, precision, etc.) are preserved
//    because they live on Machine, not on the Engine.
//---------------------------------------------------------

void Machine::changeType(MachineType newType, const QString& boardType) {

      // If boardType is empty, keep the current one.
      QString bt = boardType.isEmpty() ? _boardType : boardType;

      // Update type and boardType first.
      set_type(newType);
      set_boardType(bt);

      // Replace the engine.
      delete _engine;
      _engine = Engine::create(this, newType, bt);
      emit engineChanged();
      }

//---------------------------------------------------------
//   laserEngine
//    Convenience: return the engine as a Laser, or nullptr.
//---------------------------------------------------------

Laser* Machine::laserEngine() const {
      return qobject_cast<Laser*>(_engine);
      }

//---------------------------------------------------------
//   create
//    Factory: create a Machine with the appropriate Engine
//    based on the machine type enum and board type string.
//---------------------------------------------------------

Machine* Machine::create(ZCam* zc, MachineType machineType, const QString& boardType) {
//      Debug("Machine::create type={} board={}", int(machineType), boardType.toStdString());
      Machine* m = new Machine(zc);
      m->set_type(machineType);
      m->set_boardType(boardType);
      m->createEngine();
      return m;
      }