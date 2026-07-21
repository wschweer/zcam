//=============================================================================
//  wcam
//    CAM tool for gcode and fiber laser machines.
//
//  Copyright (C) 2025 Werner Schweer
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENSE.GPL
//=============================================================================

#pragma once

#include "element3d.h"

enum class PPType : char;

//---------------------------------------------------------
//   Rectangle
//---------------------------------------------------------

class Rectangle : public Element3d
      {
      Q_OBJECT

      // Custom size property with lockSize enforcement in set_size().
      Q_PROPERTY(QVector2D size READ size WRITE set_size NOTIFY sizeChanged)

    public:
      QVector2D size() const { return _size; }
      void set_size(QVector2D v);
    Q_SIGNALS:
      void sizeChanged();

    protected:
      QVector2D _size {QVector2D(40, 40)};
      PROPV(int, lockSize, static_cast<int>(LockScaleMode::Off))
      PROPV(double, corner, 0.0)

      inline static constexpr std::string_view _properties {
         R"json({
                  "class": "Rectangle",
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
                      "default": 0
                    },
                    {
                      "name": "line",
                      "type": "line"
                    },
                    {
                      "name": "lineWidth",
                      "label": "LineWidth",
                      "type": "float",
                      "default": 0.5
                    },
                    {
                      "name": "size",
                      "label": "Size",
                      "type": "vector2d",
                      "unit": "mm",
                      "default": [
                        0.0,
                        0.0,
                        0.0
                      ]
                    },
                    {
                      "name": "lockSize",
                      "label": "Lock",
                      "type": "lockSize",
                      "default": 2
                    },
                    {
                      "row": {
                        "joinType": {
                          "label": "Join",
                          "type": "lineJoin",
                          "default": 0
                        },
                        "endType": {
                          "label": "End",
                          "type": "lineEnd",
                          "default": 0
                        }
                      },
                      "label": " "
                    },
                    {
                      "row": {
                        "fill": {
                          "label": "fill",
                          "type": "bool",
                          "default": true
                        },
                        "corner": {
                          "label": "corner",
                          "type": "float",
                          "default": 5.0
                        }
                      },
                      "label": " "
                    }
                  ]
                      })json"};

    public:
      Rectangle(ZCam*, Element* parent = nullptr);
      virtual QString typeName() override { return QStringLiteral("rectangle"); }
      void update(int flags = ~0) override;

      virtual PathList createPath();
      bool isClosed() const;
      QRectF rectangle() const;
      virtual const std::string_view properties() const override { return _properties; }
      Q_INVOKABLE bool nameEditable() const override { return true; }
      Q_INVOKABLE virtual bool visible() const override { return true; }
      Q_INVOKABLE bool draggable() const override { return true; }
      Q_INVOKABLE bool deletable() const override { return true; }
      Q_INVOKABLE bool hasHandles() const override { return true; }
      Q_INVOKABLE int vertexCount() const override { return 4; }
      Q_INVOKABLE bool isVertex(int idx) const override;
      Q_INVOKABLE QVector3D vertexPos(int idx) const override;
      Q_INVOKABLE QVector3D vertexWorldPos(int idx) const override;
      Q_INVOKABLE void setVertexPos(int idx, const QVector3D& pos) override;

    private:
      bool _suppressUpdate {false}; ///< guard to batch updates during setVertexPos
      };
