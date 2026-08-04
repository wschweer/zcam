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

#include "laser_rkq.h"
#include "zcam.h"
#include "project.h"
#include "machine.h"

//---------------------------------------------------------
//   LaserRKQ
//---------------------------------------------------------

LaserRKQ::LaserRKQ(ZCam* w, QObject* parent) : Laser(w, parent) {
      }

//---------------------------------------------------------
//   ~LaserRKQ
//---------------------------------------------------------

LaserRKQ::~LaserRKQ() {
      LaserRKQ::exitEngine();
      }

//---------------------------------------------------------
//   setupSocketNotifiers
//---------------------------------------------------------

void LaserRKQ::setupSocketNotifiers() {
      int fd = pcap_get_selectable_fd(_pcapHandle);
      if (fd < 0) {
            Critical("pcap_get_selectable_fd failed: {}", pcap_geterr(_pcapHandle));
            return;
            }

      _readNotifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
      connect(_readNotifier, &QSocketNotifier::activated, this, &LaserRKQ::onReadable);
      _readNotifier->setEnabled(true);

      _writeNotifier = new QSocketNotifier(fd, QSocketNotifier::Write, this);
      connect(_writeNotifier, &QSocketNotifier::activated, this, &LaserRKQ::onWritable);
      _writeNotifier->setEnabled(false);
      }

void LaserRKQ::teardownSocketNotifiers() {
      delete _readNotifier;
      _readNotifier = nullptr;
      delete _writeNotifier;
      _writeNotifier = nullptr;
      }

//---------------------------------------------------------
//   laserInit
//---------------------------------------------------------

void LaserRKQ::laserInit() {
      constexpr std::size_t FrameSize   = 1510;
      constexpr std::size_t PayloadSize = 1494;
      constexpr std::size_t HeaderSize  = FrameSize - PayloadSize;

      std::vector<std::uint8_t> frame(FrameSize, 0);
      std::fill(frame.begin(), frame.begin() + 6, 0xFF);

      Debug("laserInit: sending {}-byte broadcast frame ({} byte header + {} byte zero payload)", FrameSize,
            HeaderSize, PayloadSize);

      if (!sendPacket(frame))
            Warning("laserInit: failed to enqueue broadcast discovery frame");
      }

//---------------------------------------------------------
//   initEngine
//---------------------------------------------------------

bool LaserRKQ::initEngine(bool _dryRun) {
      set_dryRun(_dryRun);

      QString devName = ethDevice();
      if (devName.isEmpty()) {
            Critical("no Ethernet device configured for RKQ board");
            return false;
            }

      if (this->dryRun()) {
            Debug("init RKQ in dryRun mode (device={})", devName);
            return true;
            }

      char errbuf[PCAP_ERRBUF_SIZE];

      _pcapHandle = pcap_create(devName.toUtf8().constData(), errbuf);
      if (!_pcapHandle) {
            Critical("pcap_create failed for <{}>: {}", devName, errbuf);
            return false;
            }

      if (pcap_set_snaplen(_pcapHandle, 65536) != 0) {
            Critical("pcap_set_snaplen failed: {}", pcap_geterr(_pcapHandle));
            pcap_close(_pcapHandle);
            _pcapHandle = nullptr;
            return false;
            }

      if (pcap_set_promisc(_pcapHandle, 1) != 0) {
            Critical("pcap_set_promisc failed: {}", pcap_geterr(_pcapHandle));
            pcap_close(_pcapHandle);
            _pcapHandle = nullptr;
            return false;
            }

      if (pcap_set_timeout(_pcapHandle, 10) != 0) {
            Critical("pcap_set_timeout failed: {}", pcap_geterr(_pcapHandle));
            pcap_close(_pcapHandle);
            _pcapHandle = nullptr;
            return false;
            }

      if (pcap_set_immediate_mode(_pcapHandle, 1) != 0)
            Warning("pcap_set_immediate_mode not supported on <{}>", devName);

      int rv = pcap_activate(_pcapHandle);
      if (rv < 0) {
            Critical("pcap_activate failed for <{}>: {}", devName, pcap_statustostr(rv));
            pcap_close(_pcapHandle);
            _pcapHandle = nullptr;
            return false;
            }
      if (rv > 0)
            Warning("pcap_activate warning for <{}>: {}", devName, pcap_geterr(_pcapHandle));

      if (pcap_setnonblock(_pcapHandle, 1, errbuf) < 0) {
            Critical("pcap_setnonblock failed: {}", errbuf);
            pcap_close(_pcapHandle);
            _pcapHandle = nullptr;
            return false;
            }

      int dlt = pcap_datalink(_pcapHandle);
      if (dlt != DLT_EN10MB) {
            Critical("unsupported datalink type {} on <{}> (expected Ethernet)", dlt, devName);
            pcap_close(_pcapHandle);
            _pcapHandle = nullptr;
            return false;
            }

      setupSocketNotifiers();
      laserInit();

      Debug("RKQ initialised on Ethernet device <{}>", devName);
      return true;
      }

void LaserRKQ::exitEngine() {
      teardownSocketNotifiers();

      if (_bpfFilterSet) {
            pcap_freecode(&_bpfFilter);
            _bpfFilterSet = false;
            }

      if (_pcapHandle) {
            pcap_close(_pcapHandle);
            _pcapHandle = nullptr;
            }

      std::lock_guard lock(_txMutex);
      std::queue<std::vector<std::uint8_t>> empty;
      std::swap(_txQueue, empty);
      }

bool LaserRKQ::startFramingEngine() {
      return true;
      }

void LaserRKQ::stopFramingEngine() {
      }

void LaserRKQ::startMarkingEngine() {
      }

void LaserRKQ::stopMarkingEngine() const {
      }

void LaserRKQ::endMarkingEngine() {
      }

void LaserRKQ::mark(const Clipper2Lib::PathD&) {
      }

void LaserRKQ::move(double, double) {
      }

void LaserRKQ::markLayer(const LaserPath&, const LaserParameterSet&) {
      }

//---------------------------------------------------------
//   setFilter
//---------------------------------------------------------

bool LaserRKQ::setFilter(const std::string& bpfExpression) {
      if (!_pcapHandle) {
            Critical("setFilter: pcap handle not open");
            return false;
            }

      if (_bpfFilterSet) {
            pcap_freecode(&_bpfFilter);
            _bpfFilterSet = false;
            }

      if (pcap_compile(_pcapHandle, &_bpfFilter, bpfExpression.c_str(), 1, PCAP_NETMASK_UNKNOWN) < 0) {
            Critical("pcap_compile failed for <{}>: {}", bpfExpression, pcap_geterr(_pcapHandle));
            return false;
            }

      if (pcap_setfilter(_pcapHandle, &_bpfFilter) < 0) {
            Critical("pcap_setfilter failed: {}", pcap_geterr(_pcapHandle));
            pcap_freecode(&_bpfFilter);
            return false;
            }

      _bpfFilterSet = true;
      return true;
      }

//---------------------------------------------------------
//   sendPacket
//---------------------------------------------------------

bool LaserRKQ::sendPacket(const std::vector<std::uint8_t>& frame) {
      if (!_pcapHandle) {
            Critical("sendPacket: pcap handle not open");
            return false;
            }

      if (frame.empty()) {
            Warning("sendPacket: empty frame");
            return false;
            }

      bool wasEmpty;
            {
            std::lock_guard lock(_txMutex);
            wasEmpty = _txQueue.empty();
            _txQueue.push(frame);
            }

      if (wasEmpty && _writeNotifier)
            _writeNotifier->setEnabled(true);

      return true;
      }

//---------------------------------------------------------
//   onReadable
//---------------------------------------------------------

void LaserRKQ::onReadable(int) {
      if (!_pcapHandle)
            return;

      while (true) {
            struct pcap_pkthdr* hdr = nullptr;
            const u_char* data      = nullptr;
            int rv                  = pcap_next_ex(_pcapHandle, &hdr, &data);
            if (rv == 1) {
                  std::vector<std::uint8_t> payload(data, data + hdr->caplen);
                  Debug("packet received ({} bytes):\n{}", hdr->caplen, payload);
                  emit packetReceived(payload);
                  }
            else if (rv == 0) {
                  break;
                  }
            else if (rv == PCAP_ERROR_BREAK) {
                  break;
                  }
            else {
                  Critical("pcap_next_ex error: {}", pcap_geterr(_pcapHandle));
                  break;
                  }
            }
      }

//---------------------------------------------------------
//   onWritable
//---------------------------------------------------------

void LaserRKQ::onWritable(int) {
      if (!_pcapHandle)
            return;

      while (true) {
            std::vector<std::uint8_t> frame;
                  {
                  std::lock_guard lock(_txMutex);
                  if (_txQueue.empty()) {
                        if (_writeNotifier)
                              _writeNotifier->setEnabled(false);
                        return;
                        }
                  frame = std::move(_txQueue.front());
                  _txQueue.pop();
                  }

            int rv = pcap_sendpacket(_pcapHandle, frame.data(), static_cast<int>(frame.size()));
            if (rv < 0) {
                  Critical("pcap_sendpacket failed: {}", pcap_geterr(_pcapHandle));
                  return;
                  }

            emit packetSent();
            }
      }