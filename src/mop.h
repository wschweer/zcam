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

#include "element3d.h"

//---------------------------------------------------------
//   Mop
//    Base class for all Machine Operations (MOP).
//    A Mop owns a colour index (0..31) that determines the
//    display colour of all elements assigned to this Mop.
//    The 32-colour palette is inspired by LightBurn's layer
//    colours.
//
//    Derived classes:
//      - NopMop    – default no-op Mop assigned to Cad
//      - LaserMop  – laser-specific machine operation
//---------------------------------------------------------

class Mop : public Element3d
      {
      Q_OBJECT
      QML_ELEMENT
      QML_UNCREATABLE("no")

      PROPV(int, colorIndex, 0)

    public:
      //--------------------------------------------------------------------
      //     mopColorTable
      //--------------------------------------------------------------------
      /// Returns the QColor for the given Mop colour index (0..31).
      /// When a ZCam instance with a Config is available, the colour
      /// is read from the user-configurable Config properties
      /// (mopColor0..mopColor31).  Otherwise the built-in default
      /// palette is used.
      static QColor mopColorTable(int index);
      /// Same as mopColorTable but reads the colour from the Config
      /// attached to this Mop's ZCam instance when available.
      QColor configMopColor(int index) const;
      /// Number of entries in the Mop colour palette.
      static constexpr int mopColorCount() { return 32; }
      /// Find the first unused colour index among all Mop elements
      /// in the project tree rooted at *root*.  Index 0 is reserved
      /// for NopMop; the search starts at 1.  If all 1..31 are in use
      /// the least-used index is returned.
      static int nextFreeColorIndex(Element* root);

      //--------------------------------------------------------------------
      //     Mop
      //--------------------------------------------------------------------
      Mop(ZCam* zcam, Element* parent = nullptr);
      virtual ~Mop() = default;
      /// Convenience: return the QColor for this Mop's colorIndex.
      /// Prefers the user-configurable colour from Config when
      /// available, falling back to the built-in default palette.
      QColor mopColor() const { return configMopColor(_colorIndex); }
      /// Q_INVOKABLE wrapper for QML access.
      Q_INVOKABLE QColor color() const { return mopColor(); }

    protected:
      inline static constexpr std::string_view _properties {R"({
    "class": "Mop",
    "rows": [
        {
            "label": "Color",
            "cells": [
                {
                    "name": "colorIndex",
                    "type": "mopColor",
                    "default": 0
                }
            ]
        }
    ]
                          })"};
      };

//---------------------------------------------------------
//   NopMop
//    A no-op Mop used as the default Mop for Cad.  All elements
//    inherit Mop from Cad so every element always has a Mop.
//    NopMop does nothing — it merely provides the colour
//    assignment.
//---------------------------------------------------------

class NopMop : public Mop
      {
      Q_OBJECT
      QML_ELEMENT
      QML_UNCREATABLE("no")

    public:
      //--------------------------------------------------------------------
      //     NopMop
      //--------------------------------------------------------------------
      NopMop(ZCam* zcam, Element* parent = nullptr);
      ~NopMop() = default;
      virtual QString typeName() override { return QStringLiteral("nopMop"); }
      virtual const std::string_view properties() const override { return _properties; }
      Q_INVOKABLE virtual void update(int flags = -1) override {}
      };