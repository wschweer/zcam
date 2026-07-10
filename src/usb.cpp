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

#include "logger.h"
#include "usb.h"
#include "laserengine.h"
#include <libusb-1.0/libusb.h>

static const int WRITE_ENDPOINT = 0x02;
static const int READ_ENDPOINT  = 0x88;

static bool debugIO = false;

//---------------------------------------------------------
//   libusbError
//---------------------------------------------------------

static void libusbError(int error) {
      Debug("{}", libusb_strerror(error));
      throw libusb_strerror(error);
      }

//---------------------------------------------------------
//   Usb
//---------------------------------------------------------

Usb::Usb() {
      std::srand(std::time(nullptr));
//      Info("libusb api version: {}", LIBUSB_API_VERSION);
      auto rc = libusb_init(&ctx);
      if (rc)
            libusbError(rc);
      }

Usb::~Usb() {
      if (list)
            libusb_free_device_list(list, 1);
      libusb_exit(ctx);
      }

//---------------------------------------------------------
//   lookupDevice
//---------------------------------------------------------

bool Usb::lookupDevice(int vendor, int product) {
      auto size = libusb_get_device_list(ctx, &list);
      for (int i = 0; i < size; ++i) {
            libusb_device_descriptor dd;
            libusb_get_device_descriptor(list[i], &dd);
            if (dd.idVendor == vendor && dd.idProduct == product) {
                  device = list[i];
                  Debug("usb device found bus {} port {}", libusb_get_bus_number(device), libusb_get_port_number(device));
                  return true;
                  }
            }
      throw(std::string("No Fiber Laser found"));
      return false;
      }

//---------------------------------------------------------
//   open
//---------------------------------------------------------

bool Usb::open(bool _mock) {
      mock = _mock;
      if (mock)
            return true;
      int error;
      if ((error = libusb_open(device, &handle)))
            libusbError(error);
      libusb_free_device_list(list, 1);
      list = nullptr;

      libusb_device_descriptor dd;
      libusb_get_device_descriptor(device, &dd);
      Debug("open Device {:4x}:{:4x}", dd.idVendor, dd.idProduct);

      return true;
      }

//---------------------------------------------------------
//   close
//---------------------------------------------------------

bool Usb::close() {
      if (mock)
            return true;
      libusb_close(handle);
      return true;
      }

//---------------------------------------------------------
//   write
//---------------------------------------------------------

bool Usb::write(const u_char* data, size_t count) {
      if (count != 0xc && count != 0xc00)
            Fatal("bad count 0x{:x} expected 0xc or 0xc00", count);
      if (debugIO) {
            Packet6* p = (Packet6*)(data);
            dump(p, count == 0xc);
            }
      if (!mock)
            return readWrite(const_cast<u_char*>(data), count, WRITE_ENDPOINT);
      return true;
      }

//---------------------------------------------------------
//   read
//---------------------------------------------------------

bool Usb::read(u_char* data, size_t count) {
      bool rv = true;
      if (!mock)
            rv = readWrite(data, count, READ_ENDPOINT);
      else
            for (int i = 0; i < count; ++i)
                  data[i] = std::rand();
      if (debugIO) {
            const Packet4& p = *(Packet4*)(data);
            Debug("  {:04x}:{:04x}:{:04x}:{:04x}", p[0], p[1], p[2], p[3]);
            }
      return rv;
      }

//---------------------------------------------------------
//   readWrite
//    synchronous transfers
//---------------------------------------------------------

bool Usb::readWrite(u_char* data, size_t count, int endpoint) {
      int transferred;
      int error = libusb_bulk_transfer(handle, endpoint, data, count, &transferred, timeout);
      if (error) {
            Debug("libusb_bulk_transfer failed");
            libusbError(error);
            }
      if (count != transferred)
            Critical("requested {}, actual transferred {}", count, transferred);
      return true;
      }
