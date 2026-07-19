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

#include <QFont>
#include <QFontMetricsF>
#include <QFontInfo>
#include <QFontDatabase>
#include <QTextOption>
#include <QTransform>
#include <QTextLayout>
#include <QPainterPath>
#include <QRectF>

#include "logger.h"
//#include "clipper.h"
#include "painterpath.h"
#include "text.h"
#include "zcam.h"
// #include "undo.h"
#include "bezier.h"
// #include "handle.h"
#include "zcam.h"
enum TextUpdateFlags { FONT = 1, CURSOR_POS = 2, TEXT = 4 };

//---------------------------------------------------------
//   Text
//---------------------------------------------------------

Text::Text(ZCam* w, Element* parent) : Element3d(w, parent) {
      setName("");
      _fill = true;
      set_geometry(new TessGeometry(this));
      QJSEngine::setObjectOwnership(geometry(), QJSEngine::CppOwnership);

      connect(this, &Text::textChanged, [this]() { update(TEXT); });
      connect(this, &Text::fontFamilyChanged, [this]() { update(FONT); });
      connect(this, &Text::pointSizeChanged, [this]() { update(FONT); });
      connect(this, &Text::weightChanged, [this]() { update(FONT); });
      connect(this, &Text::stretchChanged, [this]() { update(FONT); });
      connect(this, &Text::letterSpacingChanged, [this]() { update(FONT); });
      connect(this, &Text::wordSpacingChanged, [this]() { update(FONT); });
      connect(this, &Text::lineSpacingChanged, [this]() { update(FONT); });
      connect(this, &Text::boldChanged, [this]() { update(FONT); });
      connect(this, &Text::italicChanged, [this]() { update(FONT); });
      connect(this, &Text::underlineChanged, [this]() { update(FONT); });
      connect(this, &Text::fillChanged, [this]() { update(TEXT); });
      connect(this, &Text::alignChanged, [this]() { update(TEXT); });
      }

//---------------------------------------------------------
//   fontLineSpacing
//---------------------------------------------------------

double Text::fontLineSpacing() const {
      return QFontMetrics(font).lineSpacing() * lineSpacing() * 0.01 * FONT_SCALE;
      }

//---------------------------------------------------------
//   updateCursor
//---------------------------------------------------------

void Text::updateCursor() {
      double x = 0.0;
      QFontMetrics fm(font);
      if (!text().isEmpty() && cursorRow < (int)lineOffsets.size()) {
            QStringList sl = text().split('\n');
            if (cursorRow < sl.size()) {
                  QString s = sl[cursorRow].left(cursorColumn);
                  QTextOption to;
                  to.setUseDesignMetrics(true);
                  x  = fm.horizontalAdvance(s, to) * FONT_SCALE;
                  x += lineOffsets[cursorRow] * FONT_SCALE;
                  }
            }
      double y       = -cursorRow * fontLineSpacing();
      double ascent  = -fm.ascent() * .7 * FONT_SCALE;
      double descent = ascent * .1;
      double w2      = fm.averageCharWidth() * .2 * FONT_SCALE;

      Clipper2Lib::PathsD lines;
      Clipper2Lib::PathD line1;
      line1.push_back({x - w2, y - ascent});
      line1.push_back({x + w2, y - ascent});
      lines.push_back(line1);
      Clipper2Lib::PathD line2;
      line2.push_back({x, y - ascent});
      line2.push_back({x, y + descent});
      lines.push_back(line2);
      Clipper2Lib::PathD line3;
      line3.push_back({x - w2, y + descent});
      line3.push_back({x + w2, y + descent});
      lines.push_back(line3);
      _cursorLines = lines;
      updateSelectionGeometry();
      emit selectionGeometryChanged();
      }

//---------------------------------------------------------
//   addText
//---------------------------------------------------------

void Text::addText(QList<QPolygonF>& polys, const QPointF& gpos, const QList<QGlyphRun>& glyphRuns) {
      for (auto gr : glyphRuns) {
            auto raw                 = gr.rawFont();
            QList<QPointF> positions = gr.positions();
            QList<quint32> indexes   = gr.glyphIndexes();
            for (int i = 0; i < indexes.size(); ++i) {
                  QPainterPath pp = raw.pathForGlyph(indexes[i]);
                  QPointF pos     = positions[i] + gpos;

                  // convert painterPath to polygon list
                  QPolygonF current;
                  for (int i = 0; i < pp.elementCount(); ++i) {
                        auto e = pp.elementAt(i);
                        auto p = pos + QPointF(e.x, -e.y);
                        switch (e.type) {
                              case QPainterPath::MoveToElement:
                                    if (current.size() > 1)
                                          polys += current;
                                    current.clear();
                                    current += p;
                                    break;
                              case QPainterPath::LineToElement: current += p; break;
                              case QPainterPath::CurveToElement: {
                                    Bezier bezier = Bezier::fromPoints(
                                        QPointF(pp.elementAt(i - 1).x, -pp.elementAt(i - 1).y) + pos, p,
                                        QPointF(pp.elementAt(i + 1).x, -pp.elementAt(i + 1).y) + pos,
                                        QPointF(pp.elementAt(i + 2).x, -pp.elementAt(i + 2).y) + pos);
                                    bezier.addToPolygon(&current, 0.05);
                                    i += 2;
                                    } break;
                              default: Fatal("bad element type"); break;
                              }
                        }
                  if (current.size() > 1)
                        polys += current;
                  }
            }
      }

//---------------------------------------------------------
//   update
//---------------------------------------------------------

void Text::update(int updateFlags) {
      if (updateFlags & FONT) {
            font = QFont(fontFamily());
            font.setPointSizeF(pointSize() * FONT_SCALE_UP);

            font.setWeight(QFont::Weight(weight()));
            font.setStretch(stretch());
            font.setLetterSpacing(QFont::PercentageSpacing, letterSpacing());
            font.setWordSpacing(wordSpacing());
            font.setBold(bold());
            font.setItalic(italic());
            font.setUnderline(underline());
            updateText();
            updateCursor();
            updateFlags &= ~(FONT | CURSOR_POS | TEXT);
            }
      if (updateFlags & TEXT)
            updateText();
      if (updateFlags & CURSOR_POS)
            updateCursor();
      updateFlags = 0;
      }

//---------------------------------------------------------
//   updateText
//---------------------------------------------------------

void Text::updateText() {
      geometry()->clear();
      if (fill())
            geometry()->setPrimitiveType(QQuick3DGeometry::PrimitiveType::Triangles);
      else
            geometry()->setPrimitiveType(QQuick3DGeometry::PrimitiveType::LineStrip);
      geometry()->addAttribute(QQuick3DGeometry::Attribute::PositionSemantic, 0,
                               QQuick3DGeometry::Attribute::F32Type);
      geometry()->addAttribute(QQuick3DGeometry::Attribute::IndexSemantic, 0,
                               QQuick3DGeometry::Attribute::U32Type);
      geometry()->setStride(3 * sizeof(float));

      QTextLayout layout;
      layout.setCacheEnabled(true);

      QTextOption opt = layout.textOption();
      opt.setUseDesignMetrics(true);
      layout.setTextOption(opt);
      layout.setFont(font);

      QList<QList<QGlyphRun>> glyphRuns;
      std::vector<qreal> lineWidth;
      double w       = 0.0; // max width of text lines
      QStringList sl = text().split('\n');
      for (auto t : sl) {
            layout.setText(t);
            layout.beginLayout();
            QTextLine line = layout.createLine();
            layout.endLayout();
            qreal ww = line.naturalTextWidth();
            w        = std::max(w, ww);
            lineWidth.push_back(ww);
            glyphRuns += line.glyphRuns();
            }

      QFontMetrics fm(font);
      QList<QPolygonF> poly;
      double y  = -fm.ascent();
      double ls = -fm.lineSpacing() * lineSpacing() * 0.01;
      int i     = 0;
      lineOffsets.clear();
      for (auto t : sl) {
            qreal x = 0.0;
            if (align() == Qt::AlignHCenter) {
                  qreal ww = lineWidth[i];
                  x        = (w - ww) * .5;
                  }
            else if (align() == Qt::AlignRight) {
                  qreal ww = lineWidth[i];
                  x        = w - ww;
                  }
            lineOffsets.push_back(x);
            addText(poly, QPointF(x, y), glyphRuns[i]);
            y += ls;
            ++i;
            }
      _pathList.clear();
      for (const auto& polygon : poly) {
            Path2d pg;
            for (auto pp : polygon) {
                  auto p = Vec2d(pp.x() * FONT_SCALE, pp.y() * FONT_SCALE);
                  pg.push_back(p);
                  }
            _pathList.push_back(pg);
            }
      _pathList.setFill(fill());
      geometry()->setPolygons(_pathList);
      updateSelectionGeometry();
      emit geometryChanged();
      }

//---------------------------------------------------------
//   setEditing
//---------------------------------------------------------

void Text::setEditing(bool v) {
      if (_editing == v)
            return;
      _editing = v;
      emit editingChanged();
      if (_editing) {
            cursorRow    = 0;
            cursorColumn = 0;
            // Ensure font is initialized before computing cursor position
            if (font.family().isEmpty()) {
                  font = QFont(fontFamily());
                  font.setPointSizeF(pointSize() * FONT_SCALE_UP);
                  font.setWeight(QFont::Weight(weight()));
                  font.setStretch(stretch());
                  font.setLetterSpacing(QFont::PercentageSpacing, letterSpacing());
                  font.setWordSpacing(wordSpacing());
                  }
            }
      updateCursor();
      }

//---------------------------------------------------------
//   updateSelectionGeometry
//    When editing, show the bounding box + cursor lines.
//    When not editing, show the normal bounding box.
//---------------------------------------------------------

void Text::updateSelectionGeometry() {
      if (!_selectionGeometry)
            return;
      if (_editing) {
            // Combine bounding box + cursor lines
            Clipper2Lib::PathsD lines;
            // Bounding box edges
            QRectF bbox = boundingBox();
            if (bbox.isNull() || bbox.isEmpty()) {
                  // When text is empty, show a minimal bounding box
                  // based on the font metrics so the cursor is visible.
                  QFontMetrics fm(font);
                  double w = fm.averageCharWidth() * FONT_SCALE;
                  double h = fm.lineSpacing() * FONT_SCALE;
                  bbox     = QRectF(0.0, -fm.ascent() * FONT_SCALE, w, h);
                  }
            Clipper2Lib::PathD rect;
            rect.push_back({bbox.left(), bbox.top()});
            rect.push_back({bbox.right(), bbox.top()});
            rect.push_back({bbox.right(), bbox.top()});
            rect.push_back({bbox.right(), bbox.bottom()});
            rect.push_back({bbox.right(), bbox.bottom()});
            rect.push_back({bbox.left(), bbox.bottom()});
            rect.push_back({bbox.left(), bbox.bottom()});
            rect.push_back({bbox.left(), bbox.top()});
            lines.push_back(rect);
            // Cursor lines
            for (const auto& cl : _cursorLines)
                  lines.push_back(cl);
            _selectionGeometry->setLines(lines);
            emit selectionGeometryChanged();
            }
      else
            Element3d::updateSelectionGeometry();
      }

//---------------------------------------------------------
//   setCursorPositionFromWorld
//    Set the cursor (row/column) based on a world-space position.
//    Returns true if the position was within the text bounding box.
//---------------------------------------------------------

bool Text::setCursorPositionFromWorld(const QVector3D& worldPos) {
      // Convert world position to local coordinates
      QMatrix4x4 inv = globalMatrix();
      bool ok;
      inv = inv.inverted(&ok);
      if (!ok)
            return false;
      QVector3D localPos = inv.map(worldPos);

      // Text layout coordinates are scaled by FONT_SCALE relative to
      // the local coordinate system, so divide to get layout coords.
      double lx = localPos.x() / FONT_SCALE;
      double ly = localPos.y() / FONT_SCALE;

      // Check if the click is within the bounding box
      QRectF bbox = boundingBox();
      if (bbox.isNull() || bbox.isEmpty())
            return false;
      if (lx < bbox.left() / FONT_SCALE - 2.0 || lx > bbox.right() / FONT_SCALE + 2.0 ||
          ly < bbox.top() / FONT_SCALE - 2.0 || ly > bbox.bottom() / FONT_SCALE + 2.0)
            return false;

      // Determine which row the click falls on.
      // In text layout coordinates, row 0 starts at y = -fm.ascent().
      // Each subsequent row advances by ls (negative, going downward).
      // So row i has its vertical position at y = -ascent + i * ls.
      QFontMetricsF fm(font);
      double ascent = fm.ascent();
      double ls     = -fm.lineSpacing() * lineSpacing() * 0.01; // negative, same as in updateText

      QStringList sl = text().split('\n');
      int numRows    = sl.size();
      if (numRows == 0)
            return false;

      // rowF = (ly - (-ascent)) / ls   — ls is negative so this gives
      // 0 for row 0, 1 for row 1, etc.
      double rowF = (ly - (-ascent)) / ls;
      int newRow  = std::round(rowF);
      if (newRow < 0)
            newRow = 0;
      if (newRow >= numRows)
            newRow = numRows - 1;

      // Determine the column within the row.
      // The x position of the start of each row in layout coords is lineOffsets[row].
      // The cursor x in layout coords is lx - lineOffsets[row].
      double rowX = lx - lineOffsets[newRow];

      // Find the column whose horizontal advance is closest to rowX.
      QString rowText = sl[newRow];
      QTextOption to;
      to.setUseDesignMetrics(true);
      int newCol      = 0;
      double bestDist = std::numeric_limits<double>::max();
      for (int c = 0; c <= rowText.size(); ++c) {
            QString s   = rowText.left(c);
            double adv  = fm.horizontalAdvance(s, to);
            double dist = std::abs(adv - rowX);
            if (dist < bestDist) {
                  bestDist = dist;
                  newCol   = c;
                  }
            }

      cursorRow    = newRow;
      cursorColumn = newCol;
      update(CURSOR_POS);
      return true;
      }

//---------------------------------------------------------
//   keyEvent
//---------------------------------------------------------

bool Text::keyEvent(int key, int modifiers, const QString& s) {
      QStringList sl = text().split('\n');

      switch (key) {
            case Qt::Key_Left:
                  if (cursorColumn) {
                        --cursorColumn;
                        update(CURSOR_POS);
                        }
                  else if (cursorRow) {
                        --cursorRow;
                        cursorColumn = sl[cursorRow].size();
                        update(CURSOR_POS);
                        }
                  break;
            case Qt::Key_Right:
                  if (cursorColumn < sl[cursorRow].size()) {
                        ++cursorColumn;
                        update(CURSOR_POS);
                        }
                  else if (cursorRow < (sl.size() - 1)) {
                        ++cursorRow;
                        cursorColumn = 0;
                        update(CURSOR_POS);
                        }
                  break;
            case Qt::Key_Delete:
            case Qt::Key_Backspace:
                  if (cursorColumn) {
                        sl[cursorRow].removeAt(--cursorColumn);
                        set_text(sl.join('\n'));
                        update(CURSOR_POS);
                        }
                  else if (cursorRow) {
                        QString rs = sl[cursorRow];
                        sl.removeAt(cursorRow);
                        --cursorRow;
                        cursorColumn   = sl[cursorRow].size();
                        sl[cursorRow] += rs;
                        set_text(sl.join('\n'));
                        update(CURSOR_POS);
                        }
                  break;
            case Qt::Key_Up:
                  if (cursorRow) {
                        --cursorRow;
                        if (sl[cursorRow].size() < cursorColumn)
                              cursorColumn = sl[cursorRow].size();
                        update(CURSOR_POS);
                        }
                  break;
            case Qt::Key_Down:
                  if (cursorRow < sl.size() - 1) {
                        ++cursorRow;
                        if (sl[cursorRow].size() < cursorColumn)
                              cursorColumn = sl[cursorRow].size();
                        update(CURSOR_POS);
                        }
                  break;
            case Qt::Key_Return: {
                  int n = sl[cursorRow].size();
                  QString rs;
                  if (n > cursorColumn) {
                        rs            = sl[cursorRow].right(n - cursorColumn);
                        sl[cursorRow] = sl[cursorRow].left(cursorColumn);
                        }
                  if (sl.size() <= cursorRow)
                        sl += rs;
                  else
                        sl.insert(cursorRow + 1, rs);
                  ++cursorRow;
                  cursorColumn = 0;
                  set_text(sl.join('\n'));
                  update(CURSOR_POS);
                  } break;

            default:
                  if (!s.isEmpty()) {
                        QString& t = sl[cursorRow];
                        t.insert(cursorColumn, s);
                        cursorColumn += s.size();
                        set_text(sl.join('\n'));
                        update();
                        }
                  break;
            }
      return true;
      }
