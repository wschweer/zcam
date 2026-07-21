//=============================================================================
//  wcam
//  G-Code generator
//
//  Copyright (C) 2023 Werner Schweer
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENSE.GPL
//=============================================================================

#include "laserlayer.h"
#include "cam.h"
#include "clipper.h"
#include "element3d.h"
#include "fixture.h"
#include "project.h"
#include "cad.h"
#include "zcam.h"

#include <algorithm>
// #include <cmath>
#include <functional>
#include <future>
#include <limits>
#include <memory>
#include <vector>

//---------------------------------------------------------
//   collectBurnElementsForLaserLayer
//    Recursively collect all burnable Element3d children under the
//    given element whose effectiveLaserLayer() equals the given
//    LaserLayer.  This traverses the full subtree so that elements
//    at any depth are found, as long as they resolve to this LaserLayer.
//---------------------------------------------------------

static void collectBurnElementsForLaserLayer(Element* parent, const LaserLayer* ll,
                                             std::vector<const Element3d*>& out) {
      for (Element* child : parent->children()) {
            auto* ce = qobject_cast<Element3d*>(child);
            if (!ce)
                  continue;
            // Skip LaserLayer elements themselves — they are not geometry.
            if (isType<LaserLayer>(ce))
                  continue;
            // Check if this element's effective LaserLayer is the one we're looking for.
            if (ce->effectiveLaserLayer() == ll && ce->burn() && !ce->pathList().empty())
                  out.push_back(ce);
            // Recurse into children regardless — a child may resolve to a
            // different LaserLayer or to this one via inheritance.
            collectBurnElementsForLaserLayer(child, ll, out);
            }
      }

//---------------------------------------------------------
//   collectElements
//    Collect all Element3d items in the project tree (from Cad
//    downward) whose effectiveLaserLayer() equals this LaserLayer.
//    The search starts at the Cad element and traverses the full
//    subtree recursively.
//---------------------------------------------------------

std::vector<const Element3d*> LaserLayer::collectElements() const {
      std::vector<const Element3d*> elements;
      Project* proj = zcam->project();
      if (!proj)
            return elements;
      Cad* cad = proj->cad();
      if (!cad)
            return elements;
      collectBurnElementsForLaserLayer(cad, this, elements);
      return elements;
      }

//---------------------------------------------------------
//   Point
//---------------------------------------------------------

using Point = Clipper2Lib::Point<double>;

static void optimize(LaserPath* lp, Clipper2Lib::PathsD lines);
static Clipper2Lib::PathsD optimizePath(Clipper2Lib::PathsD inputLines, Point& currentPos);

//---------------------------------------------------------
//   LaserLayer
//---------------------------------------------------------

LaserLayer::LaserLayer(ZCam* w, Element* parent) : Element3d(w, parent) {
      // LaserLayer no longer creates its own _geometry.
      // Display geometry is collected and rendered by Cam.
      set_model("LaserLayer1.qml");
      setColor(w->config()->markColor());
      // Keep the element color in sync with Config::markColor so that
      // ProjectTree's curColor binding shows the correct color for hover/selection.
      connect(w->config(), &Config::markColorChanged, this,
              [this, w]() { setColor(w->config()->markColor()); });
      }

LaserLayer::~LaserLayer() {
      // _geometry is no longer created in the constructor.
      // Element3d base class manages _geometry = nullptr safely.
      }

//---------------------------------------------------------
//   collectLayerPath
//    Return polygon list for a single tile (no panel-grid offsets).
//    The geometry is in project-root coordinate space
//    (ce->globalMatrix()).
//    Cam applies the panel-grid offsets when building the full layout.
//---------------------------------------------------------

PathsD LaserLayer::collectLayerPath() {
      spl.clear();
      auto elements = collectElements();

      for (const auto* ce : elements) {
            const auto& pl = ce->pathList();

            QMatrix4x4 matrix = ce->globalMatrix();

            for (const auto& p : pl) {
                  Clipper2Lib::PathD cp;
                  for (const auto& pt : p) {
                        auto r = matrix.map(QVector3D(pt.x(), pt.y(), 0.0));
                        cp.push_back({r.x(), r.y()});
                        }
                  spl.push_back(cp);
                  }
            }

      return spl;
      }

//---------------------------------------------------------
//   processTileLines
//    Process one tile's geometry through the recipe (fill, wobble,
//    line segments) and return raw line segments in project-root
//    coordinate space.  No panel-grid offsets are applied —
//    Cam handles the grid layout.
//---------------------------------------------------------

Clipper2Lib::PathsD LaserLayer::processTileLines() const {
      if (!recipe()) {
            Critical("no recipe for <{}>", name());
            return {};
            }

      Clipper2Lib::PathsD lineList;
      auto elements = collectElements();

      for (const auto* ce : elements) {
            PathList pl = ce->pathList();

            QMatrix4x4 matrix = ce->globalMatrix();

            Clipper2Lib::PathsD ll;
            for (const auto& p : pl) {
                  Clipper2Lib::PathD cp;
                  for (const auto& pt : p) {
                        auto r = matrix.map(QVector3D(pt.x(), pt.y(), 0.0));
                        cp.push_back({r.x(), r.y()});
                        }
                  ll.push_back(cp);
                  }
            auto* ls = recipe()->layer(0);
            if (pl.fill())
                  lineList.append_range(createFill(ll));
            else if (ls->wobble()) {
                  for (const auto& p : ll) {
                        if (p.size() < 2)
                              continue;
                        auto wl = wobble(p, ls->wobbleStep(), ls->wobbleSize());
                        lineList.push_back(wl);
                        }
                  }
            else {
                  for (const auto& l : ll) {
                        if (l.size() < 2)
                              continue;
                        auto currentPoint = l[0];
                        for (int i = 1; i < l.size(); ++i) {
                              Clipper2Lib::PathD p;
                              p.push_back(currentPoint);
                              currentPoint = l[i];
                              p.push_back(currentPoint);
                              lineList.push_back(p);
                              }
                        }
                  }
            }
      return lineList;
      }

//---------------------------------------------------------
//   collectDisplayLines
//    Return display line segments (mark + move subsets) for a
//    single tile.  The result has exactly two subsets:
//      subset 0 — MarkTo segments (if showMarks)
//      subset 1 — MoveTo segments (if showMoves)
//    No panel-grid offsets are applied.
//---------------------------------------------------------

Clipper2Lib::PathsD LaserLayer::collectDisplayLines() const {
      Clipper2Lib::PathsD tileLines = processTileLines();

      // Optimise the line order for display
      Point currentPos(0, 0);
      auto optimized = optimizePath(std::move(tileLines), currentPos);

      // Build a LaserPath from the optimized lines
      LaserPath lp;
      if (!optimized.empty()) {
            currentPos = optimized.front()[0];
            lp.moveTo(currentPos.x, currentPos.y);
            for (const auto& l : optimized) {
                  if (l.size() == 2) {
                        if (!(qFuzzyCompare(currentPos.x, l[0].x) && qFuzzyCompare(currentPos.y, l[0].y)))
                              lp.moveTo(l[0].x, l[0].y);
                        lp.markTo(l[1].x, l[1].y);
                        currentPos = {l[1].x, l[1].y};
                        }
                  }
            }

      // Separate MarkTo and MoveTo segments into two subsets
      Clipper2Lib::PathsD lineList;

      Clipper2Lib::PathD markLines;
      LaserPathElement last;
      if (showMarks()) {
            for (auto& pt : lp) {
                  if (pt.type == LaserPathElementType::MarkTo) {
                        markLines.push_back({last.x(), last.y()});
                        markLines.push_back({pt.x(), pt.y()});
                        }
                  last = pt;
                  }
            }
      lineList.push_back(markLines);

      Clipper2Lib::PathD moveLines;
      if (!lp.empty()) {
            last = lp.front();
            if (showMoves()) {
                  for (auto& pt : lp) {
                        if (pt.type == LaserPathElementType::MoveTo) {
                              moveLines.push_back({last.x(), last.y()});
                              moveLines.push_back({pt.x(), pt.y()});
                              }
                        last = pt;
                        }
                  }
            }
      lineList.push_back(moveLines);

      return lineList;
      }

//---------------------------------------------------------
//   collectLaserPath
//    Produces the laser path for all panel tiles.
//    The geometry is in project-root coordinate space
//    (ce->globalMatrix()).  Panel-grid offsets are applied here
//    because the laser path is used for actual laser marking
//    and must contain the full panel layout.
//---------------------------------------------------------

LaserPath LaserLayer::collectLaserPath() const {
      if (!recipe()) {
            Critical("no recipe for <{}>", name());
            return LaserPath();
            }

      Cam* cam     = zcam->project()->cam();

      double panelHD = cam->panelHDistance();
      double panelVD = cam->panelVDistance();
      double w, h;
      zcam->project()->fixture()->size(w, h);

      auto elements = collectElements();

      Clipper2Lib::PathsD lineList;
      for (int row = 0; row < cam->panelRows(); ++row) {
            for (int column = 0; column < cam->panelColumns(); ++column) {
                  double xo = (panelHD + w) * column;
                  double yo = (panelVD + h) * row;

                  for (const auto* ce : elements) {
                        PathList pl = ce->pathList();

                        QMatrix4x4 matrix = ce->globalMatrix();

                        //===========================================
                        //    convert to CAM coordinate system
                        //===========================================

                        Clipper2Lib::PathsD ll;
                        for (const auto& p : pl) {
                              Clipper2Lib::PathD cp;
                              for (const auto& pt : p) {
                                    auto r = matrix.map(QVector3D(pt.x(), pt.y(), 0.0));
                                    cp.push_back({r.x() + xo, r.y() + yo});
                                    }
                              ll.push_back(cp);
                              }
                        auto* ls        = recipe()->layer(0);
                        bool mustWobble = ls->wobble();
                        if (pl.fill()) {
                              lineList.append_range(createFill(ll));
                              }
                        else {
                              // process lines
                              if (mustWobble) {
                                    for (const auto& p : ll) {
                                          if (p.size() < 2)
                                                continue;
                                          auto wl = wobble(p, ls->wobbleStep(), ls->wobbleSize());
                                          lineList.push_back(wl);
                                          }
                                    }
                              else {
                                    for (const auto& l : ll) {
                                          if (l.size() < 2)
                                                continue;
                                          auto currentPoint = l[0];
                                          for (int i = 1; i < l.size(); ++i) {
                                                Clipper2Lib::PathD p;
                                                p.push_back(currentPoint);
                                                currentPoint = l[i];
                                                p.push_back(currentPoint);
                                                lineList.push_back(p);
                                                }
                                          }
                                    }
                              }
                        }
                  }
            }
      LaserPath path;
      optimize(&path, lineList);
      return path;
      }

//---------------------------------------------------------
//   KD-Tree for fast 2-D nearest-neighbour search
//   Each node stores the point coordinates together with
//   the originating line index and which endpoint (0 or 1)
//   it represents.  Used to accelerate greedy path optimisation
//   from O(N²) down to ~O(N log N).
//---------------------------------------------------------

struct KDNode {
      double point[2] {};
      int lineIndex   = 0; // index into the input line array
      int endPointIdx = 0; // 0 or 1 - which end of the line this node holds
      std::unique_ptr<KDNode> left, right;
      };

struct EndPointRef {
      double x, y;
      int lineIndex;
      int endPointIdx; // 0 or 1
      };

static std::unique_ptr<KDNode> buildKDTree(std::vector<EndPointRef>::iterator begin,
                                           std::vector<EndPointRef>::iterator end, int depth) {
      if (begin == end)
            return nullptr;

      const int axis = depth & 1; // 0 = x, 1 = y

      const size_t n = static_cast<size_t>(std::distance(begin, end));
      auto mid       = begin + static_cast<long>(n / 2);

      std::nth_element(begin, mid, end, [axis](const EndPointRef& a, const EndPointRef& b) {
            return axis == 0 ? a.x < b.x : a.y < b.y;
            });

      auto node         = std::make_unique<KDNode>();
      node->point[0]    = mid->x;
      node->point[1]    = mid->y;
      node->lineIndex   = mid->lineIndex;
      node->endPointIdx = mid->endPointIdx;
      node->left        = buildKDTree(begin, mid, depth + 1);
      node->right       = buildKDTree(mid + 1, end, depth + 1);
      return node;
      }

/// Recursive nearest-neighbour search in the k-d tree.
/// \param best        best candidate found so far (output)
/// \param bestDist    squared distance to best candidate (output)
/// \param used        flags which lines have already been consumed
static void kdNearest(const KDNode* node, double qx, double qy, int depth, const std::vector<bool>& used,
                      const KDNode*& best, double& bestDist) {
      if (!node)
            return;

      // Evaluate this node if its line has not been consumed yet
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

      // Visit the near side first
      const KDNode* nearChild = (diff < 0) ? node->left.get() : node->right.get();
      const KDNode* farChild  = (diff < 0) ? node->right.get() : node->left.get();

      kdNearest(nearChild, qx, qy, depth + 1, used, best, bestDist);

      // Only visit the far side if it could possibly contain a closer point
      if (diff * diff < bestDist)
            kdNearest(farChild, qx, qy, depth + 1, used, best, bestDist);
      }

//---------------------------------------------------------
//   optimizePath
//   Greedy nearest-neighbour path optimisation accelerated by a k-d tree.
//   Complexity: O(N log N) average, O(N²) worst-case (degenerate tree).
//
//   The tree is rebuilt periodically once enough lines have been consumed
//   so that dead nodes don't degrade search performance.
//---------------------------------------------------------

static Clipper2Lib::PathsD optimizePath(Clipper2Lib::PathsD inputLines, Point& currentPos) {
      Clipper2Lib::PathsD optimizedLines;
      const size_t totalLines = inputLines.size();
      if (totalLines == 0)
            return optimizedLines;
      optimizedLines.reserve(totalLines);

      std::vector<bool> used(totalLines, false);
      size_t remaining = totalLines;

      // Build a k-d tree over all unused line endpoints (2 nodes per line).
      // Lines with fewer than 2 points are degenerate and are marked as used
      // so they are silently skipped during optimisation.
      auto buildTree = [&]() {
            // First pass: mark degenerate lines as used so they are never queried
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
      size_t rebuildThreshold      = (remaining + 3) / 4; // rebuild after 25% consumed

      Point pos = currentPos;

      while (remaining > 0) {
            // --- nearest-neighbour search via k-d tree ---
            const KDNode* best = nullptr;
            double bestDist    = std::numeric_limits<double>::max();
            kdNearest(tree.get(), pos.x, pos.y, 0, used, best, bestDist);

            if (!best)
                  break; // safety guard - should never happen

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
                  // Laser the line in reverse
                  nextLine.push_back(line[1]);
                  nextLine.push_back(line[0]);
                  }

            optimizedLines.push_back(std::move(nextLine));
            used[idx] = true;
            --remaining;
            pos = optimizedLines.back()[1];

            // Periodically rebuild the tree so dead nodes don't degrade search performance
            if (remaining > 0 && lastRebuildRemaining - remaining >= rebuildThreshold) {
                  tree                 = buildTree();
                  lastRebuildRemaining = remaining;
                  rebuildThreshold     = (remaining + 3) / 4;
                  }
            }

      currentPos = pos;
      return optimizedLines;
      }

//---------------------------------------------------------
//   createFill
//    fill polygon spdi with hatch pattern
//---------------------------------------------------------

Clipper2Lib::PathsD LaserLayer::createFill(Clipper2Lib::PathsD& spdi) const {
      Clipper2Lib::PathsD lineList;

      const LaserPasses* fll = recipe()->layers();

      if (kerfOffset())
            spdi = InflatePaths(spdi, kerfOffset(), Clipper2Lib::JoinType::Miter,
                                Clipper2Lib::EndType::Polygon, 2, 3);
      Clipper clipper;
      PathsD spd;
      clipper.AddSubject(spdi);
      clipper.Execute(Clipper2Lib::ClipType::Union, Clipper2Lib::FillRule::NonZero, spd);

      //-------------------------------
      //    fill polygons
      //-------------------------------

      Point currentPos(0, 0);
      for (int i = 0; i < fll->size(); ++i) {
            auto sl                   = &(*fll)[i];
            Clipper2Lib::RectD bounds = Clipper2Lib::GetBounds(spd);
            qreal angle               = sl->startAngle();
            for (int i = 0; i < sl->numPasses(); ++i) {
                  double interval = sl->interval();
                  // Compute both hatch directions in parallel using std::async.
                  // This avoids creating and destroying a std::thread per pass,
                  // which caused rapid thread churn (hundreds of threads per
                  // createFill() call when numPasses is large).
                  auto f1 = std::async(std::launch::async, [spd, bounds, angle, interval] {
                        Clipper cl;
                        return cl.hatch(spd, bounds, angle, interval);
                        });
                  clipper.Clear();
                  PathsD lines2 = clipper.hatch(spd, bounds, angle + 90.0, sl->interval());
                  PathsD lines1 = f1.get();

                  lineList.append_range(lines1);
                  lineList.append_range(lines2);

                  angle += sl->angleIncrement();
                  while (angle >= 180.0)
                        angle -= 180.0;
                  }
            }
      return lineList;
      }

//---------------------------------------------------------
//   optimize
//---------------------------------------------------------

static void optimize(LaserPath* lp, Clipper2Lib::PathsD lines) {
      Point currentPosition = lp->empty() ? Point(0, 0) : Point(lp->back().p.x(), lp->back().p.y());
      auto p                = optimizePath(lines, currentPosition);
      if (p.empty())
            return;
      currentPosition = p.front()[0];
      lp->moveTo(currentPosition.x, currentPosition.y);
      for (const auto& l : p) {
            if (l.size() == 2) {
                  if (!(qFuzzyCompare(currentPosition.x, l[0].x) && qFuzzyCompare(currentPosition.y, l[0].y)))
                        lp->moveTo(l[0].x, l[0].y);
                  lp->markTo(l[1].x, l[1].y);
                  }
            else {
                  Critical("=========not a line======");
                  }
            }
      }

//---------------------------------------------------------
//   toLaserPath
//    Convert LineSegments to LaserPath by connecting
//    the mark segments with move segments if necessary.
//---------------------------------------------------------

LaserPath LineSegments::toLaserPath() {
      LaserPath lp;
      if (empty())
            return LaserPath();
      Vec2d currentPosition = front().p1;
      lp.moveTo(currentPosition.x(), currentPosition.y());
      for (const auto& l : *this) {
            if (!(qFuzzyCompare(currentPosition.x(), l.p1.x()) &&
                  qFuzzyCompare(currentPosition.y(), l.p1.y())))
                  lp.moveTo(l.p1.x(), l.p1.y());
            lp.markTo(l.p2.x(), l.p2.y());
            currentPosition = l.p2;
            }
      return lp;
      }

//---------------------------------------------------------
//   check
//---------------------------------------------------------

bool LaserPath::check() {
      return true;
      Vec2d currentPosition = front().p;
      for (auto p = begin(); p != end(); ++p) {
            const auto& l = *p;
            if (qFuzzyCompare(currentPosition.x(), l.p.x()) && qFuzzyCompare(currentPosition.y(), l.p.y())) {
                  Critical("zero segment in LaserPath");
                  erase(p);
                  }
            currentPosition = l.p;
            }
      return true;
      }