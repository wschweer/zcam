//=============================================================================
//  ZCam - manufacturing tool for G-code machines and Fiber Laser
//
//  Copyright (C) 2026 Werner Schweer
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENSE.GPL
//=============================================================================

#pragma once

#include <QtQuick3D/qquick3dtexturedata.h>

#include <QtQml/qqmlregistration.h>
#include <QPointer>

QT_BEGIN_NAMESPACE
class QCamera;
class QMediaCaptureSession;
class QVideoSink;
class QVideoFrame;
QT_END_NAMESPACE

class CameraElement;

//---------------------------------------------------------
//   CameraTextureData
//    A QQuick3DTextureData subclass that provides live video
//    frames as an RGBA8 texture for use in QtQuick3D.
//
//    When a CameraElement is bound (camera property), the
//    texture mirrors the element's live video sink — the
//    CameraElement manages the actual camera device and its
//    settings (device selection, trapezoid correction, ...).
//
//    Without a CameraElement it falls back to capturing frames
//    from the default Linux video camera (V4L2).
//
//    Usage in QML:
//      Texture {
//          textureData: CameraTextureData {
//              camera: ZCam.project.cameraElement
//          }
//      }
//---------------------------------------------------------

class CameraTextureData : public QQuick3DTextureData
      {
      Q_OBJECT
      QML_ELEMENT

      Q_PROPERTY(CameraElement* camera READ camera WRITE set_camera NOTIFY cameraChanged)

    public:
      explicit CameraTextureData(QQuick3DObject* parent = nullptr);
      ~CameraTextureData() override;

      CameraElement* camera() const { return _cameraElement; }
      void set_camera(CameraElement* v);

    signals:
      void cameraChanged();

    private:
      void setupCamera();
      void teardownCamera();
      void onVideoFrameChanged(const QVideoFrame& frame);

      QPointer<CameraElement> _cameraElement;
      QMetaObject::Connection _sinkConnection;

      // Fallback: own capture when no CameraElement is bound.
      QCamera* _camera {nullptr};
      QMediaCaptureSession* _session {nullptr};
      QVideoSink* _sink {nullptr};
      };
