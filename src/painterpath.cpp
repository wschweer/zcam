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

#include <QTextLayout>
#include <QPainterPath>
#include "logger.h"
#include "painterpath.h"
#include "bezier.h"
// #include "xmlreader.h"
// #include "xmlwriter.h"
#include "types.h"

#define QT_PATH_KAPPA 0.5522847498

//---------------------------------------------------------
//   toPathList
//---------------------------------------------------------

PathList PainterPath::toPathList() const {
      PathList pathList;
      Path2d path;
      for (int i = 0; i < size(); ++i) {
            const auto& ppe = (*this)[i];
            switch (ppe.type) {
                  case PPType::MoveTo:
                        if (!path.empty())
                              pathList.push_back(path);
                        path.clear();
                        path.emplace_back(ppe.x(), ppe.y());
                        break;

                  case PPType::LineTo: path.emplace_back(ppe.x(), ppe.y()); break;

                  case PPType::CurveTo: {
                        Bezier bezier = Bezier::fromPoints((*this)[i - 1].pos, ppe.pos, (*this)[i + 1].pos, (*this)[i + 2].pos);
                        bezier.addToPolygon(path);
                        i += 2;
                        } break;
                  case PPType::CurveToData1:
                  case PPType::CurveToData2: Critical("bad element type"); break;
                  }
            }
      if (!path.empty())
            pathList.push_back(path);
      return pathList;
      }

//---------------------------------------------------------
//   addText
//---------------------------------------------------------

void PainterPath::addText(const QPointF& gpos, const QFont& f, const QString& text) {
      if (text.isEmpty())
            return;

      QTextLayout layout(text, f);
      layout.setCacheEnabled(true);

      QTextOption opt = layout.textOption();
      opt.setUseDesignMetrics(true);
      layout.setTextOption(opt);

      layout.beginLayout();
      QTextLine line = layout.createLine();
      layout.endLayout();
      if (!line.isValid()) {
            Debug("====invalid line");
            return;
            }
      auto lpos = QPointF(gpos.x(), gpos.y() - line.ascent() - line.leading());

      for (auto gr : line.glyphRuns()) {
            auto raw                 = gr.rawFont();
            QList<QPointF> positions = gr.positions();
            QList<quint32> indexes   = gr.glyphIndexes();
            for (int i = 0; i < indexes.size(); ++i) {
                  QPainterPath pp = raw.pathForGlyph(indexes[i]);
                  QPointF pos     = positions[i] + lpos;
                  for (int i = 0; i < pp.elementCount(); ++i) {
                        auto e = pp.elementAt(i);
                        auto p = pos + QPointF(e.x, -e.y);
                        switch (e.type) {
                              case QPainterPath::MoveToElement: moveTo({p.x(), p.y()}); break;
                              case QPainterPath::LineToElement: lineTo({p.x(), p.y()}); break;
                              case QPainterPath::CurveToElement:
                                    push_back({
                                       PPType::CurveTo, {p.x(), p.y()}
                                          });
                                    break;
                              case QPainterPath::CurveToDataElement:
                                    if (back().type == PPType::CurveToData1)
                                          push_back({
                                             PPType::CurveToData2, {p.x(), p.y()}
                                                });
                                    else
                                          push_back({
                                             PPType::CurveToData1, {p.x(), p.y()}
                                                });
                                    break;
                              }
                        }
                  }
            }
      }

#if 0

//---------------------------------------------------------
//   read
//---------------------------------------------------------

void PainterPath::read(XmlReader& r)
            {
      clear();
      while (r.readNextStartElement()) {
            const auto& n = r.name();
            if (n == u"p") {
                  Vec2d p(r.doubleAttribute("x"), r.doubleAttribute("y"));
                  auto t = PPType(r.intAttribute("t"));
                  if (t == PPType::CurveToData1 && back().type == PPType::CurveToData1)
                        push_back({PPType::CurveToData2, p});  // fix bad data
                  else
                        push_back({t, p});
                  r.readNext();
                        }
            else
                  r.unknown();
                  }
            }

//---------------------------------------------------------
//   write
//---------------------------------------------------------

void PainterPath::write(XmlWriter& w) const
            {
      w.stag("Path");
      for (const auto& p : (*this))
            w.tagE("p", "t=\"{}\" x=\"{}\" y=\"{}\"", int(p.type), p.x(), p.y());
      w.etag();
            }
#endif

//---------------------------------------------------------
//   remove
//---------------------------------------------------------

void PainterPath::eraseElement(int idx) {
      int n = size();
      if (idx >= n || idx < 0) {
            Critical("bad index {} >= {}", idx, n);
            return;
            }
      switch ((*this)[idx].type) {
            case PPType::MoveTo:
            case PPType::LineTo:
                  erase(begin() + idx);
                  if (idx == 0)
                        (*this)[idx].type = PPType::MoveTo;
                  break;
            case PPType::CurveToData1:
            case PPType::CurveToData2:
                  --idx;
                  if ((*this)[idx].type == PPType::CurveToData1)
                        --idx;
                  // fall through:
            case PPType::CurveTo: erase(begin() + idx, begin() + idx + 3); break;
            }
      }

//---------------------------------------------------------
//   makeSpline
//---------------------------------------------------------

void PainterPath::makeSpline(int idx) {
      --idx;
      (*this)[idx].type = PPType::CurveTo;

      auto p1   = (*this)[idx - 1].pos;
      auto p2   = (*this)[idx].pos;
      double dx = (p2.x() - p1.x()) * .33;
      double dy = (p2.y() - p1.y()) * .33;

      (*this)[idx].pos = p1 + Vec3d(dx, dy, 0);

      push_back({PPType::CurveToData1, (*this)[idx].pos + Vec3d(dx, dy, 0)});
      push_back({PPType::CurveToData2, p2});
      }

void PainterPath::insertElement(int idx, const PPElement& e) {
      }

//---------------------------------------------------------
//   qt_t_for_arc_angle
//
//    For a given angle in the range [0 .. 90], finds the corresponding parameter t
//    of the prototype cubic bezier arc segment
//    b = fromPoints(QPointF(1, 0), QPointF(1, KAPPA), QPointF(KAPPA, 1), QPointF(0, 1));
//
//    From the bezier equation:
//    b.pointAt(t).x() = (1-t)^3 + t*(1-t)^2 + t^2*(1-t)*KAPPA
//    b.pointAt(t).y() = t*(1-t)^2 * KAPPA + t^2*(1-t) + t^3
//
//    Third degree coefficients:
//    b.pointAt(t).x() = at^3 + bt^2 + ct + d
//    where a = 2-3*KAPPA, b = 3*(KAPPA-1), c = 0, d = 1
//
//    b.pointAt(t).y() = at^3 + bt^2 + ct + d
//    where a = 3*KAPPA-2, b = 6*KAPPA+3, c = 3*KAPPA, d = 0
//
//    Newton's method to find the zero of a function:
//    given a function f(x) and initial guess x_0
//    x_1 = f(x_0) / f'(x_0)
//    x_2 = f(x_1) / f'(x_1)
//    etc...
//---------------------------------------------------------

qreal qt_t_for_arc_angle(qreal angle) {
      if (qFuzzyIsNull(angle))
            return 0;

      if (qFuzzyCompare(angle, qreal(90)))
            return 1;

      qreal radians  = qDegreesToRadians(angle);
      qreal cosAngle = qCos(radians);
      qreal sinAngle = qSin(radians);

      // initial guess
      qreal tc = angle / 90;
      // do some iterations of newton's method to approximate cosAngle
      // finds the zero of the function b.pointAt(tc).x() - cosAngle
      tc -= ((((2 - 3 * QT_PATH_KAPPA) * tc + 3 * (QT_PATH_KAPPA - 1)) * tc) * tc + 1 - cosAngle) // value
            / (((6 - 9 * QT_PATH_KAPPA) * tc + 6 * (QT_PATH_KAPPA - 1)) * tc);                    // derivative
      tc -= ((((2 - 3 * QT_PATH_KAPPA) * tc + 3 * (QT_PATH_KAPPA - 1)) * tc) * tc + 1 - cosAngle) // value
            / (((6 - 9 * QT_PATH_KAPPA) * tc + 6 * (QT_PATH_KAPPA - 1)) * tc);                    // derivative

      // initial guess
      qreal ts = tc;
      // do some iterations of newton's method to approximate sinAngle
      // finds the zero of the function b.pointAt(tc).y() - sinAngle
      ts -= ((((3 * QT_PATH_KAPPA - 2) * ts - 6 * QT_PATH_KAPPA + 3) * ts + 3 * QT_PATH_KAPPA) * ts - sinAngle) /
            (((9 * QT_PATH_KAPPA - 6) * ts + 12 * QT_PATH_KAPPA - 6) * ts + 3 * QT_PATH_KAPPA);
      ts -= ((((3 * QT_PATH_KAPPA - 2) * ts - 6 * QT_PATH_KAPPA + 3) * ts + 3 * QT_PATH_KAPPA) * ts - sinAngle) /
            (((9 * QT_PATH_KAPPA - 6) * ts + 12 * QT_PATH_KAPPA - 6) * ts + 3 * QT_PATH_KAPPA);

      // use the average of the t that best approximates cosAngle
      // and the t that best approximates sinAngle
      qreal t = 0.5 * (tc + ts);
      return t;
      }

//---------------------------------------------------------
//   qt_find_ellipse_coords
//---------------------------------------------------------

void qt_find_ellipse_coords(const QRectF& r, qreal angle, qreal length, QPointF* startPoint, QPointF* endPoint) {
      if (r.isNull()) {
            if (startPoint)
                  *startPoint = QPointF();
            if (endPoint)
                  *endPoint = QPointF();
            return;
            }

      qreal w2 = r.width() / 2;
      qreal h2 = r.height() / 2;

      qreal angles[2]    = {angle, angle + length};
      QPointF* points[2] = {startPoint, endPoint};

      for (int i = 0; i < 2; ++i) {
            if (!points[i])
                  continue;

            qreal theta = angles[i] - 360 * qFloor(angles[i] / 360);
            qreal t     = theta / 90;
            // truncate
            int quadrant  = int(t);
            t            -= quadrant;

            t = qt_t_for_arc_angle(90 * t);

            // swap x and y?
            if (quadrant & 1)
                  t = 1 - t;

            qreal a, b, c, d;
            Bezier::coefficients(t, a, b, c, d);
            QPointF p(a + b + c * QT_PATH_KAPPA, d + c + b * QT_PATH_KAPPA);

            // left quadrants
            if (quadrant == 1 || quadrant == 2)
                  p.rx() = -p.x();

            // top quadrants
            if (quadrant == 0 || quadrant == 1)
                  p.ry() = -p.y();

            *points[i] = r.center() + QPointF(w2 * p.x(), h2 * p.y());
            }
      }

/*!
    Creates a number of curves for a given arc definition. The arc is
    defined an arc along the ellipses that fits into \a rect starting
    at \a startAngle and an arc length of \a sweepLength.

    The function has three out parameters. The return value is the
    starting point of the arc. The \a curves array represents the list
    of cubicTo elements up to a maximum of \a point_count. There are of course
    3 points pr curve.
*/

static QPointF qt_curves_for_arc(const QRectF& rect, qreal startAngle, qreal sweepLength, QPointF* curves, int* point_count) {
      Assert(point_count);
      Assert(curves);

      *point_count = 0;
      if (std::isnan(rect.x()) || std::isnan(rect.y()) || std::isnan(rect.width()) || std::isnan(rect.height()) || std::isnan(startAngle) ||
          std::isnan(sweepLength)) {
            Critical("QPainterPath::arcTo: Adding arc where a parameter is NaN, results are undefined");
            return QPointF();
            }
      if (rect.isNull())
            return QPointF();

      qreal x = rect.x();
      qreal y = rect.y();

      qreal w   = rect.width();
      qreal w2  = rect.width() / 2;
      qreal w2k = w2 * QT_PATH_KAPPA;

      qreal h   = rect.height();
      qreal h2  = rect.height() / 2;
      qreal h2k = h2 * QT_PATH_KAPPA;

      QPointF points[16] = {// start point
                            QPointF(x + w, y + h2),

                            // 0 -> 270 degrees
                            QPointF(x + w, y + h2 + h2k), QPointF(x + w2 + w2k, y + h), QPointF(x + w2, y + h),

                            // 270 -> 180 degrees
                            QPointF(x + w2 - w2k, y + h), QPointF(x, y + h2 + h2k), QPointF(x, y + h2),

                            // 180 -> 90 degrees
                            QPointF(x, y + h2 - h2k), QPointF(x + w2 - w2k, y), QPointF(x + w2, y),

                            // 90 -> 0 degrees
                            QPointF(x + w2 + w2k, y), QPointF(x + w, y + h2 - h2k), QPointF(x + w, y + h2)};

      if (sweepLength > 360)
            sweepLength = 360;
      else if (sweepLength < -360)
            sweepLength = -360;

      // Special case fast paths
      if (startAngle == 0.0) {
            if (sweepLength == 360.0) {
                  for (int i = 11; i >= 0; --i)
                        curves[(*point_count)++] = points[i];
                  return points[12];
                  }
            else if (sweepLength == -360.0) {
                  for (int i = 1; i <= 12; ++i)
                        curves[(*point_count)++] = points[i];
                  return points[0];
                  }
            }

      int startSegment = int(qFloor(startAngle / 90));
      int endSegment   = int(qFloor((startAngle + sweepLength) / 90));

      qreal startT = (startAngle - startSegment * 90) / 90;
      qreal endT   = (startAngle + sweepLength - endSegment * 90) / 90;

      int delta = sweepLength > 0 ? 1 : -1;
      if (delta < 0) {
            startT = 1 - startT;
            endT   = 1 - endT;
            }

      // avoid empty start segment
      if (qFuzzyIsNull(startT - qreal(1))) {
            startT        = 0;
            startSegment += delta;
            }

      // avoid empty end segment
      if (qFuzzyIsNull(endT)) {
            endT        = 1;
            endSegment -= delta;
            }

      startT = qt_t_for_arc_angle(startT * 90);
      endT   = qt_t_for_arc_angle(endT * 90);

      const bool splitAtStart = !qFuzzyIsNull(startT);
      const bool splitAtEnd   = !qFuzzyIsNull(endT - qreal(1));

      const int end = endSegment + delta;

      // empty arc?
      if (startSegment == end) {
            const int quadrant = 3 - ((startSegment % 4) + 4) % 4;
            const int j        = 3 * quadrant;
            return delta > 0 ? points[j + 3] : points[j];
            }

      QPointF startPoint, endPoint;
      qt_find_ellipse_coords(rect, startAngle, sweepLength, &startPoint, &endPoint);

      for (int i = startSegment; i != end; i += delta) {
            const int quadrant = 3 - ((i % 4) + 4) % 4;
            const int j        = 3 * quadrant;

            Bezier b;
            if (delta > 0)
                  b = Bezier::fromPoints(points[j + 3], points[j + 2], points[j + 1], points[j]);
            else
                  b = Bezier::fromPoints(points[j], points[j + 1], points[j + 2], points[j + 3]);

            // empty arc?
            if (startSegment == endSegment && qFuzzyCompare(startT, endT))
                  return startPoint;

            if (i == startSegment) {
                  if (i == endSegment && splitAtEnd)
                        b = b.bezierOnInterval(startT, endT);
                  else if (splitAtStart)
                        b = b.bezierOnInterval(startT, 1);
                  }
            else if (i == endSegment && splitAtEnd) {
                  b = b.bezierOnInterval(0, endT);
                  }

            // push control points
            curves[(*point_count)++] = b.pt2();
            curves[(*point_count)++] = b.pt3();
            curves[(*point_count)++] = b.pt4();
            }

      Assert(*point_count > 0);
      curves[*(point_count)-1] = endPoint;

      return startPoint;
      }

//---------------------------------------------------------
//   arcTo
//---------------------------------------------------------

void PainterPath::arcTo(const QRectF& rect, double startAngle, double sweepLength) {
      if (rect.isNull())
            return;

      int point_count;
      QPointF pts[15];
      QPointF curve_start = qt_curves_for_arc(rect, startAngle, sweepLength, pts, &point_count);

      lineTo(curve_start);
      for (int i = 0; i < point_count; i += 3)
            cubicTo({pts[i].x(), pts[i].y()}, {pts[i + 1].x(), pts[i + 1].y()}, {pts[i + 2].x(), pts[i + 2].y()});
      }

//---------------------------------------------------------
//   quadTo
//---------------------------------------------------------

void PainterPath::quadTo(const Vec2d& c, const Vec2d& e) {
      const PPElement& elm = back();
      Vec2d prev(elm.x(), elm.y());

      // Abort on empty curve as a stroker cannot handle this and the
      // curve is irrelevant anyway.
      if (prev == c && c == e)
            return;

      QPointF c1((prev.x() + 2 * c.x()) / 3, (prev.y() + 2 * c.y()) / 3);
      QPointF c2((e.x() + 2 * c.x()) / 3, (e.y() + 2 * c.y()) / 3);
      cubicTo(c1, c2, e);
      }

//---------------------------------------------------------
//   isClosed
//---------------------------------------------------------

bool PainterPath::isClosed() const {
      if (size() < 2)
            return false;
      const PPElement& first = front();
      const PPElement& last  = back();
      return qFuzzyCompare(first.pos.x(), last.pos.x()) && qFuzzyCompare(first.pos.y(), last.pos.y());
      }

//---------------------------------------------------------
//   closeSubpath
//---------------------------------------------------------

void PainterPath::closeSubpath() {
      const PPElement& first = front();
      PPElement& last        = back();
      if (!isClosed() && size() >= 2) {
            if (qFuzzyCompare(first.x(), last.x()) && qFuzzyCompare(first.y(), last.y()))
                  last.pos = first.pos;
            else
                  push_back({PPType::LineTo, first.pos});
            }
      }

//---------------------------------------------------------
//   addEllipse
//---------------------------------------------------------

void PainterPath::addEllipse(const QRectF& boundingRect) {
      if (boundingRect.isNull())
            return;

      QPointF pts[12];
      int point_count;
      QPointF start = qt_curves_for_arc(boundingRect, 0, -360, pts, &point_count);

      moveTo(start);
      cubicTo(pts[0], pts[1], pts[2]);   // 0 -> 270
      cubicTo(pts[3], pts[4], pts[5]);   // 270 -> 180
      cubicTo(pts[6], pts[7], pts[8]);   // 180 -> 90
      cubicTo(pts[9], pts[10], pts[11]); // 90 - >0
      }
