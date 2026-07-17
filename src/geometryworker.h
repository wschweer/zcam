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
#include <QThreadPool>
#include <QVector3D>
#include <QByteArray>
#include <functional>
#include <vector>
#include <atomic>
#include "types.h"

//---------------------------------------------------------
//   GeometryWorker
//    Singleton that offloads CPU-intensive geometry calculations
//    (tesselation, stroke/inflate, line conversion) to a background
//    thread pool.  Results are delivered back on the main thread via
//    QMetaObject::invokeMethod(QueuedConnection).
//
//    Thread-safety contract:
//      • Input data is captured **by value** before offloading.
//      • Computation functions are pure (no access to QObject state).
//      • Callbacks are invoked on the main thread.
//      • Use QPointer<T> in callbacks to guard against object deletion
//        while a computation is in flight.
//---------------------------------------------------------

class GeometryWorker : public QObject
      {
      Q_OBJECT

    public:
      static GeometryWorker& instance();
      //--- Tesselation (filled polygons → triangulated mesh) ---
      //    Heavy: O(n log n) sweep-line tesselation.
      //    Input:  PathList (value-copied)
      //    Output: vertex/index buffers + bounding box
      struct TessResult {
            QByteArray vertices;
            QByteArray indices;
            QVector3D minBound;
            QVector3D maxBound;
            bool valid {false};
            };
      using TessCallback = std::function<void(const TessResult&)>;
      void requestTesselation(PathList pathList, TessCallback callback);
      //--- Line geometry (open polylines → line-strip vertices) ---
      //    Light: vertex copy + bounding box.
      //    Used for non-filled polygon display and selection geometry.
      struct LineResult {
            QByteArray vertices;
            QVector3D minBound;
            QVector3D maxBound;
            struct Subset {
                  int offset;
                  int length;
                  QVector3D min;
                  QVector3D max;
                  };
            std::vector<Subset> subsets;
            bool valid {false};
            };
      using LineCallback = std::function<void(const LineResult&)>;
      void requestLines(Clipper2Lib::PathsD lines, LineCallback callback);
      //--- Stroke (InflatePaths + close path) ---
      //    Heavy: O(n) clipper2 offset operation.
      //    Produces an inflated PathList for rendering line strokes.
      struct StrokeResult {
            PathList pathList;
            bool valid {false};
            };
      using StrokeCallback = std::function<void(const StrokeResult&)>;
      void requestStroke(Clipper2Lib::PathsD paths, double halfLineWidth, int joinType, int endType,
                         bool fill, StrokeCallback callback);
      //--- Cam panel data (grid replication + line merge) ---
      //    Moderate: replicates tile lines across panel grid.
      //    Input data (per-layer display lines) is pre-computed on the
      //    main thread because it requires access to QObject properties.
      struct CamInput {
            struct Layer {
                  Clipper2Lib::PathsD tileLines; // 2 subsets: marks, moves
                  bool burn {true};
                  };
            std::vector<Layer> layers;
            int panelRows {1};
            int panelColumns {1};
            double panelHDistance {0.0};
            double panelVDistance {0.0};
            double fixtureW {0.0};
            double fixtureH {0.0};
            };
      struct CamResult {
            Clipper2Lib::PathsD combinedLines;
            bool valid {false};
            };
      using CamCallback = std::function<void(const CamResult&)>;
      void requestCamData(CamInput input, CamCallback callback);
      //--- Convex hull ---
      //    O(n log n) Andrew's monotone chain.
      struct ConvexHullResult {
            Clipper2Lib::PathD hull;
            bool valid {false};
            };
      using ConvexHullCallback = std::function<void(const ConvexHullResult&)>;
      void requestConvexHull(Clipper2Lib::PathD points, ConvexHullCallback callback);
      //--- Query ---
      int activeTaskCount() const { return m_pool.activeThreadCount(); }
      void shutdown();

    private:
      GeometryWorker();
      ~GeometryWorker();
      // Helper: enqueue a compute task and deliver result on main thread
      template <typename Result, typename Compute, typename Callback>
      void enqueue(Compute&& compute, Callback&& callback) {
            if (m_shuttingDown.load(std::memory_order::relaxed))
                  return;
            auto* raw = new ComputeTask<Result, std::decay_t<Compute>, std::decay_t<Callback>>(
                std::forward<Compute>(compute), std::forward<Callback>(callback), this, m_shuttingDown);
            m_pool.start(raw);
            }
      // Generic QRunnable wrapper (Qt 6.5 doesn't have QRunnable::create)
      template <typename R, typename F, typename C> class ComputeTask : public QRunnable
            {
            F m_compute;
            C m_callback;
            QObject* m_receiver;
            std::atomic<bool>& m_shuttingDown;

          public:
            ComputeTask(F compute, C callback, QObject* receiver, std::atomic<bool>& shuttingDown)
                : QRunnable(), m_compute(std::move(compute)), m_callback(std::move(callback)),
                  m_receiver(receiver), m_shuttingDown(shuttingDown) {}
            void run() override {
                  R result = m_compute();
                  // Skip the callback during shutdown: the receiver may be
                  // partially destroyed and posting a QueuedConnection event
                  // would keep the event loop alive, causing a long hang.
                  if (m_shuttingDown.load(std::memory_order::relaxed))
                        return;
                  QMetaObject::invokeMethod(
                      m_receiver,
                      [cb = std::move(m_callback), res = std::move(result)]() mutable { cb(res); },
                      Qt::QueuedConnection);
                  }
            };
      QThreadPool m_pool;
      std::atomic<bool> m_shuttingDown {false};
      };