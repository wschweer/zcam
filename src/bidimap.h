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

#include <array>
#include <optional>
#include <stdexcept>
// #include <string_view>

//---------------------------------------------------------
//   BidiMap
//---------------------------------------------------------

template <typename A, typename B>
struct BidiMapEntry {
      A a;
      B b;
      };

template <typename A, typename B, std::size_t N>
class BidiMap
      {
    public:
      using Entry = BidiMapEntry<A, B>;
      constexpr explicit BidiMap(std::array<Entry, N> entries) : data_(entries) {}
      constexpr explicit BidiMap(const Entry (&arr)[N]) : data_(std::to_array(arr)) {}

      constexpr std::optional<B> getB(const A& key) const {
            for (const auto& entry : data_)
                  if (entry.a == key)
                        return entry.b;
            return std::nullopt;
            }
      // Lookup: TypeB -> TypeA
      constexpr std::optional<A> getA(const B& key) const {
            for (const auto& entry : data_)
                  if (entry.b == key)
                        return entry.a;
            return std::nullopt;
            }
      // --- Direktzugriff (Wirft Compile-Fehler bei Compile-Zeit bzw. Exception zur Laufzeit) ---

      constexpr B atA(const A& key) const {
            for (const auto& entry : data_)
                  if (entry.a == key)
                        return entry.b;
            throw std::out_of_range("A not in BidiMap.");
            }
      constexpr A atB(const B& key) const {
            for (const auto& entry : data_)
                  if (entry.b == key)
                        return entry.a;
            throw std::out_of_range("B not in BidiMap.");
            }
      [[nodiscard]] constexpr std::size_t size() const { return N; }

    private:
      std::array<Entry, N> data_;
      };
