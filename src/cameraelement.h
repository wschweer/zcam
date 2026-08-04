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

#include <QImage>
#include <QtQml/qqmlregistration.h>
#include <nlohmann/json.hpp>

#include "element3d.h"
#include "macros.h"

QT_BEGIN_NAMESPACE
class QCamera;
class QMediaCaptureSession;
class QVideoSink;
class QVideoFrame;
QT_END_NAMESPACE

//---------------------------------------------------------
//   CameraElement
//    A project element that manages an attached Linux webcam
//    (V4L2 / Qt Multimedia).  The camera name is selected from
//    the list of available video input devices (combobox in the
//    inspector).  The element drives the camera overlay in the
//    XY plane of the 3D viewport: pos/size/rot position and
//    scale the overlay, and trapezX/trapezY shear it for
//    trapezoid (keystone) correction.
//
//    The live image is shown in the inspector (cameraView
//    property type) where it can be zoomed (mouse wheel) and
//    panned (drag).
//---------------------------------------------------------

class CameraElement : public Element3d
      {
      Q_OBJECT
      QML_ELEMENT
      QML_UNCREATABLE("CameraElement is created by the Project")

      // name of the camera device, selected from cameraNames()
      PROP(QString, cameraName)
      // overlay width/height in machine (mm) space
      PROPV(QVector2D, overlaySize, QVector2D(100.0, 100.0))
      // trapezoid (keystone) correction, -1 .. 1
      PROPV(double, trapezX, 0.0)
      PROPV(double, trapezY, 0.0)
      // overlay opacity, 0.0 (fully transparent) .. 1.0 (fully opaque)
      PROPV(double, opacity, 1.0)
      // V4L2 device brightness, -1.0 .. 1.0, 0.0 = default
      PROPV(double, brightness, 0.0)
      // selected camera resolution, e.g. "1920x1080", or empty for default
      PROPV(QString, resolution, "")
      // selected frame rate, e.g. "30/1", or empty for default
      PROPV(QString, frameRate, "")
      // current frame size, or invalid when no camera is active
      Q_PROPERTY(QSize frameSize READ frameSize NOTIFY frameSizeChanged)
      // live frame sink; connected to VideoOutput / CameraTextureData
      // (typed QObject* so the meta-object compiler does not need the
      // complete QVideoSink type in this header)
      Q_PROPERTY(QObject* videoSink READ videoSinkObject CONSTANT)

      QVideoSink* _sink {nullptr};
      QCamera* _camera {nullptr};
      QMediaCaptureSession* _session {nullptr};
      QSize _frameSize;
      ZCam* _zcam {nullptr};

      inline static constexpr std::string_view _properties {
         R"json({
                  "class": "CameraElement",
                  "rows": [
                    {
                      "label": "Visibility",
                      "cells": [
                        {
                          "type": "bool",
                          "default": true,
                          "name": "show",
                          "sublabel": "Show"
                        }
                      ]
                    },
                    {
                      "label": "Camera",
                      "cells": [
                        {
                          "name": "cameraName",
                          "type": "cameraName"
                        }
                      ]
                    },
                    {
                      "label": "Position",
                      "cells": [
                        {
                          "name": "pos",
                          "type": "vector3d",
                          "unit": "mm"
                        }
                      ]
                    },
                    {
                      "label": "Size",
                      "cells": [
                        {
                          "name": "overlaySize",
                          "type": "vector2d",
                          "unit": "mm",
                          "min": 1.0,
                          "max": 10000.0,
                          "default": [
                            100.0,
                            100.0
                          ]
                        }
                      ]
                    },
                    {
                      "label": "Rotation",
                      "cells": [
                        {
                          "name": "rot",
                          "type": "vector3d",
                          "unit": "°",
                          "min": -360.0,
                          "max": 360.0,
                          "default": [
                            0.0,
                            0.0,
                            0.0
                          ]
                        }
                      ]
                    },
                    {
                      "label": "Trapez",
                      "cells": [
                        {
                          "type": "float",
                          "min": -1.0,
                          "max": 1.0,
                          "precision": 3,
                          "default": 0.0,
                          "name": "trapezX",
                          "sublabel": "X"
                        },
                        {
                          "type": "float",
                          "min": -1.0,
                          "max": 1.0,
                          "precision": 3,
                          "default": 0.0,
                          "name": "trapezY",
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
                    },
                    {
                      "label": "Brightness",
                      "cells": [
                        {
                          "type": "float",
                          "min": -1.0,
                          "max": 1.0,
                          "precision": 2,
                          "default": 0.0,
                          "name": "brightness"
                        }
                      ]
                    },
                    {
                      "label": "Resolution",
                      "cells": [
                        {
                          "name": "resolution",
                          "type": "cameraResolution"
                        }
                      ]
                    },
                    {
                      "label": "Frame Rate",
                      "cells": [
                        {
                          "name": "frameRate",
                          "type": "cameraFrameRate"
                        }
                      ]
                    }
                  ]
                      })json"};

    signals:
      void frameChanged();
      void frameSizeChanged();

    public:
      explicit CameraElement(ZCam*, Element* parent = nullptr);
      ~CameraElement() override;
      virtual QString typeName() override { return QStringLiteral("cameraElement"); }
      virtual const std::string_view properties() const override { return _properties; }
      virtual bool saveChildren() const override { return true; }
      Q_INVOKABLE virtual bool visible() const override { return true; }
      Q_INVOKABLE bool deletable() const override { return true; }
      virtual void fromJson(const json&) override;
      virtual void fixup() override;

      /// List of available camera device descriptions (for the combobox).
      QStringList cameraNames() const;
      /// List of available resolutions for the current camera, e.g. "1920x1080".
      Q_INVOKABLE QStringList resolutionNames() const;
      /// List of available frame rates for the current camera + resolution,
      /// e.g. "30/1".
      Q_INVOKABLE QStringList frameRateNames() const;
      /// Live frame sink; the inspector's cameraView delegate and the
      /// CameraTextureData connect to this for live images.  Never null.
      QVideoSink* videoSink() const { return _sink; }
      /// Same as videoSink(), but returns QObject* for the meta-object
      /// system (avoids needing the complete QVideoSink type here).
      QObject* videoSinkObject() const;
      QSize frameSize() const { return _frameSize; }
      /// Force a camera start/stop to match the show/visibility state.
      void updateActiveState();

    private:
      void startCamera();
      void stopCamera();
      void restartCamera();
      void applyBrightness();
      void onVideoFrameChanged(const QVideoFrame& frame);
      };
