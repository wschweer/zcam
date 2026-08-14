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

#include "mop.h"
#include "zcam.h"
#include "element.h"
#include "config.h"

#include <QColor>
#include <QMetaObject>
#include <array>
#include <cstdint>
#include <functional>

//---------------------------------------------------------
//   mopColorTable
//    Returns the QColor for the given Mop colour index (0..31).
//    Index 0 is a neutral grey (the NopMop default).
//    Indices 1..31 follow the LightBurn layer colour palette.
//
//    LightBurn layer colours (approximated from the default
//    palette): a mix of saturated primary/secondary colours
//    with good contrast against a dark canvas background.
//--------------------------------------------------------------------

QColor Mop::mopColorTable(int index) {
      static constexpr std::array<uint32_t, 32> colors = {
         0xFF808080, // 0  grey (NopMop default)
         0xFFFF0000, // 1  red
         0xFF00FF00, // 2  green
         0xFF0000FF, // 3  blue
         0xFFFFFF00, // 4  yellow
         0xFFFF00FF, // 5  magenta
         0xFF00FFFF, // 6  cyan
         0xFFFF8000, // 7  orange
         0xFF8000FF, // 8  purple
         0xFF0080FF, // 9  light blue
         0xFF80FF00, // 10 lime
         0xFFFF0080, // 11 pink
         0xFF00FF80, // 12 spring green
         0xFF808000, // 13 olive
         0xFF008080, // 14 teal
         0xFF800080, // 15 maroon
         0xFFFF8080, // 16 light red
         0xFF80FF80, // 17 light green
         0xFF8080FF, // 18 light blue
         0xFFFFFF80, // 19 light yellow
         0xFFFF80FF, // 20 light magenta
         0xFF80FFFF, // 21 light cyan
         0xFFC0C0C0, // 22 silver
         0xFFA04040, // 23 dark red-brown
         0xFF40A040, // 24 dark green
         0xFF4040A0, // 25 dark blue
         0xFFA0A040, // 26 dark yellow
         0xFFA040A0, // 27 dark magenta
         0xFF40A0A0, // 28 dark cyan
         0xFF404040, // 29 dark grey
         0xFFE0E0E0, // 30 near white
         0xFF202020, // 31 near black
            };

      if (index < 0)
            index = 0;
      if (index >= 32)
            index = 31;
      uint32_t c = colors[static_cast<size_t>(index)];
      return QColor(qRed(c), qGreen(c), qBlue(c), qAlpha(c));
      }

//---------------------------------------------------------
//   configMopColor
//    Returns the QColor for the given Mop colour index from the
//    user-configurable Config properties (mopColor0..mopColor31).
//    Falls back to mopColorTable() (the built-in default palette)
//    when no ZCam/Config instance is available.
//--------------------------------------------------------------------

QColor Mop::configMopColor(int index) const {
      if (index < 0)
            index = 0;
      if (index >= 32)
            index = 31;
      ZCam* zc = zcamInstance();
      if (!zc || !zc->config())
            return mopColorTable(index);
      Config* cfg = zc->config();
      // Read the mopColor<N> property from Config via QMetaObject.
      QString propName = QStringLiteral("mopColor%1").arg(index);
      QVariant v = cfg->property(propName.toUtf8().constData());
      if (v.isValid() && v.canConvert<QColor>())
            return v.value<QColor>();
      return mopColorTable(index);
      }

//---------------------------------------------------------
//   nextFreeColorIndex
//    Walk the project tree and collect all colorIndex values used
//    by Mop elements.  Return the first unused index in 1..31.
//    If all are in use, return the index that is used the fewest
//    times (so duplicate colours are rare).
//--------------------------------------------------------------------

int Mop::nextFreeColorIndex(Element* root) {
      // Count how many Mops use each colour index (0..31).
      std::array<int, 32> usageCount {};

      std::function<void(Element*)> walk = [&](Element* e) {
            if (!e)
                  return;
            if (auto* mop = qobject_cast<Mop*>(e)) {
                  int idx = mop->colorIndex();
                  if (idx >= 0 && idx < 32)
                        ++usageCount[idx];
                  }
            for (Element* c : e->children())
                  walk(c);
            };
      walk(root);

      // Find the first unused index starting at 1.
      for (int i = 1; i < 32; ++i)
            if (usageCount[i] == 0)
                  return i;

      // All indices are used — return the least-used one.
      int bestIdx   = 1;
      int bestCount = usageCount[1];
      for (int i = 2; i < 32; ++i) {
            if (usageCount[i] < bestCount) {
                  bestCount = usageCount[i];
                  bestIdx   = i;
                  }
            }
      return bestIdx;
      }

//---------------------------------------------------------
//   Mop
//--------------------------------------------------------------------

Mop::Mop(ZCam* zcam, Element* parent) : Element3d(zcam, parent) {
      // When this Mop's colour index changes, every element in the
      // project tree whose effective Mop is this Mop must update its
      // displayed colour.  Walk the entire tree and emit curColorChanged
      // on each Element3d whose effectiveMop() == this.
      connect(this, &Mop::colorIndexChanged, this, [this] {
            ZCam* zc = zcamInstance();
            if (!zc || !zc->rootElement())
                  return;
            std::function<void(Element*)> walk = [&](Element* e) {
                  if (!e)
                        return;
                  if (auto* e3d = qobject_cast<Element3d*>(e)) {
                        if (e3d->effectiveMop() == this)
                              emit e3d->curColorChanged();
                        }
                  for (auto* c : e->children())
                        walk(c);
                  };
            walk(zc->rootElement());
            });
      }

//---------------------------------------------------------
//   NopMop
//--------------------------------------------------------------------

NopMop::NopMop(ZCam* zcam, Element* parent) : Mop(zcam, parent) {
      setName("NopMop");
      set_model("LaserLayer1.qml");
      }