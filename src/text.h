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

#include <QFont>

class QFontMetricsF;
class QGlyphRun;
class Tesselator;
class ZCam;

#include "element3d.h"
enum TextSubelement { BboxSub = 0, CursorSub = 1 };

//---------------------------------------------------------
//   Text
//---------------------------------------------------------

class Text : public Element3d
      {
      Q_OBJECT

      PROPV(bool, fill, true)
      PROPV(int, weight, 600)
      PROPV(QString, text, "Mops")
      PROPV(QString, fontFamily, "NotoSans")
      PROPV(double, pointSize, 24.0)
      PROPV(int, stretch, 100)
      PROPV(double, letterSpacing, 100.0)
      PROPV(double, wordSpacing, .0)
      PROPV(double, lineSpacing, 100.0)
      PROPV(int, align, Qt::AlignLeft)

      inline static constexpr std::string_view _properties {R"({
            "class": "Text",
            "items": [
                  { "row":
                        {
                              "show": { "label": "Show",     "type": "bool", "default": true },
                              "burn": { "label": "Burn",     "type": "bool", "default": true }
                        },
                        "label": "State"
                     },
                  { "name": "color",         "label": "Color",    "type": "color","default": "green" },
                  { "name": "pos",           "label": "Pos.",     "type": "vector3d", "unit": "mm",  "default": [0.0, 0.0, 0.0] },
                  { "name": "rot",           "label": "Rot.",     "type": "vector3d", "unit": "°", "min": 0.0, "max": 360, "default": [0.0, 0.0, 0.0] },
                  { "name": "scale",         "label": "Scale",    "type": "vector3d", "min": 0.001, "max": 1000, "default": [1.0, 1.0, 1.0] },
                  { "row":
                        {
                              "mirrorX": { "label": "X", "type": "bool", "default": false },
                              "mirrorY": { "label": "Y", "type": "bool", "default": false }
                        },
                        "label": "Mirror"
                     },
                  { "name": "line", "type": "line" },
                  { "name": "text",          "label": "Text",     "type": "multiline", "default": "Mops" },
                  { "name": "fontFamily",    "label": "Font",     "type": "font", "default": "NotoSans" },
                  { "row":
                        {
                              "pointSize": { "label": "Size",     "type": "float", "unit": "pt", "min": 1.0,   "max": 1000.0, "default": 24.0 },
                              "weight":    { "label": "Weight",   "type": "int",   "min": 100,   "max": 900, "default": 600 }
                        },
                        "label": " "
                     },
                  { "row":
                        {
                              "stretch":       { "label": "Stretch",  "type": "int",   "unit": "%",  "min": 25,    "max": 400, "default": 100 },
                              "letterSpacing": { "label": "Letter", "type": "float", "unit": "%",  "min": 1.0,   "max": 500.0, "default": 100.0 }
                        },
                        "label": "Spacing"
                     },
                  { "row":
                        {
                              "wordSpacing":   { "label": "Word",   "type": "float", "unit": "pxl", "min": -100.0, "max": 100.0, "default": 0.0 },
                              "lineSpacing":   { "label": "Line",   "type": "float", "unit": "%",  "min": 0.0,   "max": 500.0, "default": 100.0 }
                        },
                        "label": " "
                     },

                  { "row":
                        {
                              "fill":          { "label": "Fill",     "type": "bool", "default": true },
                              "align":         { "label": "Align",    "type": "halign", "default": 4 }
                        },
                        "label": " "
                     }
                  ]
                        })"};

      QFont font;
      QFontMetricsF* fontMetrics {nullptr};
      std::vector<double> lineOffsets;

      int cursorRow {0};
      int cursorColumn {0};

      //      bool userDelete() const override { return true; }
      void updateText();
      void updateCursor();
      double fontLineSpacing() const;
      void addText(QList<QPolygonF>& polys, const QPointF& gpos, const QList<QGlyphRun>& glyphRuns);

    public slots:
      void update(int flags = -1) override;

    public:
      explicit Text(ZCam*, Element* parent = nullptr);
      virtual ~Text() {}
      virtual QString typeName() override { return QStringLiteral("text"); }
      Q_INVOKABLE bool nameEditable() const override { return true; }
      virtual const std::string_view properties() const override { return _properties; }
      Q_INVOKABLE virtual bool visible() const override { return true; }
      };
