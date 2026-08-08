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

#include <QImage>
#include <QPointer>
#include <QtQuick3D/qquick3dgeometry.h>
#include <QtQuick3D/qquick3dtexturedata.h>
#include <QtQml/qqmlregistration.h>

#include "element3d.h"
#include "macros.h"

class ImageElement;

//---------------------------------------------------------
//   ImagePlaneGeometry
//    A unit quad (1×1, centered at origin, in the XY plane)
//    with UV coordinates suitable for texture mapping.
//    The quad extends from -0.5 to +0.5 in both X and Y.
//    The element's scale property determines the physical
//    size in mm.
//---------------------------------------------------------

class ImagePlaneGeometry : public QQuick3DGeometry
      {
      Q_OBJECT

    public:
      explicit ImagePlaneGeometry(QQuick3DObject* parent = nullptr);

    private:
      void rebuild();
      };

//---------------------------------------------------------
//   ImageTextureData
//    A QQuick3DTextureData that loads a static image file
//    (PNG, JPEG, BMP, GIF, TIFF, WEBP, ...) and provides
//    it as an RGBA8 texture for QtQuick3D.
//
//    The texture is owned by an ImageElement and reloaded
//    whenever the element's filePath property changes.
//---------------------------------------------------------

class ImageTextureData : public QQuick3DTextureData
      {
      Q_OBJECT

    public:
      explicit ImageTextureData(QQuick3DObject* parent = nullptr);
      ~ImageTextureData() override;

      void loadFromFile(const QString& path);

    private:
      QString _filePath;
      };

//---------------------------------------------------------
//   ImageElement
//    An Element3d that displays a pixel-based image (PNG,
//    JPEG, BMP, ...) on the 3D canvas as a textured quad
//    in the XY plane.
//
//    The element's scale property represents the physical
//    size in mm.  On import, the scale is set so the larger
//    dimension defaults to 100 mm, preserving the image's
//    aspect ratio.  lockScale defaults to Lock mode so
//    resizing preserves the aspect ratio.
//
//    The filePath property stores the absolute or relative
//    path to the image file.  The image is reloaded from
//    disk on project load (similar to BrepElement).
//---------------------------------------------------------

class ImageElement : public Element3d
      {
      Q_OBJECT
      QML_ELEMENT
      QML_UNCREATABLE("ImageElement is created by the Project")

      PROP(QString, filePath)
      PROPV(double, opacity, 1.0)

      Q_PROPERTY(ImageTextureData* textureData READ textureData NOTIFY textureDataChanged)
      Q_PROPERTY(ImagePlaneGeometry* planeGeometry READ planeGeometry CONSTANT)

      inline static constexpr std::string_view _properties {
         R"json({
    "class": "ImageElement",
    "rows": [
        {
            "label": "State",
            "cells": [
                {
                    "type": "bool",
                    "default": true,
                    "name": "show",
                    "sublabel": "Show"
                },
                {
                    "type": "bool",
                    "default": true,
                    "name": "burn",
                    "sublabel": "Burn"
                }
            ]
        },
        {
            "label": "Recipe",
            "cells": [
                {
                    "name": "laserLayer",
                    "type": "laserLayer",
                    "default": ""
                }
            ]
        },
        {
            "label": "File",
            "cells": [
                {
                    "name": "filePath",
                    "type": "string",
                    "sublabel": "Path",
                    "readOnly": true
                }
            ]
        },
        {
            "label": "Pos.",
            "cells": [
                {
                    "name": "pos",
                    "type": "vector3d",
                    "unit": "mm",
                    "default": [0.0, 0.0, 0.0]
                }
            ]
        },
        {
            "label": "Rot.",
            "cells": [
                {
                    "name": "rot",
                    "type": "vector3d",
                    "unit": "°",
                    "min": 0.0,
                    "max": 360,
                    "default": [0.0, 0.0, 0.0]
                }
            ]
        },
        {
            "label": "Scale",
            "cells": [
                {
                    "name": "scale",
                    "type": "scale",
                    "min": 0.001,
                    "max": 10000,
                    "default": [100.0, 100.0, 1.0]
                }
            ]
        },
        {
            "label": "Lock",
            "cells": [
                {
                    "name": "lockScale",
                    "type": "lockScale",
                    "default": 1
                }
            ]
        },
        {
            "label": "Mirror",
            "cells": [
                {
                    "type": "bool",
                    "default": false,
                    "name": "mirrorX",
                    "sublabel": "X"
                },
                {
                    "type": "bool",
                    "default": false,
                    "name": "mirrorY",
                    "sublabel": "Y"
                }
            ]
        },
        {
            "label": "Opacity",
            "cells": [
                {
                    "type": "float",
                    "min": 0.0,
                    "max": 1.0,
                    "precision": 2,
                    "default": 1.0,
                    "name": "opacity"
                }
            ]
        }
    ]
})json"};

      ImageTextureData* _textureData {nullptr};
      ImagePlaneGeometry* _planeGeometry {nullptr};
      bool _imageLoaded {false};

    signals:
      void textureDataChanged();

    public:
      explicit ImageElement(ZCam* zcam, Element* parent = nullptr);
      ~ImageElement() override;

      virtual QString typeName() override { return QStringLiteral("image"); }
      virtual const std::string_view properties() const override { return _properties; }
      Q_INVOKABLE virtual bool visible() const override { return true; }
      Q_INVOKABLE bool draggable() const override { return true; }
      Q_INVOKABLE bool deletable() const override { return true; }
      Q_INVOKABLE bool nameEditable() const override { return true; }

      virtual json toJson() const override;
      virtual void fromJson(const json& data) override;
      virtual void fixup() override;

      ImageTextureData* textureData() const { return _textureData; }
      ImagePlaneGeometry* planeGeometry() const { return _planeGeometry; }

      bool loadFile(const QString& path);
      bool imageLoaded() const { return _imageLoaded; }

      virtual QRectF contentBoundingBox() const override;
      Q_INVOKABLE virtual void update(int flags = -1) override;
      };