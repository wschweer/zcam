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

#include "pathstrategy.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

using Point = Clipper2Lib::Point<double>;

//=============================================================================
//  KD-Tree for fast 2-D nearest-neighbour search
//  Each node stores the point coordinates together with
//  the originating line index and which endpoint (0 or 1)
//  it represents.  Used to accelerate greedy path optimisation
//  from O(N²) down to ~O(N log N).
//=============================================================================
struct KDNode {
      double point[2] {};
      int lineIndex   = 0;
      int endPointIdx = 0;
      std::unique_ptr<KDNode> left, right;
      };

struct EndPointRef {
      double x, y;
      int lineIndex;
      int endPointIdx;
      };

//--------------------------------------------------------------------
//     buildKDTree
//--------------------------------------------------------------------

static std::unique_ptr<KDNode> buildKDTree(
    std::vector<EndPointRef>::iterator begin, std::vector<EndPointRef>::iterator end, int depth) {
      if (begin == end)
            return nullptr;

      const int axis = depth & 1;

      const size_t n = static_cast<size_t>(std::distance(begin, end));
      auto mid       = begin + static_cast<long>(n / 2);

      std::nth_element(begin, mid, end,
          [axis](const EndPointRef& a, const EndPointRef& b) { return axis == 0 ? a.x < b.x : a.y < b.y; });

      auto node         = std::make_unique<KDNode>();
      node->point[0]    = mid->x;
      node->point[1]    = mid->y;
      node->lineIndex   = mid->lineIndex;
      node->endPointIdx = mid->endPointIdx;
      node->left        = buildKDTree(begin, mid, depth + 1);
      node->right       = buildKDTree(mid + 1, end, depth + 1);
      return node;
      }

//--------------------------------------------------------------------
//     kdNearest
//--------------------------------------------------------------------

static void kdNearest(const KDNode* node, double qx, double qy, int depth, const std::vector<bool>& used,
    const KDNode*& best, double& bestDist) {
      if (!node)
            return;

      if (!used[node->lineIndex]) {
            const double dx = node->point[0] - qx;
            const double dy = node->point[1] - qy;
            const double d2 = dx * dx + dy * dy;
            if (d2 < bestDist) {
                  bestDist = d2;
                  best     = node;
                  }
            }

      const int axis    = depth & 1;
      const double diff = (axis == 0 ? qx - node->point[0] : qy - node->point[1]);

      const KDNode* nearChild = (diff < 0) ? node->left.get() : node->right.get();
      const KDNode* farChild  = (diff < 0) ? node->right.get() : node->left.get();

      kdNearest(nearChild, qx, qy, depth + 1, used, best, bestDist);

      if (diff * diff < bestDist)
            kdNearest(farChild, qx, qy, depth + 1, used, best, bestDist);
      }

//--------------------------------------------------------------------
//     optimizePath
//   Greedy nearest-neighbour path optimisation accelerated by a k-d tree.
//--------------------------------------------------------------------

static Clipper2Lib::PathsD optimizePath(Clipper2Lib::PathsD inputLines, Point& currentPos) {
      Clipper2Lib::PathsD optimizedLines;
      const size_t totalLines = inputLines.size();
      if (totalLines == 0)
            return optimizedLines;
      optimizedLines.reserve(totalLines);

      std::vector<bool> used(totalLines, false);
      size_t remaining = totalLines;

      auto buildTree = [&]() {
            for (size_t i = 0; i < totalLines; ++i) {
                  if (!used[i] && inputLines[i].size() < 2) {
                        used[i] = true;
                        --remaining;
                        }
                  }

            std::vector<EndPointRef> pts;
            pts.reserve(remaining * 2);
            for (size_t i = 0; i < totalLines; ++i) {
                  if (used[i])
                        continue;
                  pts.push_back({inputLines[i][0].x, inputLines[i][0].y, int(i), 0});
                  pts.push_back({inputLines[i][1].x, inputLines[i][1].y, int(i), 1});
                  }
            return buildKDTree(pts.begin(), pts.end(), 0);
            };

      std::unique_ptr<KDNode> tree = buildTree();
      size_t lastRebuildRemaining  = remaining;
      size_t rebuildThreshold      = (remaining + 3) / 4;

      Point pos = currentPos;

      while (remaining > 0) {
            const KDNode* best = nullptr;
            double bestDist    = std::numeric_limits<double>::max();
            kdNearest(tree.get(), pos.x, pos.y, 0, used, best, bestDist);

            if (!best)
                  break;

            const int idx   = best->lineIndex;
            const int epIdx = best->endPointIdx;
            auto& line      = inputLines[idx];

            Clipper2Lib::PathD nextLine;
            nextLine.reserve(2);
            if (epIdx == 0) {
                  nextLine.push_back(line[0]);
                  nextLine.push_back(line[1]);
                  }
            else {
                  nextLine.push_back(line[1]);
                  nextLine.push_back(line[0]);
                  }

            optimizedLines.push_back(std::move(nextLine));
            used[idx] = true;
            --remaining;
            pos = optimizedLines.back()[1];

            if (remaining > 0 && lastRebuildRemaining - remaining >= rebuildThreshold) {
                  tree                 = buildTree();
                  lastRebuildRemaining = remaining;
                  rebuildThreshold     = (remaining + 3) / 4;
                  }
            }

      currentPos = pos;
      return optimizedLines;
      }

//=============================================================================
//  PathStrategy implementation
//=============================================================================

//--------------------------------------------------------------------
//     normalizeAngle
//--------------------------------------------------------------------

double PathStrategy::normalizeAngle(double a) {
      while (a >= 180.0)
            a -= 180.0;
      while (a < 0.0)
            a += 180.0;
      return a;
      }

//--------------------------------------------------------------------
//     findOrCreateLayer
//--------------------------------------------------------------------

HatchLayer& PathStrategy::findOrCreateLayer(
    LayeredLines& layered, double angle, const LaserPass* pass, bool isOutline) {
      double norm = normalizeAngle(angle);

      for (auto& layer : layered.layers) {
            if (isOutline && layer.isOutline)
                  return layer;
            if (!isOutline && !layer.isOutline && std::abs(layer.angle - norm) < 0.01)
                  return layer;
            }

      layered.layers.emplace_back(norm, pass, isOutline);
      return layered.layers.back();
      }

//--------------------------------------------------------------------
//     toLayeredLaserPath
//   Optimiere die Linien jeder Ebene unabhaengig und erzeuge einen
//   LaserPath (ebenausweise).  Die currentPos wird zwischen Ebenen
//   weitergegeben, so dass der Laser nach Abschluss einer Ebene an
//   der aktuellen Position bleibt und von dort zur naechsten Ebene
//   springt.
//--------------------------------------------------------------------

LaserPath PathStrategy::toLayeredLaserPath(const LayeredLines& layered) {
      LaserPath lp;
      Point currentPos(0, 0);

      for (const auto& layer : layered.layers) {
            if (layer.lines.empty())
                  continue;

            auto optimized = optimizePath(layer.lines, currentPos);
            if (optimized.empty())
                  continue;

            if (lp.empty()) {
                  currentPos = optimized.front()[0];
                  lp.moveTo(currentPos.x, currentPos.y);
                  }

            for (const auto& l : optimized) {
                  if (l.size() == 2) {
                        if (!(qFuzzyCompare(currentPos.x, l[0].x) && qFuzzyCompare(currentPos.y, l[0].y)))
                              lp.moveTo(l[0].x, l[0].y);
                        lp.markTo(l[1].x, l[1].y);
                        currentPos = {l[1].x, l[1].y};
                        }
                  }
            }

      return lp;
      }

//--------------------------------------------------------------------
//     toDisplayLines
//   Wie toLayeredLaserPath, aber trennt Mark- und Move-Segmente in
//   zwei separate PathD-Objekte fuer die 3D-Canvas-Vorschau.
//--------------------------------------------------------------------

void PathStrategy::toDisplayLines(const LayeredLines& layered, Clipper2Lib::PathD& markLines,
    Clipper2Lib::PathD& moveLines, bool showMarks, bool showMoves) {

      LaserPath lp = toLayeredLaserPath(layered);
      if (lp.empty())
            return;

      if (showMarks) {
            LaserPathElement last = lp.front();
            for (auto& pt : lp) {
                  if (pt.type == LaserPathElementType::MarkTo) {
                        markLines.push_back({last.x(), last.y()});
                        markLines.push_back({pt.x(), pt.y()});
                        }
                  last = pt;
                  }
            }

      if (showMoves) {
            LaserPathElement last = lp.front();
            for (auto& pt : lp) {
                  if (pt.type == LaserPathElementType::MoveTo) {
                        moveLines.push_back({last.x(), last.y()});
                        moveLines.push_back({pt.x(), pt.y()});
                        }
                  last = pt;
                  }
            }
      }