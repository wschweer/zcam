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
#if 0
      Dictionary* d = new Dictionary;
      d->push_back(   {  "left",   Qt::AlignLeft  });
      d->push_back(   {  "center",  Qt::AlignCenter });
      d->push_back(   {  "right",   Qt::AlignRight  });
      alignProperty->setDictionary(d);
#endif

      set_geometry(new TessGeometry(this));
      QJSEngine::setObjectOwnership(geometry(), QJSEngine::CppOwnership);

      //      _subElements.push_back(new Bbox(zcam, this, TextSubelement::BboxSub, nullptr));
      //      _subElements.push_back(new Cursor(zcam, this, TextSubelement::CursorSub, nullptr));

      connect(this, &Text::textChanged, [this]() { update(TEXT); });
      connect(this, &Text::fontFamilyChanged, [this]() { update(FONT); });
      connect(this, &Text::pointSizeChanged, [this]() { update(FONT); });
      connect(this, &Text::weightChanged, [this]() { update(FONT); });
      connect(this, &Text::stretchChanged, [this]() { update(FONT); });
      connect(this, &Text::letterSpacingChanged, [this]() { update(FONT); });
      connect(this, &Text::wordSpacingChanged, [this]() { update(FONT); });
      connect(this, &Text::lineSpacingChanged, [this]() { update(FONT); });
      connect(this, &Text::fillChanged, [this]() { update(TEXT); });
      connect(this, &Text::alignChanged, [this]() { update(TEXT); });

      //      setEditMode(false);
      update(-1);
      }

Text::~Text() {
      }

//---------------------------------------------------------
//   fontLineSpacing
//---------------------------------------------------------

double Text::fontLineSpacing() const {
      return fontMetrics->lineSpacing() * lineSpacing() * 0.01 * FONT_SCALE;
      }

//---------------------------------------------------------
//   updateCursor
//---------------------------------------------------------

void Text::updateCursor() {
      double x = 0.0;
      if (!text().isEmpty()) {
            QStringList sl = text().split('\n');
            QString s      = sl[cursorRow].left(cursorColumn);
            QTextOption to;
            to.setUseDesignMetrics(true);
            x  = fontMetrics->horizontalAdvance(s, to) * FONT_SCALE;
            x += lineOffsets[cursorRow] * FONT_SCALE;
            }
      double y       = -cursorRow * fontLineSpacing();
      double ascent  = -fontMetrics->ascent() * .7 * FONT_SCALE;
      double descent = ascent * .1;
      double w2      = fontMetrics->averageCharWidth() * .2 * FONT_SCALE;

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
      //      cursor()->geometry()->setLines(lines);
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
                                    Bezier bezier = Bezier::fromPoints(QPointF(pp.elementAt(i - 1).x, -pp.elementAt(i - 1).y) + pos, p,
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
      updateFlags = -1;
      if (updateFlags & FONT) {
            font = QFont(fontFamily());
            font.setPointSizeF(pointSize() * FONT_SCALE_UP);

            font.setWeight(QFont::Weight(weight()));
            font.setStretch(stretch());
            font.setLetterSpacing(QFont::PercentageSpacing, letterSpacing());
            font.setWordSpacing(wordSpacing());
            if (fontMetrics)
                  delete fontMetrics;
            fontMetrics  = new QFontMetricsF(font);
            QFontInfo fi = QFontInfo(font);
            updateText();
            updateCursor();
            updateFlags &= ~(FONT | CURSOR_POS | TEXT);
            }
      if (updateFlags & TEXT)
            updateText();
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
      geometry()->addAttribute(QQuick3DGeometry::Attribute::PositionSemantic, 0, QQuick3DGeometry::Attribute::F32Type);
      geometry()->addAttribute(QQuick3DGeometry::Attribute::IndexSemantic, 0, QQuick3DGeometry::Attribute::U32Type);
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

      QList<QPolygonF> poly;
      double y  = -fontMetrics->ascent();
      double ls = -fontMetrics->lineSpacing() * lineSpacing() * 0.01;
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
      }

//---------------------------------------------------------
//   setEditMode
//---------------------------------------------------------

#if 0
bool Text::setEditMode(bool val)
            {
      _subElements[TextSubelement::BboxSub]->setActive(val);
      _subElements[TextSubelement::CursorSub]->setActive(val);
      return true;
            }

//---------------------------------------------------------
//   keyEvent
//---------------------------------------------------------

bool Text::keyEvent(int key, int modifiers, const QString& s)
            {
      Debug("Key Event");
      QStringList sl = text().split('\n');

      switch(key) {
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
                  else if (cursorRow < (sl.size()-1)) {
                        ++cursorRow;
                        cursorColumn = 0;
                        update(CURSOR_POS);
                              }
                  break;
            case Qt::Key_Delete:
            case Qt::Key_Backspace:
                  if (cursorColumn) {
                        sl[cursorRow].removeAt(--cursorColumn);
                        zcam->undoChangeProperty(this, "text", sl.join('\n'));
                        update(CURSOR_POS);
                              }
                  else  if (cursorRow) {
                        QString rs = sl[cursorRow];
                        sl.removeAt(cursorRow);
                        --cursorRow;
                        cursorColumn = sl[cursorRow].size();
                        sl[cursorRow] += rs;
                        zcam->undoChangeProperty(this, "text", sl.join('\n'));
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
                  if (cursorRow < sl.size()-1) {
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
                        rs = sl[cursorRow].right(n- cursorColumn);
                        sl[cursorRow] = sl[cursorRow].left(cursorColumn);
                              }
                  if (sl.size() <= cursorRow)
                        sl += rs;
                  else
                        sl.insert(cursorRow+1, rs);
                  ++cursorRow;
                  cursorColumn = 0;
                  zcam->undoChangeProperty(this, "text", sl.join('\n'));
                  update(CURSOR_POS);
                        }
                  break;

            default:
                  if (!s.isEmpty()) {
                        Debug("{} {}: insert <{}> into <{}>", cursorColumn, cursorRow, s, sl[0]);
                        QString& t = sl[cursorRow];
                        t.insert(cursorColumn, s);
                        cursorColumn += s.size();
                        zcam->undoChangeProperty(this, "text", sl.join('\n'));
                        update();
                              }
                  break;
                  }
      return true;
            }
#endif
