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

#define PROP_GADGET(T, name) \
      Q_PROPERTY(T name READ name WRITE set_##name) \
    public: \
      T name() const { return _##name; } \
      void set_##name(T v) { _##name = v; } \
    private: \
      T _##name;

#define PROPV_GADGET(T, name, value) \
      Q_PROPERTY(T name READ name WRITE set_##name) \
    public: \
      T name() const { return _##name; } \
      void set_##name(T v) { _##name = v; } \
    private: \
      T _##name {value};


// In element.h
#define PROP(T, name)                                                                                                                      \
      Q_PROPERTY(T name READ name WRITE set_##name NOTIFY name##Changed)                                                                   \
                                                                                                                                           \
    public:                                                                                                                                \
      T name() const {                                                                                                                     \
            return _##name;                                                                                                                \
            }                                                                                                                                    \
      void set_##name(T v) {                                                                                                               \
            if (v != _##name) {                                                                                                            \
                  _##name = v;                                                                                                             \
                  emit name##Changed();                                                                                                    \
                  }                                                                                                                              \
            }                                                                                                                                    \
    Q_SIGNALS:                                                                                                                             \
      void name##Changed();                                                                                                                \
                                                                                                                                           \
    private:                                                                                                                               \
      T _##name;

#define PROPV(typ, name, value)                                                                                                            \
      Q_PROPERTY(typ name READ name WRITE set_##name NOTIFY name##Changed)                                                                 \
                                                                                                                                           \
    public:                                                                                                                                \
      typ name() const {                                                                                                                   \
            return _##name;                                                                                                                \
            }                                                                                                                                    \
      void set_##name(typ v) {                                                                                                             \
            if (v != _##name) {                                                                                                            \
                  _##name = v;                                                                                                             \
                  emit name##Changed();                                                                                                    \
                  }                                                                                                                              \
            }                                                                                                                                    \
    Q_SIGNALS:                                                                                                                             \
      void name##Changed();                                                                                                                \
                                                                                                                                           \
    private:                                                                                                                               \
      typ _##name{value};
