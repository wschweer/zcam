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

#include <stack>
#include <string>
#include <format>
#include <filesystem>
namespace fs = std::filesystem;

#include <QList>
#include <QString>
#include <QTextStream>
#include <QVariant>
#include <QVector3D>

//---------------------------------------------------------
//   XmlWriter
//---------------------------------------------------------

class XmlWriter : public std::ostream {
      std::stack<std::string> stack;
      void putLevel();

   protected:
      std::ostream* os;

    public:
      XmlWriter(std::ostream* _os) : os(_os) {}
      XmlWriter() { os = nullptr; }

      void header();

      void stag(const std::string&);
      void stag(const QString& s) { stag(s.toStdString()); }
      void stag(const char* s) { stag(std::string(s)); }

      template<class... Args>
         void stag(const std::string& name, std::format_string<Args...> fmt, Args&&... args) {
            putLevel();
            *os << std::format("<{} ", name);
            *os << std::format(fmt, std::forward<Args>(args)...);
            *os << std::format(">\n", name);
            stack.push(name);
            }

      void etag();

      template<class... Args>
         void tagE(const std::string& name, std::format_string<Args...> fmt, Args&&... args) {
            putLevel();
            *os << std::format("<{} ", name);
            *os << std::format(fmt, std::forward<Args>(args)...);
            *os << std::format("/>\n", name);
            }

      void ntag(const char* name);
      void netag(const char* name);

      void tag(const char* name, const std::string&);
      void tag(const std::string&, QVariant data);
      void tag(const char* name, const char* data) { tag(name, std::string(data)); }
      void tag(const char* name, const QString& s) { tag(name, s.toStdString()); }
      void tag(const char* name, const QVector3D&);
      void tag(const char* name, const QVector2D&);

      template<class... Args>
         void tag(const char* name, std::format_string<Args...> fmt, Args&&... args) {
            putLevel();
            *os << std::format("<{}>", name);
            *os << std::format(fmt, std::forward<Args>(args)...);
            *os << std::format("</{}>\n", name);
            }

      void comment(const std::string&);
      void writeXml(const std::string& name, std::string s);

      static QString xmlString(const QString&);
      static QString xmlString(ushort c);
      };

//---------------------------------------------------------
//   SaveXmlWriter
//---------------------------------------------------------

class SaveXmlWriter : public XmlWriter {
      fs::path path;
      std::ofstream* fos;

   public:
      SaveXmlWriter(fs::path p);
      bool commit();
      };
