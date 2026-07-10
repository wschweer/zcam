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

#pragma once

#include <nlohmann/json.hpp>
#include <string_view>
#include <vector>
#include <utility>
#include <QMetaObject>
#include <QMetaProperty>
#include <QVariant>
#include <QVector3D>
#include <QVector2D>
#include <QColor>

//---------------------------------------------------------
//   propjson
//    Shared utilities for serialising/deserialising properties
//    to/from JSON using a properties() JSON definition.
//    Works with both QObject-derived classes and Q_GADGET classes.
//---------------------------------------------------------

namespace propjson {

using PropNameList = std::vector<std::pair<std::string, std::string>>;

//---------------------------------------------------------
//   parseAllPropertyNames
//    Parse the properties() JSON definition and return a list of
//    (propertyName, type) pairs.  Handles both the "items" array
//    format and the old-style top-level key format.
//---------------------------------------------------------
PropNameList parseAllPropertyNames(std::string_view propStr);

//---------------------------------------------------------
//   writePropertyToJson
//    Serialise a single property to JSON.  Returns true if the
//    property was handled, false if the type is unknown (caller
//    should handle it).
//
//    meta   – the QMetaObject of the containing class
//    obj    – pointer to the object instance
//    gadget – true for Q_GADGET (uses readOnGadget),
//             false for QObject (uses read)
//---------------------------------------------------------
bool writePropertyToJson(nlohmann::json& data, const void* obj, const QMetaObject* meta, bool gadget,
                         const std::string& name, const std::string& type);

//---------------------------------------------------------
//   readPropertyFromJson
//    Deserialise a single property from JSON.  Returns true if
//    the property was handled, false if the type is unknown or
//    the key is missing.
//---------------------------------------------------------
bool readPropertyFromJson(const nlohmann::json& data, void* obj, const QMetaObject* meta, bool gadget,
                          const std::string& name, const std::string& type);

      } // namespace propjson