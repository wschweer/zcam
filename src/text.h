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
      PROPV(double, pointSize, 24)
      PROPV(int, stretch, 100)
      PROPV(double, letterSpacing, 100.0)
      PROPV(double, wordSpacing, .0)
      PROPV(double, lineSpacing, 100.0)
      PROPV(int, align, Qt::AlignLeft)

      inline static constexpr std::string_view properties{R"({
            "class": "Text",
            "text":          { "label": "Text",        "type": "multiline" },
            "fill":          { "label": "Fill",        "type": "bool" },
            "weight":        { "label": "Weight",      "type": "int",   "min": 100,   "max": 900 },
            "fontFamily":    { "label": "Font",        "type": "font" },
            "pointSize":     { "label": "Size",        "type": "float", "unit": "pt", "min": 1.0, "max": 1000.0 },
            "stretch":       { "label": "Stretch",     "type": "int",   "unit": "%",  "min": 25,    "max": 400 },
            "letterSpacing": { "label": "LetterSp",    "type": "float", "min": 0.0,   "max": 500.0 },
            "wordSpacing":   { "label": "WordSp",      "type": "float", "unit": "mm", "min": -100.0, "max": 100.0 },
            "lineSpacing":   { "label": "LineSp",      "type": "float", "min": 0.0,   "max": 500.0 },
            "align":         { "label": "Align",       "type": "int",   "min": 0,     "max": 4 }
                  })"};

      QFont font;
      QFontMetricsF* fontMetrics{nullptr};
      std::vector<double> lineOffsets;

      int cursorRow{0};
      int cursorColumn{0};

      //      bool userDelete() const override { return true; }
      void updateText();
      void updateCursor();
      double fontLineSpacing() const;
      void addText(QList<QPolygonF>& polys, const QPointF& gpos, const QList<QGlyphRun>& glyphRuns);

    public slots:
      void update(int flags = -1) override;

    public:
      explicit Text(ZCam*, Element* parent = nullptr);
      virtual ~Text();
      virtual const char* typeName() override { return "text"; }
      static std::string_view getProperties() { return properties; }
      json toJson() const override;
      void fromJson(const json& json) override;
      };
