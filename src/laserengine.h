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

class ZCam;
class Laser;

class LaserEngine {

   public:
      LaserEngine(ZCam*, Laser*) {}
      bool init(bool) { return true; }
      void exit() {}
      void startMarking() {}
      void endMarking() {}
      void startFraming() {}
      void stopFraming() {}
      void stop() {}
      };
