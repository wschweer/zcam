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

#include <QObject>
#include <QVector2D>
#include <QTimer>
#include <QElapsedTimer>
#include <QSocketNotifier>
#include <QtQml/qqmlregistration.h>
// #include "recipe.h"

#include "logger.h"
#include "laserengine.h"

#include <pcap/pcap.h>

#include <atomic>
#include <mutex>
#include <queue>
#include <vector>
#include <cstdint>

class ZCam;

//-------------------------------------------------------------------------------------------------
//   LaserRKQ
//    Laser engine for the RKQ-LM-441 controller board.
//    Communication is via raw Ethernet frames using libpcap.
//    Both sending and receiving are fully asynchronous and
//    integrated into the Qt event loop via QSocketNotifier.
//-------------------------------------------------------------------------------------------------

class LaserRKQ : public LaserEngine
      {
      Q_OBJECT
      QML_ELEMENT
      QML_UNCREATABLE("no no no")

    private:
      // libpcap handle for the Ethernet device
      pcap_t* _pcapHandle {nullptr};

      // QSocketNotifier wraps the pcap selectable fd so incoming
      // packets are dispatched inside the Qt event loop.
      QSocketNotifier* _readNotifier  {nullptr};
      QSocketNotifier* _writeNotifier {nullptr};

      // Outgoing packet queue – populated by sendPacket(), drained
      // by the writeNotifier callback.  Guarded by a mutex because
      // sendPacket() can be called from framing/marking threads.
      std::mutex _txMutex;
      std::queue<std::vector<std::uint8_t>> _txQueue;

      // BPF filter program for the capture (optional).
      struct bpf_program _bpfFilter {};
      bool _bpfFilterSet {false};

      void setupSocketNotifiers();
      void teardownSocketNotifiers();

    private slots:
      void onReadable(int fd);
      void onWritable(int fd);

    protected:
      ZCam* zcam;

    public:
      LaserRKQ(ZCam* w, QObject* parent = nullptr);
      virtual ~LaserRKQ();
      virtual bool init(bool dryRun) override;
      virtual void exit() override;
      virtual void stop() override;

      virtual bool startFraming() override;
      virtual void stopFraming() override;

      virtual void startMarking() override;
      virtual void stopMarking() override;
      virtual void endMarking() override;

      virtual void mark(const Clipper2Lib::PathD&) override;
      virtual void move(double x, double y) override;
      virtual void markLayer(const LaserPath& path, const LaserParameterSet& sl) override;

      // ---- raw Ethernet I/O (async, integrated into Qt event loop) ----

      /// Asynchronously enqueue a raw Ethernet frame for transmission.
      /// The frame data must include the full Ethernet header (dst MAC,
      /// src MAC, EtherType, payload).  Returns false if the pcap handle
      /// is not open or the packet is empty.
      bool sendPacket(const std::vector<std::uint8_t>& frame);

      /// Install a BPF capture filter so only matching frames are
      /// delivered to packetReceived().  Call after init() succeeds.
      bool setFilter(const std::string& bpfExpression);

    signals:
      /// Emitted inside the Qt event loop for every raw frame that is
      /// received.  The vector contains the full Ethernet frame as
      /// captured by libpcap.
      void packetReceived(const std::vector<std::uint8_t>& payload);

      /// Emitted when a previously enqueued frame has been written.
      void packetSent();
      };