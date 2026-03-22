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
#include <QObject>
#include <QVector3D>
#include <QtQml/qqmlregistration.h>
#include "logger.h"

class QSocketNotifier;

//---------------------------------------------------------
//   HidDeviceInfo
//---------------------------------------------------------

class HidDeviceInfo {
   public:
      std::string path;               // Platform-specific device path
      unsigned short vendorId;        // Device Vendor ID
      unsigned short productId;       // Device Product ID
      unsigned short release_number;  // Device Release Number in binary-coded decimal, also known as Device Version Number
      std::string manufacturer;       // Manufacturer String
      std::string product;            // Product string
      int interface_number;           // The USB interface which this logical device represents.
      };

using HidDeviceList = std::list<HidDeviceInfo*>;

//---------------------------------------------------------
//   HidDevice
//---------------------------------------------------------

class HidDevice : public QObject {
      Q_OBJECT

      static HidDeviceList devices;

      QSocketNotifier* notifier;
      int handle { -1 };

      void enumerate();

   protected:
      HidDeviceInfo deviceInfo;

      bool open();
      virtual void processData(int n, const unsigned char* data) {}

   public:
      HidDevice(QObject* parent = nullptr);
      bool open(unsigned short vendor, unsigned short product);
      HidDeviceList enumerate(unsigned short int, unsigned short int);
      };

//---------------------------------------------------------
//   SpaceMouse
//---------------------------------------------------------

class SpaceMouse : public HidDevice {
      Q_OBJECT
      QML_ELEMENT

      double translationDeadzone { 0.1 };
      double rotationDeadzone    { 0.1 };
      double scale               { 0.0035 };
      bool processTranslate(const unsigned char* packet);
      bool processRotate(const unsigned char* packet);
      bool processButtons(int n, const unsigned char* packet);
      double convertInput(int lsb, int msb, double deadzone);
      void processData(int n, const unsigned char* data) override;

   signals:
      void translate(QVector3D);
      void rotate(QVector3D);
      void buttonPressed(int);

   public:
      SpaceMouse(QObject* parent = nullptr) : HidDevice(parent) { init(); }
      Q_INVOKABLE bool init();                  // return true if spacemouse is found and operational
      };
