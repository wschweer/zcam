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
      Q_PROPERTY(bool editing READ editing WRITE setEditing NOTIFY editingChanged)

      PROPV(int, weight, 600)
      PROPV(QString, text, "ZCam")
      PROPV(QString, fontFamily, "NotoSans")
      PROPV(double, pointSize, 24.0)
      PROPV(int, stretch, 100)
      PROPV(double, letterSpacing, 100.0)
      PROPV(double, wordSpacing, .0)
      PROPV(double, lineSpacing, 100.0)
      PROPV(int, align, Qt::AlignLeft)
      PROPV(bool, bold, false)
      PROPV(bool, italic, false)
      PROPV(bool, underline, false)

      inline static constexpr std::string_view _properties {
         R"json({
                  "class": "Text",
                  "items": [
                    {
                      "row": {
                        "show": {
                          "label": "Show",
                          "type": "bool",
                          "default": true
                        },
                        "burn": {
                          "label": "Burn",
                          "type": "bool",
                          "default": true
                        }
                      },
                      "label": "State"
                    },
                    {
                      "name": "laserLayer",
                      "label": "LaserLayer",
                      "type": "laserLayer",
                      "default": ""
                    },
                    {
                      "name": "color",
                      "label": "Color",
                      "type": "color",
                      "default": "green"
                    },
                    {
                      "name": "pos",
                      "label": "Pos.",
                      "type": "vector3d",
                      "unit": "mm",
                      "default": [
                        0.0,
                        0.0,
                        0.0
                      ]
                    },
                    {
                      "name": "rot",
                      "label": "Rot.",
                      "type": "vector3d",
                      "unit": "°",
                      "min": 0.0,
                      "max": 360,
                      "default": [
                        0.0,
                        0.0,
                        0.0
                      ]
                    },
                    {
                      "name": "scale",
                      "label": "Scale",
                      "type": "scale",
                      "min": 0.001,
                      "max": 1000,
                      "default": [
                        1.0,
                        1.0,
                        1.0
                      ]
                    },
                    {
                      "name": "lockScale",
                      "label": "Lock",
                      "type": "lockScale",
                      "default": 2
                    },
                    {
                      "row": {
                        "mirrorX": {
                          "label": "X",
                          "type": "bool",
                          "default": false
                        },
                        "mirrorY": {
                          "label": "Y",
                          "type": "bool",
                          "default": false
                        }
                      },
                      "label": "Mirror"
                    },
                    {
                      "name": "line",
                      "type": "line"
                    },
                    {
                      "name": "text",
                      "label": "Text",
                      "type": "multiline",
                      "default": "ZCam"
                    },
                    {
                      "name": "fontFamily",
                      "label": "Font",
                      "type": "font",
                      "default": "NotoSans"
                    },
                    {
                      "row": {
                        "bold": {
                          "label": "B",
                          "type": "fontStyle",
                          "default": false
                        },
                        "italic": {
                          "label": "I",
                          "type": "fontStyle",
                          "default": false
                        },
                        "underline": {
                          "label": "U",
                          "type": "fontStyle",
                          "default": false
                        }
                      },
                      "label": " "
                    },
                    {
                      "row": {
                        "pointSize": {
                          "label": "Size",
                          "type": "float",
                          "unit": "pt",
                          "min": 1.0,
                          "max": 1000.0,
                          "default": 24.0
                        },
                        "weight": {
                          "label": "Weight",
                          "type": "int",
                          "min": 100,
                          "max": 900,
                          "default": 600
                        }
                      },
                      "label": " "
                    },
                    {
                      "row": {
                        "stretch": {
                          "label": "Stretch",
                          "type": "int",
                          "unit": "%",
                          "min": 25,
                          "max": 400,
                          "default": 100
                        },
                        "letterSpacing": {
                          "label": "Letter",
                          "type": "float",
                          "unit": "%",
                          "min": 1.0,
                          "max": 500.0,
                          "default": 100.0
                        }
                      },
                      "label": "Spacing"
                    },
                    {
                      "row": {
                        "wordSpacing": {
                          "label": "Word",
                          "type": "float",
                          "unit": "pxl",
                          "min": -100.0,
                          "max": 100.0,
                          "default": 0.0
                        },
                        "lineSpacing": {
                          "label": "Line",
                          "type": "float",
                          "unit": "%",
                          "min": 0.0,
                          "max": 500.0,
                          "default": 100.0
                        }
                      },
                      "label": " "
                    },

                    {
                      "row": {
                        "fill": {
                          "label": "Fill",
                          "type": "bool",
                          "default": true
                        },
                        "align": {
                          "label": "Align",
                          "type": "halign",
                          "default": 4
                        }
                      },
                      "label": " "
                    }
                  ]
                      })json"};

      QFont font;
      std::vector<double> lineOffsets;

      int cursorRow {0};
      int cursorColumn {0};
      bool _editing {false};
      Clipper2Lib::PathsD _cursorLines;

      //      bool userDelete() const override { return true; }
      void updateText();
      void updateCursor();
      void updateSelectionGeometry() override;
      double fontLineSpacing() const;
      void addText(QList<QPolygonF>& polys, const QPointF& gpos, const QList<QGlyphRun>& glyphRuns);

    signals:
      void editingChanged();

    public slots:
      void update(int flags = -1) override;

    public:
      explicit Text(ZCam*, Element* parent = nullptr);
      virtual ~Text() {}
      virtual QString typeName() override { return QStringLiteral("text"); }
      Q_INVOKABLE bool nameEditable() const override { return true; }
      virtual const std::string_view properties() const override { return _properties; }
      Q_INVOKABLE virtual bool visible() const override { return true; }
      Q_INVOKABLE bool draggable() const override { return true; }
      Q_INVOKABLE bool deletable() const override { return true; }
      bool editing() const { return _editing; }
      Q_INVOKABLE void setEditing(bool v);
      Q_INVOKABLE bool keyEvent(int key, int modifiers, const QString& s);
      Q_INVOKABLE bool setCursorPositionFromWorld(const QVector3D& worldPos);
      };
