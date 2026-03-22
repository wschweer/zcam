//=============================================================================
//  wcam
//
//  Copyright (C) 2025 Werner Schweer
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENSE.GPL
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
#include "libudev.h"
#include "logger.h"

/* Linux */
#include <linux/hidraw.h>
#include <linux/input.h>
#include <linux/version.h>

#include "spacemouse.h"

HidDeviceList HidDevice::devices;

static const std::vector<int> _3DCONNEXION_VENDORS = {
      0x046d, // LOGITECH = 1133 // Logitech (3Dconnexion is made by Logitech)
      0x256F  // 3DCONNECTION = 9583 // 3Dconnexion
      };

static const std::vector<int> _3DCONNEXION_DEVICES = {
      0xc603, /* 50691 spacemouse plus XT */
      0xc605, /* 50693 cadman */
      0xc606, /* 50694 spacemouse classic */
      0xc621, /* 50721 spaceball 5000 */
      0xc623, /* 50723 space traveller */
      0xc625, /* 50725 space pilot */
      0xc626, /* 50726 space navigator *TESTED* */
      0xc627, /* 50727 space explorer */
      0xc628, /* 50728 space navigator for notebooks*/
      0xc629, /* 50729 space pilot pro*/
      0xc62b, /* 50731 space mouse pro*/
      0xc62e, /* 50734 spacemouse wireless (USB cable) *TESTED* */
      0xc62f, /* 50735 spacemouse wireless receiver */
      0xc631, /* 50737 spacemouse pro wireless *TESTED* */
      0xc632, /* 50738 spacemouse pro wireless receiver */
      0xc633, /* 50739 spacemouse enterprise */
      0xc635, /* 50741 spacemouse compact *TESTED* */
      0xc636, /* 50742 spacemouse module */
      0xc640, /* 50752 nulooq */
      0xc652, /* 50770 3Dconnexion universal receiver *TESTED* */
      };

struct DeviceIds {
      unsigned short vendor;
      unsigned short product;
      bool operator<(const DeviceIds& d) const { return d.vendor < vendor || d.product < product; }
      };

typedef std::vector<std::string> DevicePathList;
typedef std::map<DeviceIds, DevicePathList> Mice;

//---------------------------------------------------------
//   parse_uevent_info
//
// The caller is responsible for free()ing the (newly-allocated) character
// strings pointed to by serial_number_utf8 and product_name_utf8 after use.
//---------------------------------------------------------

static int parse_uevent_info(const char* uevent, int* bus_type, unsigned short* vendor_id, unsigned short* product_id, char** serial_number_utf8, char** product_name_utf8)
      {
      char* tmp = strdup(uevent);
      char* saveptr = NULL;
      char* line;
      char* key;
      char* value;

      int found_id = 0;
      int found_serial = 0;
      int found_name = 0;

      line = strtok_r(tmp, "\n", &saveptr);
      while (line != NULL) {
            /* line: "KEY=value" */
            key = line;
            value = strchr(line, '=');
            if (!value) {
                  goto next_line;
                  }
            *value = '\0';
            value++;

            if (strcmp(key, "HID_ID") == 0) {
                  /**
                   *        type vendor   product
                   * HID_ID=0003:000005AC:00008242
                   **/
                  int ret = sscanf(value, "%x:%hx:%hx", bus_type, vendor_id, product_id);
                  if (ret == 3) {
                        found_id = 1;
                        }
                  }
            else if (strcmp(key, "HID_NAME") == 0) {
                  /* The caller has to free the product name */
                  *product_name_utf8 = strdup(value);
                  found_name = 1;
                  }
            else if (strcmp(key, "HID_UNIQ") == 0) {
                  /* The caller has to free the serial number */
                  *serial_number_utf8 = strdup(value);
                  found_serial = 1;
                  }

      next_line:
            line = strtok_r(NULL, "\n", &saveptr);
            }

      free(tmp);
      return (found_id && found_name && found_serial);
      }

//---------------------------------------------------------
//   HidDevice
//---------------------------------------------------------

HidDevice::HidDevice(QObject* parent)
   : QObject(parent)
      {
      notifier = new QSocketNotifier(QSocketNotifier::Read, this);
      connect(notifier, &QSocketNotifier::activated, [this]() {
            unsigned char buffer[128];
            int n = read(handle, buffer, 128);
            if (n == 7 || n == 13 || ((n == 3) && buffer[0] == 3))
                  processData(n, buffer);
            else
                  Debug("unknown spacemouse data packet");
            });
      }

//---------------------------------------------------------
//   convert_input
//    Convert a signed 16bit word from a 3DConnexion mouse
//    HID packet into a double coordinate, apply a dead zone.
//---------------------------------------------------------

double SpaceMouse::convertInput(int lsb, int msb, double deadzone)
      {
      signed short value = lsb | (msb << 8);
      double ret = (double)value * scale;
      return (std::abs(ret) > deadzone) ? ret : 0.0;
      }

//---------------------------------------------------------
//   processRotate
//---------------------------------------------------------

bool SpaceMouse::processRotate(const unsigned char* packet)
      {
      QVector3D rotation(
            -convertInput(packet[0], packet[1], rotationDeadzone),
            -convertInput(packet[2], packet[3], rotationDeadzone),
            convertInput(packet[4], packet[5], rotationDeadzone));

      if (!rotation.isNull()) {
            emit rotate(rotation);
            return true;
            }
      return false;
      }

//---------------------------------------------------------
//   processTranslate
//---------------------------------------------------------

bool SpaceMouse::processTranslate(const unsigned char* packet)
      {
      double deadzone = translationDeadzone;
      QVector3D translation(
            convertInput(packet[1], packet[2], deadzone),
            convertInput(packet[3], packet[4],  deadzone),
            convertInput(packet[5], packet[6],  deadzone));
      if (!translation.isNull()) {
            emit translate(translation);
            return true;
            }
      return false;
      }

//---------------------------------------------------------
//   processButtons
//---------------------------------------------------------

bool SpaceMouse::processButtons(int packet_size, const unsigned char* packet)
      {
      unsigned int data = 0;
      for (unsigned int i = 1; i < packet_size; ++i) {
            data |= packet[i] << 8 * (i - 1);
            }
      const std::bitset<32> data_bits{data};
      for (size_t i = 0; i < data_bits.size(); ++i) {
            if (data_bits.test(i)) {
                  emit buttonPressed(i);
                  return true;
                  }
            }

      return false;
      }

//---------------------------------------------------------
//   processData
//---------------------------------------------------------

void SpaceMouse::processData(int n, const unsigned char* packet)
      {
      switch (packet[0]) {
            case 1: { // Translation + Rotation
                  processTranslate(packet);
                  if (n == 13)
                        processRotate(packet + 7);
                  break;
                  }
            case 2:     // Rotation
                  processRotate(packet+1);
                  break;
            case 3: { // Button
                  processButtons(n, packet);
                  break;
                  }
            case 23:          // Battery charge
            default:
                  Critical("spacemouse packet {}", packet[0]);
                  break;
            }
      }

//---------------------------------------------------------
//   enumerate
//---------------------------------------------------------

HidDeviceList HidDevice::enumerate(unsigned short vendor, unsigned short product)
      {
      if (devices.empty())
            enumerate();
      HidDeviceList hdl;
      for (auto d : devices) {
            if ((vendor == 0 || vendor == d->vendorId) && (product == 0 || product == d->productId))
                  hdl.push_back(d);
            }
      return hdl;
      }

//---------------------------------------------------------
//   enumerate
//---------------------------------------------------------

void HidDevice::enumerate()
      {
      struct udev* udev = udev_new();
      if (!udev) {
            Debug("Can't create udev\n");
            return;
            }
      struct udev_enumerate* enumerate = udev_enumerate_new(udev);
      udev_enumerate_add_match_subsystem(enumerate, "hidraw");
      udev_enumerate_scan_devices(enumerate);
      struct udev_list_entry* devices = udev_enumerate_get_list_entry(enumerate);

      /* For each item, see if it matches the vid/pid, and if so
         create a udev_device record for it */
      struct udev_list_entry *dev_list_entry;
      for (dev_list_entry = devices; dev_list_entry; dev_list_entry = udev_list_entry_get_next(dev_list_entry)) {
            const char* sysfs_path;
            const char* dev_path;
            const char* str;
            struct udev_device* raw_dev;  /* The device's hidraw udev node. */
            struct udev_device* hid_dev;  /* The device's HID udev node. */
            struct udev_device* usb_dev;  /* The device's USB udev node. */
            struct udev_device* intf_dev; /* The device's interface (in the USB sense). */
            unsigned short dev_vid;
            unsigned short dev_pid;
            char* serial_number_utf8 = nullptr;
            char* product_name_utf8 = nullptr;
            int bus_type;

            /* Get the filename of the /sys entry for the device
               and create a udev_device object (dev) representing it */

            sysfs_path = udev_list_entry_get_name(dev_list_entry);
            raw_dev    = udev_device_new_from_syspath(udev, sysfs_path);
            dev_path   = udev_device_get_devnode(raw_dev);

            hid_dev = udev_device_get_parent_with_subsystem_devtype(raw_dev, "hid", NULL);

            int result = 0;
            if (hid_dev)
                  result = parse_uevent_info(udev_device_get_sysattr_value(hid_dev, "uevent"),
                     &bus_type, &dev_vid, &dev_pid, &serial_number_utf8, &product_name_utf8);

            if (hid_dev && result && (bus_type == BUS_USB || bus_type == BUS_BLUETOOTH)) {
                  /* Check the VID/PID against the arguments */
                  HidDeviceInfo* dev = new HidDeviceInfo;
                  HidDevice::devices.push_back(dev);

                  dev->path           = std::string(dev_path);
                  dev->vendorId       = dev_vid;
                  dev->productId      = dev_pid;
                  dev->release_number = 0x0;
                  dev->interface_number = -1;

                  if (bus_type == BUS_USB) {
                        /* The device pointed to by raw_dev contains information about
                           the hidraw device. In order to get information about the
                           USB device, get the parent device with the
                           subsystem/devtype pair of "usb"/"usb_device". This will
                           be several levels up the tree, but the function will find
                           it. */
                        usb_dev = udev_device_get_parent_with_subsystem_devtype(raw_dev, "usb", "usb_device");
                        if (!usb_dev) {
                              delete dev;
                              HidDevice::devices.pop_back();
                              }
                        else {
                              /* Manufacturer and Product strings */
                              auto manufacturer = udev_device_get_sysattr_value(usb_dev, "manufacturer");
                              auto product      = udev_device_get_sysattr_value(usb_dev, "product");
                              dev->manufacturer = std::string(manufacturer ? manufacturer : "");
                              dev->product      = std::string(product ? product : "");

                              /* Release Number */
                              str = udev_device_get_sysattr_value(usb_dev, "bcdDevice");
                              dev->release_number = (str) ? strtol(str, NULL, 16) : 0x0;

                              /* Get a handle to the interface's udev node. */
                              intf_dev = udev_device_get_parent_with_subsystem_devtype(raw_dev, "usb", "usb_interface");
                              if (intf_dev) {
                                    str = udev_device_get_sysattr_value(intf_dev, "bInterfaceNumber");
                                    dev->interface_number = (str) ? strtol(str, NULL, 16) : -1;
                                    }
                              }
                        }
                  else if (bus_type == BUS_BLUETOOTH) {
                        /* Manufacturer and Product strings */
                        dev->manufacturer = strdup("");
                        dev->product      = strdup(product_name_utf8);
                        }
                  else {
                        /* Unknown device type - this should never happen, as we
                        * check for USB and Bluetooth devices above */
                        }
                  }
            free(serial_number_utf8);
            free(product_name_utf8);
            udev_device_unref(raw_dev);
            }
      udev_enumerate_unref(enumerate);
      udev_unref(udev);
      }

//---------------------------------------------------------
//   open
//---------------------------------------------------------

bool HidDevice::open(unsigned short vendor, unsigned short product)
      {
      const std::string path;

      auto devs = enumerate(vendor, product);
      if (devs.empty()) {
            Debug("no mice");
            return false;
            }
      deviceInfo = *devs.front();
      if (!deviceInfo.path.empty())
            return open();
      return false;
      }

bool HidDevice::open()
      {
      handle = ::open(deviceInfo.path.c_str(), O_RDWR);
      if (handle > 0) {
            /* Get the report descriptor */
            int res, desc_size = 0;
            struct hidraw_report_descriptor rpt_desc;

            memset(&rpt_desc, 0x0, sizeof(rpt_desc));

            /* Get Report Descriptor Size */
            res = ioctl(handle, HIDIOCGRDESCSIZE, &desc_size);
            if (res < 0)
                  Critical("HIDIOCGRDESCSIZE");

            /* Get Report Descriptor */
            rpt_desc.size = desc_size;
            res = ioctl(handle, HIDIOCGRDESC, &rpt_desc);
            if (res < 0)
                  Critical("HIDIOCGRDESC");
            notifier->setSocket(handle);
            notifier->setEnabled(true);
            return true;
            }
      return false;
      }

//---------------------------------------------------------
//   collectMouse
//---------------------------------------------------------

static void collectMouse(HidDeviceInfo* current, Mice& mice)
      {
      for (auto v : _3DCONNEXION_VENDORS) {
            if (v == current->vendorId) {
                  for (auto d : _3DCONNEXION_DEVICES) {
                        if (d == current->productId) {
                              DeviceIds detected_device(current->vendorId, current->productId);

                              auto it = mice.find(detected_device);
                              if (it == mice.end())
                                    it = mice.insert(Mice::value_type(detected_device, DevicePathList())).first;

                              it->second.emplace_back(current->path);
                              return;
                              }
                        }
                  }
            }
      }

//---------------------------------------------------------
//   init
//---------------------------------------------------------

bool SpaceMouse::init()
      {
      HidDeviceList  devices = enumerate(0, 0);
      if (devices.empty()) {
            Debug("No HID device enumerated.");
            return false;
            }
      Mice mice;
      for (auto m : devices)
            collectMouse(m, mice);

      if (mice.empty()) {
            Debug("no mice found");
            return false;
            }
      for (const auto& mouse : mice) {
            if (mouse.second.size() == 1) {
                  if (open(mouse.first.vendor, mouse.first.product)) {
//                        Info("spacemouse found: <{}><{}>", deviceInfo.manufacturer, deviceInfo.product);
                        return true;
                        }
                  Debug("3DConnexion device cannot be opened: {}\n"
                     "You may need to update /etc/udev/rules.d", mouse.second.front());
                  }
            }
      return false;
      }
