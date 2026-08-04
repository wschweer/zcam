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

#include "cameraelement.h"
#include "project.h"
#include "zcam.h"
#include "logger.h"

#include <QtMultimedia/qcamera.h>
#include <QtMultimedia/qcameradevice.h>
#include <QtMultimedia/qmediacapturesession.h>
#include <QtMultimedia/qmediadevices.h>
#include <QtMultimedia/qvideosink.h>
#include <QtMultimedia/qvideoframe.h>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <unistd.h>
#include <cmath>
#include <QSet>

//---------------------------------------------------------
//   CameraElement
//---------------------------------------------------------

CameraElement::CameraElement(ZCam* z, Element* parent) : Element3d(z, parent), _zcam(z) {
      setName(QStringLiteral("camera"));

      _sink = new QVideoSink(this);
      connect(_sink, &QVideoSink::videoFrameChanged, this,
              [this](const QVideoFrame& frame) { onVideoFrameChanged(frame); });

      // The show flag gates the camera capture: hidden element → camera off.
      connect(this, &CameraElement::showChanged, this, [this] { updateActiveState(); });

      // Switching the device restarts the capture.
      connect(this, &CameraElement::cameraNameChanged, this, [this] {
            if (!_camera)
                  return;
            stopCamera();
            startCamera();
            });

      // Applying brightness requires the camera device to be open.
      connect(this, &CameraElement::brightnessChanged, this, [this] { applyBrightness(); });

      // Changing resolution or frame rate requires a camera restart.
      connect(this, &CameraElement::resolutionChanged, this, [this] {
            if (_camera)
                  restartCamera();
            });
      connect(this, &CameraElement::frameRateChanged, this, [this] {
            if (_camera)
                  restartCamera();
            });
      }

CameraElement::~CameraElement() {
      stopCamera();
      }

//---------------------------------------------------------
//   videoSinkObject
//    QVideoSink is a QObject, but it is only forward-declared
//    in the header, so the implicit upcast cannot be inlined.
//---------------------------------------------------------

QObject* CameraElement::videoSinkObject() const {
      return _sink;
      }

//---------------------------------------------------------
//   cameraNames
//    Return the descriptions of all available video input
//    devices.  These are the strings stored in cameraName.
//---------------------------------------------------------

QStringList CameraElement::cameraNames() const {
      QStringList names;
      const auto inputs = QMediaDevices::videoInputs();
      for (const auto& dev : inputs)
            names << dev.description();
      return names;
      }

//---------------------------------------------------------
//   startCamera / stopCamera
//---------------------------------------------------------

void CameraElement::startCamera() {
      if (_camera)
            return; // already running
      const auto inputs = QMediaDevices::videoInputs();
      if (inputs.isEmpty()) {
            Log("CameraElement: no video input devices found");
            return;
            }

      // Select the device by description; fall back to the default.
      QCameraDevice dev = QMediaDevices::defaultVideoInput();
      QString wanted    = cameraName();
      bool found        = false;
      for (const auto& d : inputs) {
            if (!wanted.isEmpty() && d.description() == wanted) {
                  dev   = d;
                  found = true;
                  break;
                  }
            }
      if (!wanted.isEmpty() && !found)
            Warning("CameraElement: camera '{}' not found, using default", wanted);

      Log("CameraElement: starting camera '{}'", dev.description());

      _session = new QMediaCaptureSession(this);
      _camera  = new QCamera(dev, this);

      // Apply resolution if set.
      QString res = resolution();
      if (!res.isEmpty()) {
            int xSep = res.indexOf('x');
            if (xSep > 0) {
                  int w = res.first(xSep).toInt();
                  int h = res.sliced(xSep + 1).toInt();
                  if (w > 0 && h > 0) {
                        // Find a matching QCameraFormat for the requested resolution.
                        const QCameraFormat* bestFmt = nullptr;
                        // Parse requested frame rate if set.
                        float wantFps = -1.0f;
                        QString fr    = frameRate();
                        if (!fr.isEmpty()) {
                              int slashPos = fr.indexOf('/');
                              if (slashPos > 0) {
                                    int num = fr.first(slashPos).toInt();
                                    int den = fr.sliced(slashPos + 1).toInt();
                                    if (den > 0 && num > 0)
                                          wantFps = float(num) / float(den);
                                    }
                              }
                        float bestDiff = 1e9f;
                        for (const auto& fmt : dev.videoFormats()) {
                              if (fmt.resolution().width() != w || fmt.resolution().height() != h)
                                    continue;
                              if (wantFps > 0.0f) {
                                    float fps  = float(fmt.minFrameRate());
                                    float diff = std::abs(fps - wantFps);
                                    if (diff < bestDiff) {
                                          bestDiff = diff;
                                          bestFmt  = &fmt;
                                          }
                                    }
                              else {
                                    bestFmt = &fmt;
                                    break;
                                    }
                              }
                        if (bestFmt)
                              _camera->setCameraFormat(*bestFmt);
                        }
                  }
            }

      _session->setCamera(_camera);
      _session->setVideoSink(_sink);
      _camera->start();

      // Apply V4L2 brightness after the camera is started (device is open).
      applyBrightness();
      }

void CameraElement::stopCamera() {
      if (_camera)
            _camera->stop();
      delete _camera;
      _camera = nullptr;
      delete _session;
      _session = nullptr;
      if (!_frameSize.isEmpty()) {
            _frameSize = QSize();
            emit frameSizeChanged();
            }
      }

//---------------------------------------------------------
//   updateActiveState
//    Start or stop the camera so it matches the visibility
//    state (show && ancestorsShow).
//---------------------------------------------------------

void CameraElement::updateActiveState() {
      if (show() && ancestorsShow())
            startCamera();
      else
            stopCamera();
      }

//---------------------------------------------------------
//   onVideoFrameChanged
//---------------------------------------------------------

void CameraElement::onVideoFrameChanged(const QVideoFrame& frame) {
      if (!frame.isValid())
            return;
      QSize s = frame.toImage().size();
      if (s != _frameSize) {
            _frameSize = s;
            emit frameSizeChanged();
            }
      emit frameChanged();
      }

//---------------------------------------------------------
//   fromJson
//---------------------------------------------------------

void CameraElement::fromJson(const json& data) {
      Element3d::fromJson(data);
      // The camera is started in fixup(), after the whole project
      // tree has been loaded.
      }

//---------------------------------------------------------
//   fixup
//    Called after the project is fully loaded.  Connect to
//    cameraName changes (device switch) and start the camera
//    if the element is visible.
//---------------------------------------------------------

void CameraElement::fixup() {
      Element3d::fixup();
      updateActiveState();
      }

//---------------------------------------------------------
//   resolutionNames
//    Return a list of available resolutions for the currently
//    selected camera, formatted as "WxH".
//---------------------------------------------------------

QStringList CameraElement::resolutionNames() const {
      QStringList list;
      // Find the current camera device.
      const auto inputs = QMediaDevices::videoInputs();
      QCameraDevice dev = QMediaDevices::defaultVideoInput();
      QString wanted    = cameraName();
      for (const auto& d : inputs) {
            if (!wanted.isEmpty() && d.description() == wanted) {
                  dev = d;
                  break;
                  }
            }
      QSet<QSize> seen;
      for (const auto& fmt : dev.videoFormats()) {
            QSize r = fmt.resolution();
            if (!seen.contains(r)) {
                  seen.insert(r);
                  list.append(QStringLiteral("%1x%2").arg(r.width()).arg(r.height()));
                  }
            }
      return list;
      }

//---------------------------------------------------------
//   frameRateNames
//    Return a list of available frame rates for the current
//    camera and selected resolution, formatted as "num/den".
//---------------------------------------------------------

QStringList CameraElement::frameRateNames() const {
      QStringList list;
      const auto inputs = QMediaDevices::videoInputs();
      QCameraDevice dev = QMediaDevices::defaultVideoInput();
      QString wanted    = cameraName();
      for (const auto& d : inputs) {
            if (!wanted.isEmpty() && d.description() == wanted) {
                  dev = d;
                  break;
                  }
            }
      // Determine target resolution.
      QSize targetRes;
      QString res = resolution();
      if (!res.isEmpty()) {
            int xSep = res.indexOf('x');
            if (xSep > 0)
                  targetRes = QSize(res.first(xSep).toInt(), res.sliced(xSep + 1).toInt());
            }
      QSet<float> seen;
      for (const auto& fmt : dev.videoFormats()) {
            if (!targetRes.isEmpty() && fmt.resolution() != targetRes)
                  continue;
            float fps = fmt.minFrameRate();
            if (!seen.contains(fps)) {
                  seen.insert(fps);
                  // Format as a fraction.  Common denominators are 1 or 1001.
                  // Use "num/den" format.  For typical integer fps, use N/1.
                  int num = int(std::round(fps));
                  if (std::abs(fps - float(num)) < 0.01f)
                        list.append(QStringLiteral("%1/1").arg(num));
                  else
                        list.append(QStringLiteral("%1/%2").arg(int(std::round(fps * 1001))).arg(1001));
                  }
            }
      return list;
      }

//---------------------------------------------------------
//   applyBrightness
//    Apply the brightness property to the V4L2 device via ioctl.
//    The brightness property is normalized to -1.0..1.0; we map
//    it to the V4L2 brightness control range (typically 0..255,
//    default 128).
//---------------------------------------------------------

void CameraElement::applyBrightness() {
      if (!_camera)
            return;
      // Find the /dev/videoN device path.
      QString name = cameraName();
      if (name.isEmpty())
            return;
      // Iterate /dev/video* and match by description.
      for (int i = 0; i < 64; ++i) {
            QString path = QStringLiteral("/dev/video%1").arg(i);
            int fd       = ::open(path.toUtf8().constData(), O_RDWR);
            if (fd < 0)
                  continue;
            struct v4l2_capability cap {};
            if (ioctl(fd, VIDIOC_QUERYCAP, &cap) >= 0) {
                  QString card(reinterpret_cast<const char*>(cap.card));
                  if (card == name) {
                        // Query the brightness control.
                        struct v4l2_queryctrl qctrl {};
                        qctrl.id = V4L2_CID_BRIGHTNESS;
                        if (ioctl(fd, VIDIOC_QUERYCTRL, &qctrl) >= 0) {
                              // Map normalized -1.0..1.0 to qctrl.minimum..qctrl.maximum.
                              double b    = brightness();
                              int32_t val = int32_t(qctrl.default_value +
                                                    b * (b >= 0 ? (qctrl.maximum - qctrl.default_value)
                                                                : (qctrl.default_value - qctrl.minimum)));
                              struct v4l2_control ctrl {};
                              ctrl.id    = V4L2_CID_BRIGHTNESS;
                              ctrl.value = val;
                              ioctl(fd, VIDIOC_S_CTRL, &ctrl);
                              }
                        ::close(fd);
                        return;
                        }
                  }
            ::close(fd);
            }
      }

//---------------------------------------------------------
//   restartCamera
//    Restart the camera to apply resolution/frame rate changes.
//---------------------------------------------------------

void CameraElement::restartCamera() {
      stopCamera();
      startCamera();
      }