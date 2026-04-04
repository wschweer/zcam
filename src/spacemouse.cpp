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

#include <dlfcn.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <unistd.h>
#include <QSocketNotifier>
#include <bitset>
#include "logger.h"

/* Linux */
#include <linux/input.h>
#include <linux/version.h>

#include "spacemouse.h"
#include <spnav.h>

//---------------------------------------------------------
//   init
//---------------------------------------------------------

bool SpaceMouse::init() {
      if (spnav_open() == -1) {
            Debug("3DConnexion device cannot be opened");
            return false;
            }
      int fd   = spnav_fd();
      notifier = new QSocketNotifier(QSocketNotifier::Read, this);
      notifier->setSocket(fd);
      connect(notifier, &QSocketNotifier::activated, [this]() {
            spnav_event ev;
            float scale = 0.002;
            if (spnav_poll_event(&ev)) {
                  // Translation (x, y, z) und Rotation (rx, ry, rz)
                  // Wertebereich liegt typischerweise zwischen -350 und +350
                  if (ev.type == SPNAV_EVENT_MOTION) {
                        emit translate({ev.motion.x * scale, ev.motion.z * -scale, ev.motion.y * -scale});
                        emit rotate({ev.motion.rx * -scale, ev.motion.rz * scale, ev.motion.ry * -scale});
                        }
                  else if (ev.type == SPNAV_EVENT_BUTTON)
                        Debug("SpaceMouse Button pressed");
                  }
            });
      notifier->setEnabled(true);
      return true;
      }

//---------------------------------------------------------
//   SpaceMouse
//---------------------------------------------------------

SpaceMouse::~SpaceMouse() {
      spnav_close();
      }

//---------------------------------------------------------
//   SpaceMouse
//---------------------------------------------------------

SpaceMouse::SpaceMouse(QObject* parent) : QObject(parent) {
      init();
      }
