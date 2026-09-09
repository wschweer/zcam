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

#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include "bidimap.h"

inline const std::vector<std::string> boardTypes {"BJJCZ", "RKQ-LM-441"};
enum MachineType { UNKNOWN, Q_LASER, MOPA_LASER, UV_LASER, GCODE_LASER, GCODE_MILL };

//---------------------------------------------------------
//   MachineTypes
//---------------------------------------------------------

using MachineEntry = BidiMapEntry<std::string_view, MachineType>;
template <std::size_t N> struct MachineTypes : public BidiMap<std::string_view, MachineType, N> {
      using BidiMap<std::string_view, MachineType, N>::BidiMap;
      constexpr std::optional<MachineType> type(std::string_view name) const { return this->getB(name); }
      constexpr std::string_view name(MachineType t) const { return this->getA(t).value_or(""); }
      constexpr MachineType typeAt(std::string_view name) const { return this->atA(name); }
      constexpr std::string_view nameAt(MachineType t) const { return this->atB(t); }
      };

template <std::size_t N> MachineTypes(const MachineEntry (&)[N]) -> MachineTypes<N>;

inline constexpr MachineTypes machineTypeMap {{
          {"Unknown", MachineType::UNKNOWN},
          {"Q-switched Laser", MachineType::Q_LASER},
          {"MOPA Laser", MachineType::MOPA_LASER},
          {"UV Laser", MachineType::UV_LASER},
          {"GCode Laser", MachineType::GCODE_LASER},
          {"GCode Mill", MachineType::GCODE_MILL}
          }};
