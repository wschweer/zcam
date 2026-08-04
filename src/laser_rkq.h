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

#include "logger.h"
#include "laser.h"

#include <pcap/pcap.h>

#include <atomic>
#include <mutex>
#include <queue>
#include <vector>
#include <cstdint>

class ZCam;

//-------------------------------------------------------------------------------------------------
//   LaserRKQ
//    Concrete Laser implementation for the RKQ-LM-441 controller board.
//    Communication is via raw Ethernet frames using libpcap.
//    Both sending and receiving are fully asynchronous and
//    integrated into the Qt event loop via QSocketNotifier.
//-------------------------------------------------------------------------------------------------

class LaserRKQ : public Laser
      {
      Q_OBJECT
      QML_ELEMENT
      QML_UNCREATABLE("no no no")

      pcap_t* _pcapHandle {nullptr};

      QSocketNotifier* _readNotifier {nullptr};
      QSocketNotifier* _writeNotifier {nullptr};

      std::mutex _txMutex;
      std::queue<std::vector<std::uint8_t>> _txQueue;

      struct bpf_program _bpfFilter {};
      bool _bpfFilterSet {false};

      void setupSocketNotifiers();
      void teardownSocketNotifiers();

      /// Send the initial broadcast discovery packet to the laser
      /// controller.
      void laserInit();

    private slots:
      void onReadable(int fd);
      void onWritable(int fd);

    signals:
      void packetReceived(const std::vector<std::uint8_t>& payload);
      void packetSent();

    public:
      LaserRKQ(ZCam* w, QObject* parent = nullptr);
      virtual ~LaserRKQ();

      // ── LaserEngine interface overrides ───────────────────────
      virtual bool initEngine(bool dryRun) override;
      virtual void exitEngine() override;

      virtual bool startFramingEngine() override;
      virtual void stopFramingEngine() override;

      virtual void startMarkingEngine() override;
      virtual void stopMarkingEngine() const override;
      virtual void endMarkingEngine() override;

      virtual void mark(const Clipper2Lib::PathD&) override;
      virtual void move(double x, double y) override;
      virtual void markLayer(const LaserPath& path, const LaserParameterSet& sl) override;

      // ---- raw Ethernet I/O (async, integrated into Qt event loop) ----
      bool sendPacket(const std::vector<std::uint8_t>& frame);
      bool setFilter(const std::string& bpfExpression);
      virtual const std::string_view properties() const override { return ""; }
      };