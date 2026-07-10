//=============================================================================
//  wcam
//    CAM tool for gcode and fiber laser machines.
//
//  Copyright (C) 2025 Werner Schweer
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENSE.GPL
//=============================================================================

#pragma once

#include <QXmlStreamReader>
#include "logger.h"

#include <QColor>
#include <QFile>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QVector3D>

//---------------------------------------------------------
//   XmlReader
//---------------------------------------------------------

class XmlReader : public QXmlStreamReader {
      QString docName; // used for error reporting

      // For readahead possibility.
      // If needed, must be explicitly set by setReadAheadDevice.
//      QIODevice* _readAheadDevice = nullptr;

      void htmlToString(int level, QString*);

    public:
      XmlReader(QFile* f) : QXmlStreamReader(f), docName(f->fileName()) {}
      XmlReader(const QByteArray& d, const QString& st = QString()) : QXmlStreamReader(d), docName(st) {}
      XmlReader(QIODevice* d, const QString& st = QString()) : QXmlStreamReader(d), docName(st) {}
      XmlReader(const QString& d, const QString& st = QString()) : QXmlStreamReader(d), docName(st) {}
      XmlReader(const XmlReader&) = delete;
      XmlReader& operator=(const XmlReader&) = delete;
      ~XmlReader();

      void unknown();

      // attribute helper routines:
      QStringView attribute(QAnyStringView s) const { return attributes().value(s); }
      QStringView attribute(QAnyStringView s, QStringView _default) const;
      int intAttribute(QAnyStringView s) const;
      int intAttribute(QAnyStringView s, int _default) const;
      bool boolAttribute(QAnyStringView s) const;
      double doubleAttribute(QAnyStringView s) const;
      double doubleAttribute(QAnyStringView s, double _default) const;
      bool hasAttribute(QAnyStringView s) const;

      // helper routines based on readElementText():
      int readInt()         { return readElementText().toInt();      }
      int readInt(bool* ok) { return readElementText().toInt(ok);    }
      int readIntHex()      { return readElementText().toInt(0, 16); }
      double readDouble()   { return readElementText().toDouble();   }
      float readFloat()     { return readElementText().toFloat();    }
      bool readBool();

      double readDouble(double min, double max);
      QPointF readPoint();
      QVector3D readVector3D();
      QVector2D readVector2D();
      QSizeF readSizeF();
      QRectF readRect();
      QColor readColor();
      QString readXml();

      void setDocName(const QString& s) { docName = s; }
      QString getDocName() const { return docName; }
      };
