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

#include "xmlreader.h"
#include "logger.h"

//---------------------------------------------------------
//   ~XmlReader
//---------------------------------------------------------

XmlReader::~XmlReader()
      {
      }

//---------------------------------------------------------
//   intAttribute
//---------------------------------------------------------

int XmlReader::intAttribute(QAnyStringView s, int _default) const
      {
      if (attributes().hasAttribute(s))
            // return attributes().value(s).toString().toInt();
            return attributes().value(s).toInt();
      else
            return _default;
      }

int XmlReader::intAttribute(QAnyStringView s) const
      {
      return attributes().value(s).toInt();
      }

bool XmlReader::boolAttribute(QAnyStringView s) const
      {
      return attributes().value(s) == u"true";
      }

//---------------------------------------------------------
//   doubleAttribute
//---------------------------------------------------------

double XmlReader::doubleAttribute(QAnyStringView s) const
      {
      return attributes().value(s).toDouble();
      }

double XmlReader::doubleAttribute(QAnyStringView s, double _default) const
      {
      if (attributes().hasAttribute(s))
            return attributes().value(s).toDouble();
      else
            return _default;
      }

//---------------------------------------------------------
//   attribute
//---------------------------------------------------------

QStringView XmlReader::attribute(QAnyStringView s, QStringView _default) const
      {
      if (attributes().hasAttribute(s))
            return attributes().value(s);
      else
            return _default;
      }

//---------------------------------------------------------
//   hasAttribute
//---------------------------------------------------------

bool XmlReader::hasAttribute(QAnyStringView s) const
      {
      return attributes().hasAttribute(s);
      }

//---------------------------------------------------------
//   readPoint
//---------------------------------------------------------

QPointF XmlReader::readPoint()
      {
      Q_ASSERT(tokenType() == QXmlStreamReader::StartElement);
      qreal x = doubleAttribute("x", 0.0);
      qreal y = doubleAttribute("y", 0.0);
      readNext();
      return QPointF(x, y);
      }

//---------------------------------------------------------
//   readVector3D
//---------------------------------------------------------

QVector3D XmlReader::readVector3D()
      {
      Q_ASSERT(tokenType() == QXmlStreamReader::StartElement);
      QVector3D p;
      p.setX(doubleAttribute("x", 0.0));
      p.setY(doubleAttribute("y", 0.0));
      p.setZ(doubleAttribute("z", 0.0));
      skipCurrentElement();
      return p;
      }

//---------------------------------------------------------
//   readVector2D
//---------------------------------------------------------

QVector2D XmlReader::readVector2D()
      {
      Q_ASSERT(tokenType() == QXmlStreamReader::StartElement);
      QVector2D p;
      p.setX(doubleAttribute("x", 0.0));
      p.setY(doubleAttribute("y", 0.0));
      skipCurrentElement();
      return p;
      }

//---------------------------------------------------------
//   readColor
//---------------------------------------------------------

QColor XmlReader::readColor()
      {
      Q_ASSERT(tokenType() == QXmlStreamReader::StartElement);
      QColor c;
      c.setRed(intAttribute("r"));
      c.setGreen(intAttribute("g"));
      c.setBlue(intAttribute("b"));
      c.setAlpha(intAttribute("a", 255));
      skipCurrentElement();
      return c;
      }

//---------------------------------------------------------
//   readSizeF
//---------------------------------------------------------

QSizeF XmlReader::readSizeF()
      {
      Q_ASSERT(tokenType() == QXmlStreamReader::StartElement);
      QSizeF p;
      p.setWidth(doubleAttribute("w", 0.0));
      p.setHeight(doubleAttribute("h", 0.0));
      skipCurrentElement();
      return p;
      }

//---------------------------------------------------------
//   readRect
//---------------------------------------------------------

QRectF XmlReader::readRect()
      {
      Q_ASSERT(tokenType() == QXmlStreamReader::StartElement);
      QRectF p;
      p.setX(doubleAttribute("x", 0.0));
      p.setY(doubleAttribute("y", 0.0));
      p.setWidth(doubleAttribute("w", 0.0));
      p.setHeight(doubleAttribute("h", 0.0));
      skipCurrentElement();
      return p;
      }

//---------------------------------------------------------
//   unknown
//    unknown tag read
//---------------------------------------------------------

void XmlReader::unknown()
      {
      if (QXmlStreamReader::error())
            Debug("{} ", errorString());
      if (!docName.isEmpty()) {
            Debug("tag in <{}> line {} col {}: <{}>", docName, lineNumber(), columnNumber(), name());
            }
      else {
            Critical("line {} col {}: {}", lineNumber(), columnNumber(), name());
            }
      QXmlStreamReader::skipCurrentElement();
      }

//---------------------------------------------------------
//   readDouble
//---------------------------------------------------------

double XmlReader::readDouble(double min, double max)
      {
      double val = readElementText().toDouble();
      if (val < min)
            val = min;
      else if (val > max)
            val = max;
      return val;
      }

//---------------------------------------------------------
//   readBool
//---------------------------------------------------------

bool XmlReader::readBool()
      {
      bool val;
      QXmlStreamReader::TokenType tt = readNext();
      if (tt == QXmlStreamReader::Characters) {
            if (text() == u"true")
                  val = true;
            else if (text() == u"false")
                  val = false;
            else
                  val = text().toInt() != 0;
            readNext();
            }
      else
            val = true;
      return val;
      }

//---------------------------------------------------------
//   htmlToString
//---------------------------------------------------------

void XmlReader::htmlToString(int level, QString* s)
      {
      *s += QString("<%1").arg(name().toString());
      for (const QXmlStreamAttribute& a : attributes())
            *s += QString(" %1=\"%2\"").arg(a.name().toString()).arg(a.value().toString());
      *s += ">";
      ++level;
      for (;;) {
            QXmlStreamReader::TokenType t = readNext();
            switch (t) {
                  case QXmlStreamReader::StartElement: htmlToString(level, s); break;
                  case QXmlStreamReader::EndElement:
                        *s += QString("</%1>").arg(name().toString());
                        --level;
                        return;
                  case QXmlStreamReader::Characters:
                        if (!isWhitespace())
                              *s += text().toString().toHtmlEscaped();
                        break;
                  case QXmlStreamReader::Comment: break;

                  default:
                        Debug("htmlToString: read token: {}", tokenString());
                        return;
                  }
            }
      }

//-------------------------------------------------------------------
//   readXml
//    read verbatim until end tag of current level is reached
//-------------------------------------------------------------------

QString XmlReader::readXml()
      {
      QString s;
      int level = 1;
      for (QXmlStreamReader::TokenType t = readNext(); t != QXmlStreamReader::EndElement; t = readNext()) {
            switch (t) {
                  case QXmlStreamReader::StartElement: htmlToString(level, &s); break;
                  case QXmlStreamReader::EndElement: break;
                  case QXmlStreamReader::Characters: s += text().toString().toHtmlEscaped(); break;
                  case QXmlStreamReader::Comment: break;

                  default:
                        Debug("htmlToString: read token: {}", tokenString());
                        return s;
                  }
            }
      return s;
      }
