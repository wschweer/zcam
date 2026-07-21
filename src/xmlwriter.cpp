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

#include <QColor>
#include <QRect>

#include "logger.h"
#include "xmlwriter.h"

//---------------------------------------------------------
//   putLevel
//---------------------------------------------------------

void XmlWriter::putLevel() {
      int level = stack.size();
      for (int i = 0; i < level * 2; ++i)
            *os << ' ';
      }

//---------------------------------------------------------
//   header
//---------------------------------------------------------

void XmlWriter::header() {
      *os << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
      }

//---------------------------------------------------------
//   stag
//    <mops>
//---------------------------------------------------------

void XmlWriter::stag(const std::string& s) {
      putLevel();
      *os << '<' << s << ">\n";
      stack.push(s);
      }

//---------------------------------------------------------
//   etag
//    </mops>
//---------------------------------------------------------

void XmlWriter::etag() {
      if (stack.empty())
            Fatal("stack is empty");

      putLevel();
      *os << "</" << stack.top() << '>' << '\n';
      stack.pop();
      }

//---------------------------------------------------------
//   ntag
//    <mops> without newline
//---------------------------------------------------------

void XmlWriter::ntag(const char* name) {
      putLevel();
      *os << "<" << name << ">";
      }

//---------------------------------------------------------
//   netag
//    </mops>     without indentation
//---------------------------------------------------------

void XmlWriter::netag(const char* s) {
      *os << "</" << s << '>' << '\n';
      }

//---------------------------------------------------------
//   tag
//    <mops>value</mops>
//---------------------------------------------------------

void XmlWriter::tag(const char* name, const std::string& data) {
      putLevel();
      *os << std::format("<{}>{}</{}>\n", name, data, name);
      }

void XmlWriter::tag(const std::string& name, QVariant data) {
      putLevel();
      switch (data.typeId()) {
            case QMetaType::Bool:
            case QMetaType::Char:
            case QMetaType::Int:
            case QMetaType::UInt:
            case QMetaType::Double:
            case QMetaType::Float:
            case QMetaType::QString: *os << std::format("<{}>{}</{}>\n", name, data.toString(), name); break;
            case QMetaType::QStringList: {
                  QString s = xmlString(data.toStringList().join('\n'));
                  *os << std::format("<{}>{}</{}>\n", name, s, name);
                  } break;
            case QMetaType::QColor: {
                  QColor color(data.value<QColor>());
                  *os << std::format("<{} r=\"{}\" g=\"{}\" b=\"{}\" a=\"{}\"/>\n", name, color.red(), color.green(), color.blue(),
                                     color.alpha());
                  } break;
            case QMetaType::QRect: {
                  const QRect& r(data.value<QRect>());
                  *os << std::format("<{} x=\"{}\" y=\"{}\" w=\"{}\" h=\"{}\"/>\n", name, r.x(), r.y(), r.width(), r.height());
                  } break;
            case QMetaType::QRectF: {
                  const QRectF& r(data.value<QRectF>());
                  *os << std::format("<{} x=\"{}\" y=\"{}\" w=\"{}\" h=\"{}\"/>\n", name, r.x(), r.y(), r.width(), r.height());
                  } break;
            case QMetaType::QPointF: {
                  const QPointF& p(data.value<QPointF>());
                  *os << std::format("<{} x=\"{}\" y=\"{}\"/>\n", name, p.x(), p.y());
                  } break;
            case QMetaType::QVector3D: {
                  const QVector3D& p(data.value<QVector3D>());
                  *os << std::format("<{} x=\"{}\" y=\"{}\" z=\"{}\"/>\n", name, p.x(), p.y(), p.z());
                  } break;
            case QMetaType::QVector2D: {
                  const QVector2D& p(data.value<QVector2D>());
                  *os << std::format("<{} x=\"{}\" y=\"{}\"/>\n", name, p.x(), p.y());
                  } break;
            case QMetaType::QSizeF: {
                  const QSizeF& p(data.value<QSizeF>());
                  *os << std::format("<{} w=\"{}\" h=\"{}\"/>\n", name, p.width(), p.height());
                  } break;
            default: break;
            }
      }

void XmlWriter::tag(const char* name, const QVector3D& r) {
      putLevel();
      *os << std::format("<{} x=\"{}\" y=\"{}\" z=\"{}\"/>\n", name, r.x(), r.y(), r.z());
      }

void XmlWriter::tag(const char* name, const QVector2D& r) {
      putLevel();
      *os << std::format("<{} width=\"{}\" height=\"{}\"\"/>\n", name, r.x(), r.y());
      }

//---------------------------------------------------------
//   comment
//---------------------------------------------------------

void XmlWriter::comment(const std::string& text) {
      putLevel();
      *os << "<!-- " << text << " -->\n";
      }

//---------------------------------------------------------
//   xmlString
//---------------------------------------------------------

QString XmlWriter::xmlString(ushort c) {
      switch (c) {
            case '<': return QLatin1String("&lt;");
            case '>': return QLatin1String("&gt;");
            case '&': return QLatin1String("&amp;");
            case '\"': return QLatin1String("&quot;");
            default:
                  // ignore invalid characters in xml 1.0
                  if ((c < 0x20 && c != 0x09 && c != 0x0A && c != 0x0D))
                        return QString();
                  return QString(QChar(c));
            }
      }

//---------------------------------------------------------
//   xmlString
//---------------------------------------------------------

QString XmlWriter::xmlString(const QString& s) {
      QString escaped;
      escaped.reserve(s.size());
      for (int i = 0; i < s.size(); ++i) {
            ushort c  = s.at(i).unicode();
            escaped  += xmlString(c);
            }
      return escaped;
      }

//---------------------------------------------------------
//   writeXml
//    string s is already escaped (& -> "&amp;")
//---------------------------------------------------------

void XmlWriter::writeXml(const std::string& name, std::string s) {
      putLevel();
      for (auto& c : s)
            if (c < 0x20 && c != 0x09 && c != 0x0A && c != 0x0D)
                  c = '?';
      *os << "<" << name << ">";
      *os << s;
      *os << "</" << name << ">\n";
      }

SaveXmlWriter::SaveXmlWriter(fs::path p) {
      path = p;
      fs::path tempName{path.native() + ".tmp"};

      fos = new std::ofstream();
      fos->open(tempName, std::ios::out);
      os = fos;
      }

bool SaveXmlWriter::commit() {
      fos->close();
      delete fos;

      // rename path to path.bak
      fs::path bakName = path.native() + ".bak";
      std::error_code ec;
      fs::rename(path, bakName, ec);
      if (ec) {
            Critical("rename <{}> to <{}> failed: {}", path.native(), bakName.native(), ec.message());
            return false;
            }
      // rename tempName to path
      fs::path tempName{path.native() + ".tmp"};
      fs::rename(tempName, path, ec);
      if (ec.value()) {
            Critical("rename <{}> to <{}> failed: {}", tempName.native(), path.native(), ec.message());
            return false;
            }
      return true;
      }
