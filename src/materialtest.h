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
#include "laserengine.h"

//---------------------------------------------------------
//   MaterialTest
//---------------------------------------------------------

class MaterialTest : public Element3d
      {
      Q_OBJECT
      QML_ELEMENT
      QML_UNCREATABLE("no")

      PROPV(QString, description, "Test")
      PROPV(int, rows, 5)
      PROPV(int, columns, 5)
      PROPV(double, boxHeight, 5.0)
      PROPV(double, boxWidth, 5.0)
      PROPV(int, rowParameter, int(ParameterType::Speed))
      PROPV(int, columnParameter, int(ParameterType::Power))
      PROPV(double, rowMin, 100.0)
      PROPV(double, rowMax, 1000.0)
      PROPV(double, columnMin, 10.0)
      PROPV(double, columnMax, 80.0)
      PROPV(Recipe*, materialLayer, nullptr)
      PROPV(Recipe*, textLayer,     nullptr)
      PROPV(Recipe*, borderLayer,   nullptr)
      PROPV(bool, showBorder, true)
      PROPV(bool, showText, true)

      inline static constexpr std::string_view _properties {
         R"json({
                  "class": "MaterialTest",
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
                      "name": "description",
                      "label": "Desc.",
                      "type": "singleline",
                      "default": "Test"
                    },
                    {
                      "name": "line",
                      "type": "line"
                    },
                    {
                      "row": {
                        "rows": {
                          "label": "Rows",
                          "type": "int",
                          "min": 1,
                          "max": 100,
                          "default": 5
                        },
                        "columns": {
                          "label": "Cols",
                          "type": "int",
                          "min": 1,
                          "max": 100,
                          "default": 5
                        }
                      },
                      "label": "Grid"
                    },
                    {
                      "row": {
                        "boxHeight": {
                          "label": "Height",
                          "type": "float",
                          "unit": "mm",
                          "min": 1.0,
                          "max": 100.0,
                          "precision": 1,
                          "default": 5.0
                        },
                        "boxWidth": {
                          "label": "Width",
                          "type": "float",
                          "unit": "mm",
                          "min": 1.0,
                          "max": 100.0,
                          "precision": 1,
                          "default": 5.0
                        }
                      },
                      "label": "Box"
                    },
                    {
                      "name": "line",
                      "type": "line"
                    },
                    {
                      "row": {
                        "rowParameter": {
                          "label": "Row",
                          "type": "override",
                          "default": 1
                        },
                        "columnParameter": {
                          "label": "Col",
                          "type": "override",
                          "default": 2
                        }
                      },
                      "label": "Param"
                    },
                    {
                      "row": {
                        "rowMin": {
                          "label": "Min",
                          "type": "float",
                          "min": -1000000.0,
                          "max": 1000000.0,
                          "precision": 2,
                          "default": 100.0
                        },
                        "rowMax": {
                          "label": "Max",
                          "type": "float",
                          "min": -1000000.0,
                          "max": 1000000.0,
                          "precision": 2,
                          "default": 1000.0
                        }
                      },
                      "label": "Row"
                    },
                    {
                      "row": {
                        "columnMin": {
                          "label": "Min",
                          "type": "float",
                          "min": -1000000.0,
                          "max": 1000000.0,
                          "precision": 2,
                          "default": 10.0
                        },
                        "columnMax": {
                          "label": "Max",
                          "type": "float",
                          "min": -1000000.0,
                          "max": 1000000.0,
                          "precision": 2,
                          "default": 80.0
                        }
                      },
                      "label": "Col"
                    },
                    {
                      "name": "line",
                      "type": "line"
                    },
                    {
                      "name": "materialLayer",
                      "label": "Material",
                      "type": "recipe"
                    },
                    {
                      "name": "textLayer",
                      "label": "Text",
                      "type": "recipe"
                    },
                    {
                      "name": "borderLayer",
                      "label": "Border",
                      "type": "recipe"
                    },
                    {
                      "name": "line",
                      "type": "line"
                    },
                    {
                      "row": {
                        "showBorder": {
                          "label": "Border",
                          "type": "bool",
                          "default": true
                        },
                        "showText": {
                          "label": "Text",
                          "type": "bool",
                          "default": true
                        }
                      },
                      "label": "Create"
                    }
                  ]
                      })json"};

      Layer* borderL { nullptr };
      Layer* textL { nullptr };

      QString genRowText(int row) const;
      QString genColText(int col) const;
      double rowValue(int row) const;
      double columnValue(int col) const;
      void updateChildren();
      void addText(double x, double y, const QString& s, Layer* layer, double pt, double rot);

    public:
      MaterialTest(ZCam* zcam, Element* parent = nullptr);
      virtual QString typeName() override { return QStringLiteral("materialtest"); }
      void createChildren();
      Q_INVOKABLE bool nameEditable() const override { return true; }
      virtual const std::string_view properties() const override { return _properties; }
      Q_INVOKABLE virtual bool visible() const override { return true; }
      // Text elements are layer elements and can be dragged on the 3D canvas.
      static constexpr bool s_draggable = true;
      Q_INVOKABLE bool draggable() const override { return s_draggable; }
      // Text elements can be deleted from the project tree.
      static constexpr bool s_deletable = true;
      Q_INVOKABLE bool deletable() const override { return s_deletable; }
      };
