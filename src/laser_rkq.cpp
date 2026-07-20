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

LaserRKQ::LaserRKQ(ZCam* w, QObject* parent) : LaserEngine(w, parent) {
      zcam = w;
      }

//---------------------------------------------------------
//   ~LaserRKQ
//---------------------------------------------------------

LaserRKQ::~LaserRKQ() {
      LaserRKQ::exit();
      }

//---------------------------------------------------------
//   setupSocketNotifiers
//    Create QSocketNotifier objects for the pcap file descriptor
//    and wire them into the Qt event loop.  libpcap's selectable
//    fd is only available in non-blocking mode.
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

      // The write notifier is initially disabled – it is enabled
      // on demand when there are queued packets to send.
      _writeNotifier = new QSocketNotifier(fd, QSocketNotifier::Write, this);
      connect(_writeNotifier, &QSocketNotifier::activated, this, &LaserRKQ::onWritable);
      _writeNotifier->setEnabled(false);
      }

//---------------------------------------------------------
//   teardownSocketNotifiers
//---------------------------------------------------------

void LaserRKQ::teardownSocketNotifiers() {
      delete _readNotifier;
      _readNotifier = nullptr;
      delete _writeNotifier;
      _writeNotifier = nullptr;
      }

//---------------------------------------------------------
//   init
//    Open the configured Ethernet device with libpcap for
//    capturing and injecting raw frames.  The device name is
//    read from Machine::ethDevice().
//---------------------------------------------------------

bool LaserRKQ::init(bool _dryRun) {
      setDryRun(_dryRun);

      Machine* machine = zcam->project() ? zcam->project()->machine() : nullptr;
      if (!machine) {
            Critical("no laser machine configured");
            return false;
            }

      QString devName = machine->ethDevice();
      if (devName.isEmpty()) {
            Critical("no Ethernet device configured for RKQ board");
            return false;
            }

      if (this->dryRun()) {
            Debug("init RKQ in dryRun mode (device={})", devName);
            return true;
            }

      char errbuf[PCAP_ERRBUF_SIZE];

      // Use pcap_create / pcap_activate for fine-grained control.
      _pcapHandle = pcap_create(devName.toUtf8().constData(), errbuf);
      if (!_pcapHandle) {
            Critical("pcap_create failed for <{}>: {}", devName, errbuf);
            return false;
            }

      // Snaplen: large enough for full Ethernet frame (jumbo-safe).
      if (pcap_set_snaplen(_pcapHandle, 65536) != 0) {
            Critical("pcap_set_snaplen failed: {}", pcap_geterr(_pcapHandle));
            pcap_close(_pcapHandle);
            _pcapHandle = nullptr;
            return false;
            }

      // Promiscuous mode to see all traffic on the wire.
      if (pcap_set_promisc(_pcapHandle, 1) != 0) {
            Critical("pcap_set_promisc failed: {}", pcap_geterr(_pcapHandle));
            pcap_close(_pcapHandle);
            _pcapHandle = nullptr;
            return false;
            }

      // Read timeout 10 ms – short enough for responsive async I/O.
      if (pcap_set_timeout(_pcapHandle, 10) != 0) {
            Critical("pcap_set_timeout failed: {}", pcap_geterr(_pcapHandle));
            pcap_close(_pcapHandle);
            _pcapHandle = nullptr;
            return false;
            }

      // Immediate mode delivers packets as soon as they arrive
      // rather than buffering until the timeout expires.
      if (pcap_set_immediate_mode(_pcapHandle, 1) != 0) {
            // Non-fatal: older libpcap versions may not support this.
            Warning("pcap_set_immediate_mode not supported on <{}>", devName);
            }

      int rv = pcap_activate(_pcapHandle);
      if (rv < 0) {
            Critical("pcap_activate failed for <{}>: {}", devName, pcap_statustostr(rv));
            pcap_close(_pcapHandle);
            _pcapHandle = nullptr;
            return false;
            }
      if (rv > 0) {
            // Warning (e.g. promiscuous mode not granted).
            Warning("pcap_activate warning for <{}>: {}", devName, pcap_geterr(_pcapHandle));
            }

      // Switch to non-blocking so QSocketNotifier works correctly.
      if (pcap_setnonblock(_pcapHandle, 1, errbuf) < 0) {
            Critical("pcap_setnonblock failed: {}", errbuf);
            pcap_close(_pcapHandle);
            _pcapHandle = nullptr;
            return false;
            }

      // Verify that the data link is Ethernet.
      int dlt = pcap_datalink(_pcapHandle);
      if (dlt != DLT_EN10MB) {
            Critical("unsupported datalink type {} on <{}> (expected Ethernet)", dlt, devName);
            pcap_close(_pcapHandle);
            _pcapHandle = nullptr;
            return false;
            }

      setupSocketNotifiers();

      Debug("RKQ initialised on Ethernet device <{}>", devName);
      return true;
      }

//---------------------------------------------------------
//   exit
//---------------------------------------------------------

void LaserRKQ::exit() {
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

//---------------------------------------------------------
//   stop
//---------------------------------------------------------

void LaserRKQ::stop() {
      }

//---------------------------------------------------------
//   startFraming
//---------------------------------------------------------

bool LaserRKQ::startFraming() {
      return true;
      }

//---------------------------------------------------------
//   stopFraming
//---------------------------------------------------------

void LaserRKQ::stopFraming() {
      }

//---------------------------------------------------------
//   startMarking
//---------------------------------------------------------

void LaserRKQ::startMarking() {
      }

//---------------------------------------------------------
//   stopMarking
//---------------------------------------------------------

void LaserRKQ::stopMarking() {
      }

//---------------------------------------------------------
//   endMarking
//---------------------------------------------------------

void LaserRKQ::endMarking() {
      }

//---------------------------------------------------------
//   mark
//---------------------------------------------------------

void LaserRKQ::mark(const Clipper2Lib::PathD&) {
      }

//---------------------------------------------------------
//   move
//---------------------------------------------------------

void LaserRKQ::move(double, double) {
      }

//---------------------------------------------------------
//   markLayer
//---------------------------------------------------------

void LaserRKQ::markLayer(const LaserPath&, const LaserParameterSet&) {
      }

//---------------------------------------------------------
//   setFilter
//    Compile and install a BPF filter on the pcap handle so
//    only frames matching the expression are delivered to the
//    read notifier callback.
//---------------------------------------------------------

bool LaserRKQ::setFilter(const std::string& bpfExpression) {
      if (!_pcapHandle) {
            Critical("setFilter: pcap handle not open");
            return false;
            }

      // Free a previously installed filter.
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
//    Asynchronously enqueue a raw Ethernet frame for transmission.
//    The actual pcap_sendpacket() call happens in onWritable()
//    inside the Qt event loop.  If the queue was empty before this
//    call, the write notifier is enabled so the event loop will
//    drain the queue promptly.
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
//    Called by the Qt event loop when the pcap fd becomes
//    readable.  Dispatches all currently available frames
//    via pcap_next_ex() and emits packetReceived() for each.
//    In non-blocking mode pcap_next_ex() returns 0 when no
//    more packets are buffered.
//---------------------------------------------------------

void LaserRKQ::onReadable(int) {
      if (!_pcapHandle)
            return;

      while (true) {
            struct pcap_pkthdr* hdr  = nullptr;
            const u_char* data        = nullptr;
            int rv = pcap_next_ex(_pcapHandle, &hdr, &data);
            if (rv == 1) {
                  // A packet was successfully read.
                  std::vector<std::uint8_t> payload(data, data + hdr->caplen);
                  emit packetReceived(payload);
                  }
            else if (rv == 0) {
                  // No packets available right now (non-blocking).
                  break;
                  }
            else if (rv == PCAP_ERROR_BREAK) {
                  // Loop terminated by pcap_breakloop().
                  break;
                  }
            else {
                  // Error reading.
                  Critical("pcap_next_ex error: {}", pcap_geterr(_pcapHandle));
                  break;
                  }
            }
      }

//---------------------------------------------------------
//   onWritable
//    Called by the Qt event loop when the pcap fd is writable
//    and we have queued frames to send.  Drains the transmit
//    queue by calling pcap_sendpacket() for each frame.
//    When the queue is empty the write notifier is disabled
//    to avoid busy-looping.
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
                  // Re-enable notifier for a retry on next event loop iteration.
                  return;
                  }

            emit packetSent();
            }
      }