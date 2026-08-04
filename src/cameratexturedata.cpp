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

#include "cameratexturedata.h"
#include "cameraelement.h"
#include "logger.h"

#include <QtMultimedia/qcamera.h>
#include <QtMultimedia/qcameradevice.h>
#include <QtMultimedia/qmediacapturesession.h>
#include <QtMultimedia/qmediadevices.h>
#include <QtMultimedia/qvideosink.h>
#include <QtMultimedia/qvideoframe.h>

#include <QImage>
#include <QSize>

//---------------------------------------------------------
//   CameraTextureData
//---------------------------------------------------------

CameraTextureData::CameraTextureData(QQuick3DObject* parent) : QQuick3DTextureData(parent) {
      setFormat(Format::RGBA8);
      setSize(QSize(640, 480));
      }

CameraTextureData::~CameraTextureData() {
      teardownCamera();
      }

//---------------------------------------------------------
//   set_camera
//    Bind to a CameraElement.  The texture then mirrors the
//    element's live video sink.  Passing nullptr restores the
//    fallback mode (own capture from the default device).
//---------------------------------------------------------

void CameraTextureData::set_camera(CameraElement* v) {
      if (v == _cameraElement)
            return;

      if (_sinkConnection)
            QObject::disconnect(_sinkConnection);

      _cameraElement = v;
      emit cameraChanged();

      if (_cameraElement) {
            // The element owns the camera; drop any fallback capture.
            teardownCamera();
            _sinkConnection =
                QObject::connect(_cameraElement->videoSink(), &QVideoSink::videoFrameChanged, this,
                                 [this](const QVideoFrame& frame) { onVideoFrameChanged(frame); });
            }
      else
            setupCamera();
      }

//---------------------------------------------------------
//   setupCamera
//    Fallback mode: find the first available video input
//    device and start capturing frames from it.
//---------------------------------------------------------

void CameraTextureData::setupCamera() {
      if (_camera || _cameraElement)
            return; // already running, or bound to a CameraElement

      const auto inputs = QMediaDevices::videoInputs();
      if (inputs.isEmpty()) {
            Log("CameraTextureData: no video input devices found");
            return;
            }

      const QCameraDevice& dev = inputs.first();
      Log("CameraTextureData: using camera '{}'", dev.description().toStdString());

      _camera  = new QCamera(dev);
      _session = new QMediaCaptureSession;
      _sink    = new QVideoSink;

      _session->setCamera(_camera);
      _session->setVideoSink(_sink);

      QObject::connect(_sink, &QVideoSink::videoFrameChanged,
                       [this](const QVideoFrame& frame) { onVideoFrameChanged(frame); });

      _camera->start();
      }

//---------------------------------------------------------
//   teardownCamera
//---------------------------------------------------------

void CameraTextureData::teardownCamera() {
      if (_camera)
            _camera->stop();
      delete _camera;
      _camera = nullptr;
      delete _session;
      _session = nullptr;
      delete _sink;
      _sink = nullptr;
      }

//---------------------------------------------------------
//   onVideoFrameChanged
//    Convert the incoming QVideoFrame to RGBA8 and update the
//    texture data.
//---------------------------------------------------------

void CameraTextureData::onVideoFrameChanged(const QVideoFrame& frame) {
      if (!frame.isValid())
            return;

      QImage img = frame.toImage();
      if (img.isNull())
            return;

      QImage rgba = img.convertToFormat(QImage::Format_RGBA8888);

      if (rgba.size() != size())
            setSize(rgba.size());

      setTextureData(
          QByteArray::fromRawData(reinterpret_cast<const char*>(rgba.constBits()), rgba.sizeInBytes()));

      emit textureDataNodeDirty();
      }
