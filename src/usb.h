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

#include <types.h>

struct libusb_context;
struct libusb_device;
struct libusb_device_handle;

//---------------------------------------------------------
//   Usb
//---------------------------------------------------------

class Usb {
      int timeout { 1000 };      // was 100 (ms)
      libusb_context* ctx          { nullptr };
      libusb_device** list         { nullptr };
      libusb_device* device        { nullptr };
      libusb_device_handle* handle { nullptr };

      bool mock;

      bool readWrite(u_char* data, size_t count, int endpoint);

   public:
      Usb();
      ~Usb();
      bool lookupDevice(int vendor, int product);
      bool open(bool moc);
      bool close();
      bool write(const u_char* data, size_t count);
      bool read(u_char* data, size_t count);
      };
