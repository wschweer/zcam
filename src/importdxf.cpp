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

#include <QFile>
#include <QString>
#include <cassert>
#include <cmath>
#include <fstream>
#include <iostream>
#include <numbers>
#include <sstream>
#include <iosfwd>
#include <list>
#include <map>
#include <string>
#include <vector>

#include "cad.h"
#include "layer.h"
#include "polygon.h"
#include "circle.h"
#include "logger.h"
#include "projecttree.h"
#include "zcam.h"
#include "types.h"
#include "fixture.h"
#include "undo.h"
#include "laserlayer.h"

class QIODevice;
class Cad;

typedef int ColorIndex_t; // DXF color index

//---------------------------------------------------------
//   eDxfUnits_t
//---------------------------------------------------------

enum eDxfUnits_t {
      eUnspecified = 0, // Unspecified (No units)
      eInches,
      eFeet,
      eMiles,
      eMillimeters,
      eCentimeters,
      eMeters,
      eKilometers,
      eMicroinches,
      eMils,
      eYards,
      eAngstroms,
      eNanometers,
      eMicrons,
      eDecimeters,
      eDekameters,
      eHectometers,
      eGigameters,
      eAstronomicalUnits,
      eLightYears,
      eParsecs
      };

//---------------------------------------------------------
//   SplineData
//---------------------------------------------------------

struct SplineData {
      double norm[3];
      int degree;
      int knots;
      int control_points;
      int fit_points;
      int flag;
      std::vector<double> starttanx;
      std::vector<double> starttany;
      std::vector<double> starttanz;
      std::vector<double> endtanx;
      std::vector<double> endtany;
      std::vector<double> endtanz;
      std::vector<double> knot;
      std::vector<double> weight;
      std::vector<double> controlx;
      std::vector<double> controly;
      std::vector<double> controlz;
      std::vector<double> fitx;
      std::vector<double> fity;
      std::vector<double> fitz;
      };

typedef enum {
      RUnknown,
      ROlder,
      R10,
      R11_12,
      R13,
      R14,
      R2000,
      R2004,
      R2007,
      R2010,
      R2013,
      R2018,
      RNewer,
} eDXFVersion_t;

//---------------------------------------------------------
//   DxfReader
//---------------------------------------------------------

class DxfReader
      {
      Layer* layer;
      Wcam* zcam;
      std::ifstream* m_ifs;
      int lineNo;

      string m_str;
      string m_unused_line;
      eDxfUnits_t m_eUnits;
      bool m_measurement_inch;

      string m_layer_name;
      string m_section_name;
      string m_block_name;

      std::map<std::string, ColorIndex_t> m_layer_ColorIndex_map; // Mapping from layer name -> layer color index
      const ColorIndex_t ColorBylayer = 256;
      //      const ColorIndex_t ColorByBlock = 0;

      bool ReadUnits();
      bool ReadLayer();
      bool ReadLine();
      bool ReadText();
      bool ReadArc();
      bool ReadCircle();
      bool ReadEllipse();
      bool ReadPoint();
      bool ReadSpline();
      bool ReadLwPolyLine();
      bool ReadPolyLine();
      bool ReadVertex(double* pVertex, bool* bulge_found, double* bulge);
      void OnReadEllipse(const double* c, const double* m, double ratio, double start_angle, double end_angle);
      bool ReadInsert();
      bool ReadDimension();
      bool ReadBlockInfo();
      bool ReadVersion();
      bool ReadDWGCodePage();

      const string& get_line();
      void put_line(const string& value);
      void ResolveColorIndex();

      void readZero();

    protected:
      ColorIndex_t m_ColorIndex;
      eDXFVersion_t m_version; // Version from $ACADVER variable in DXF
      const char* (DxfReader::*stringToUTF8)(const char*) const;

    public:
      DxfReader(Wcam* wc);
      virtual ~DxfReader(); // this closes the file

      void DoRead(const char* filepath);
      double mm(double value) const;

      virtual void OnReadLine(const double* s, const double* e, bool hidden);
      virtual void OnReadCircle(const double* c, double radius, bool hidden);
      virtual void OnReadArc(double start_angle, double end_angle, double radius, const double* c, double z_extrusion_dir, bool hidden);
      virtual void OnReadPoint(const double* s) { Debug("n.i.: read point"); }
      virtual void OnReadText(const double* point, const double height, const char* text) { Debug("n.i: read text"); }
      virtual void OnReadEllipse(const double* c, double major_radius, double minor_radius, double rotation, double start_angle,
                                 double end_angle, bool dir) {
            Debug("n.i: read ellipse");
            }
      virtual void OnReadSpline(struct SplineData& sd);
      virtual void OnReadInsert(const double* point, const double* scale, const string& name, double rotation) {}
      virtual void OnReadDimension(const double* s, const double* e, const double* point, double rotation) {}
      virtual void AddGraphics() const {}
      std::string LayerName() const;

      bool read(const char* path, Layer* m);
      };

using namespace std;

//---------------------------------------------------------
//   DxfReader
//---------------------------------------------------------

DxfReader::DxfReader(Wcam* wc) {
      zcam               = wc;
      m_ColorIndex       = 0;
      m_eUnits           = eMillimeters;
      m_measurement_inch = false;
      m_layer_name       = "0"; // Default layer name
      m_version          = RUnknown;
      }

DxfReader::~DxfReader() {
      delete m_ifs;
      }

bool DxfReader::read(const char* path, Layer* m) {
      layer = m;
      DoRead(path);
      return true;
      }

//---------------------------------------------------------
//   mm
//---------------------------------------------------------

double DxfReader::mm(double value) const {
      // re #6461
      // this if handles situation of malformed DXF file where
      // MEASUREMENT specifies English units, but
      // INSUNITS specifies millimeters or is not specified
      //(millimeters is our default)
      if (m_measurement_inch && (m_eUnits == eMillimeters))
            value *= 25.4;

      switch (m_eUnits) {
            case eUnspecified: return (value * 1.0); // We don't know any better.
            case eInches: return (value * 25.4);
            case eFeet: return (value * 25.4 * 12);
            case eMiles: return (value * 1609344.0);
            case eMillimeters: return (value * 1.0);
            case eCentimeters: return (value * 10.0);
            case eMeters: return (value * 1000.0);
            case eKilometers: return (value * 1000000.0);
            case eMicroinches: return (value * 25.4 / 1000.0);
            case eMils: return (value * 25.4 / 1000.0);
            case eYards: return (value * 3 * 12 * 25.4);
            case eAngstroms: return (value * 0.0000001);
            case eNanometers: return (value * 0.000001);
            case eMicrons: return (value * 0.001);
            case eDecimeters: return (value * 100.0);
            case eDekameters: return (value * 10000.0);
            case eHectometers: return (value * 100000.0);
            case eGigameters: return (value * 1000000000000.0);
            case eAstronomicalUnits: return (value * 149597870690000.0);
            case eLightYears: return (value * 9454254955500000000.0);
            case eParsecs: return (value * 30856774879000000000.0);
            default: return (value * 1.0); // We don't know any better.
      } // End switch
} // End mm() method

//---------------------------------------------------------
//   ReadLine
//---------------------------------------------------------

bool DxfReader::ReadLine() {
      double s[3] = {0, 0, 0};
      double e[3] = {0, 0, 0};
      bool hidden = false;

      while (!m_ifs->eof()) {
            get_line();
            int n;

            if (sscanf(m_str.c_str(), "%d", &n) != 1) {
                  Debug("DxfReader::ReadLine() Failed to read integer from '{}'", m_str);
                  return false;
                  }

            std::istringstream ss;
            ss.imbue(std::locale("C"));
            switch (n) {
                  case 0:
                        // next item found, so finish with line
                        ResolveColorIndex();
                        OnReadLine(s, e, hidden);
                        hidden = false;
                        return true;

                  case 8: // Layer name follows
                        get_line();
                        m_layer_name = m_str;
                        break;

                  case 6: // line style name follows
                        get_line();
                        if (m_str[0] == 'h' || m_str[0] == 'H')
                              hidden = true;
                        break;

                  case 10:
                        // start x
                        get_line();
                        ss.str(m_str);
                        ss >> s[0];
                        s[0] = mm(s[0]);
                        if (ss.fail())
                              return false;
                        break;
                  case 20:
                        // start y
                        get_line();
                        ss.str(m_str);
                        ss >> s[1];
                        s[1] = mm(s[1]);
                        if (ss.fail())
                              return false;
                        break;
                  case 30:
                        // start z
                        get_line();
                        ss.str(m_str);
                        ss >> s[2];
                        s[2] = mm(s[2]);
                        if (ss.fail())
                              return false;
                        break;
                  case 11:
                        // end x
                        get_line();
                        ss.str(m_str);
                        ss >> e[0];
                        e[0] = mm(e[0]);
                        if (ss.fail())
                              return false;
                        break;
                  case 21:
                        // end y
                        get_line();
                        ss.str(m_str);
                        ss >> e[1];
                        e[1] = mm(e[1]);
                        if (ss.fail())
                              return false;
                        break;
                  case 31:
                        // end z
                        get_line();
                        ss.str(m_str);
                        ss >> e[2];
                        e[2] = mm(e[2]);
                        if (ss.fail())
                              return false;
                        break;
                  case 62:
                        // color index
                        get_line();
                        ss.str(m_str);
                        ss >> m_ColorIndex;
                        if (ss.fail())
                              return false;
                        break;

                  case 100:
                  case 39:
                  case 210:
                  case 220:
                  case 230:
                        // skip the next line
                        get_line();
                        break;
                  default:
                        // skip the next line
                        Debug("{}: unknown {}", lineNo, n);
                        get_line();
                        break;
                  }
            }

      ResolveColorIndex();
      OnReadLine(s, e, false);
      return false;
      }

//---------------------------------------------------------
//   ReadPoint
//---------------------------------------------------------

bool DxfReader::ReadPoint() {
      double s[3] = {0, 0, 0};

      while (!((*m_ifs).eof())) {
            get_line();
            int n;

            if (sscanf(m_str.c_str(), "%d", &n) != 1) {
                  Debug("DxfReader::ReadPoint() Failed to read integer from '{}'", m_str);
                  return false;
                  }

            std::istringstream ss;
            ss.imbue(std::locale("C"));
            switch (n) {
                  case 0:
                        // next item found, so finish with line
                        ResolveColorIndex();
                        OnReadPoint(s);
                        return true;

                  case 8: // Layer name follows
                        get_line();
                        m_layer_name = m_str;
                        break;

                  case 10:
                        // start x
                        get_line();
                        ss.str(m_str);
                        ss >> s[0];
                        s[0] = mm(s[0]);
                        if (ss.fail())
                              return false;
                        break;
                  case 20:
                        // start y
                        get_line();
                        ss.str(m_str);
                        ss >> s[1];
                        s[1] = mm(s[1]);
                        if (ss.fail())
                              return false;
                        break;
                  case 30:
                        // start z
                        get_line();
                        ss.str(m_str);
                        ss >> s[2];
                        s[2] = mm(s[2]);
                        if (ss.fail())
                              return false;
                        break;

                  case 62:
                        // color index
                        get_line();
                        ss.str(m_str);
                        ss >> m_ColorIndex;
                        if (ss.fail())
                              return false;
                        break;

                  case 100:
                  case 39:
                  case 210:
                  case 220:
                  case 230:
                        // skip the next line
                        get_line();
                        break;
                  default:
                        // skip the next line
                        get_line();
                        break;
                  }
            }

      ResolveColorIndex();
      OnReadPoint(s);
      return false;
      }

//---------------------------------------------------------
//   ReadArc
//---------------------------------------------------------

bool DxfReader::ReadArc() {
      double start_angle     = 0.0; // in degrees
      double end_angle       = 0.0;
      double radius          = 0.0;
      double c[3]            = {0, 0, 0}; // centre
      double z_extrusion_dir = 1.0;
      bool hidden            = false;

      while (!((*m_ifs).eof())) {
            get_line();
            int n;
            if (sscanf(m_str.c_str(), "%d", &n) != 1) {
                  Debug("DxfReader::ReadArc() Failed to read integer from '{}'", m_str);
                  return false;
                  }

            std::istringstream ss;
            ss.imbue(std::locale("C"));
            switch (n) {
                  case 0:
                        // next item found, so finish with arc
                        ResolveColorIndex();
                        OnReadArc(start_angle, end_angle, radius, c, z_extrusion_dir, hidden);
                        hidden = false;
                        return true;

                  case 8: // Layer name follows
                        get_line();
                        m_layer_name = m_str;
                        break;

                  case 6: // line style name follows
                        get_line();
                        if (m_str[0] == 'h' || m_str[0] == 'H')
                              hidden = true;
                        break;

                  case 10:
                        // centre x
                        get_line();
                        ss.str(m_str);
                        ss >> c[0];
                        c[0] = mm(c[0]);
                        if (ss.fail())
                              return false;
                        break;
                  case 20:
                        // centre y
                        get_line();
                        ss.str(m_str);
                        ss >> c[1];
                        c[1] = mm(c[1]);
                        if (ss.fail())
                              return false;
                        break;
                  case 30:
                        // centre z
                        get_line();
                        ss.str(m_str);
                        ss >> c[2];
                        c[2] = mm(c[2]);
                        if (ss.fail())
                              return false;
                        break;
                  case 40:
                        // radius
                        get_line();
                        ss.str(m_str);
                        ss >> radius;
                        radius = mm(radius);
                        if (ss.fail())
                              return false;
                        break;
                  case 50:
                        // start angle
                        get_line();
                        ss.str(m_str);
                        ss >> start_angle;
                        if (ss.fail())
                              return false;
                        break;
                  case 51:
                        // end angle
                        get_line();
                        ss.str(m_str);
                        ss >> end_angle;
                        if (ss.fail())
                              return false;
                        break;
                  case 62:
                        // color index
                        get_line();
                        ss.str(m_str);
                        ss >> m_ColorIndex;
                        if (ss.fail())
                              return false;
                        break;

                  case 100:
                  case 39:
                  case 210:
                  case 220:
                        // skip the next line
                        get_line();
                        break;
                  case 230:
                        // Z extrusion direction for arc
                        get_line();
                        ss.str(m_str);
                        ss >> z_extrusion_dir;
                        if (ss.fail())
                              return false;
                        break;

                  default:
                        // skip the next line
                        get_line();
                        break;
                  }
            }
      ResolveColorIndex();
      OnReadArc(start_angle, end_angle, radius, c, z_extrusion_dir, false);
      return false;
      }

//---------------------------------------------------------
//   ReadSpline
//---------------------------------------------------------

bool DxfReader::ReadSpline() {
      SplineData sd;
      sd.norm[0]        = 0;
      sd.norm[1]        = 0;
      sd.norm[2]        = 1;
      sd.degree         = 0;
      sd.knots          = 0;
      sd.flag           = 0;
      sd.control_points = 0;
      sd.fit_points     = 0;

      double temp_double;

      while (!((*m_ifs).eof())) {
            get_line();
            int n;
            if (sscanf(m_str.c_str(), "%d", &n) != 1) {
                  Debug("DxfReader::ReadSpline() Failed to read integer from '{}'", m_str);
                  return false;
                  }
            std::istringstream ss;
            ss.imbue(std::locale("C"));
            switch (n) {
                  case 0:
                        // next item found, so finish with Spline
                        ResolveColorIndex();
                        OnReadSpline(sd);
                        return true;
                  case 8: // Layer name follows
                        get_line();
                        m_layer_name = m_str;
                        break;
                  case 62:
                        // color index
                        get_line();
                        ss.str(m_str);
                        ss >> m_ColorIndex;
                        if (ss.fail())
                              return false;
                        break;
                  case 210:
                        // normal x
                        get_line();
                        ss.str(m_str);
                        ss >> sd.norm[0];
                        if (ss.fail())
                              return false;
                        break;
                  case 220:
                        // normal y
                        get_line();
                        ss.str(m_str);
                        ss >> sd.norm[1];
                        if (ss.fail())
                              return false;
                        break;
                  case 230:
                        // normal z
                        get_line();
                        ss.str(m_str);
                        ss >> sd.norm[2];
                        if (ss.fail())
                              return false;
                        break;
                  case 70:
                        // flag
                        get_line();
                        ss.str(m_str);
                        ss >> sd.flag;
                        if (ss.fail())
                              return false;
                        break;
                  case 71:
                        // degree
                        get_line();
                        ss.str(m_str);
                        ss >> sd.degree;
                        if (ss.fail())
                              return false;
                        break;
                  case 72:
                        // knots
                        get_line();
                        ss.str(m_str);
                        ss >> sd.knots;
                        if (ss.fail())
                              return false;
                        break;
                  case 73:
                        // control points
                        get_line();
                        ss.str(m_str);
                        ss >> sd.control_points;
                        if (ss.fail())
                              return false;
                        break;
                  case 74:
                        // fit points
                        get_line();
                        ss.str(m_str);
                        ss >> sd.fit_points;
                        if (ss.fail())
                              return false;
                        break;
                  case 12:
                        // starttan x
                        get_line();
                        ss.str(m_str);
                        ss >> temp_double;
                        temp_double = mm(temp_double);
                        if (ss.fail())
                              return false;
                        sd.starttanx.push_back(temp_double);
                        break;
                  case 22:
                        // starttan y
                        get_line();
                        ss.str(m_str);
                        ss >> temp_double;
                        temp_double = mm(temp_double);
                        if (ss.fail())
                              return false;
                        sd.starttany.push_back(temp_double);
                        break;
                  case 32:
                        // starttan z
                        get_line();
                        ss.str(m_str);
                        ss >> temp_double;
                        temp_double = mm(temp_double);
                        if (ss.fail())
                              return false;
                        sd.starttanz.push_back(temp_double);
                        break;
                  case 13:
                        // endtan x
                        get_line();
                        ss.str(m_str);
                        ss >> temp_double;
                        temp_double = mm(temp_double);
                        if (ss.fail())
                              return false;
                        sd.endtanx.push_back(temp_double);
                        break;
                  case 23:
                        // endtan y
                        get_line();
                        ss.str(m_str);
                        ss >> temp_double;
                        temp_double = mm(temp_double);
                        if (ss.fail())
                              return false;
                        sd.endtany.push_back(temp_double);
                        break;
                  case 33:
                        // endtan z
                        get_line();
                        ss.str(m_str);
                        ss >> temp_double;
                        temp_double = mm(temp_double);
                        if (ss.fail())
                              return false;
                        sd.endtanz.push_back(temp_double);
                        break;
                  case 40:
                        // knot
                        get_line();
                        ss.str(m_str);
                        ss >> temp_double;
                        temp_double = mm(temp_double);
                        if (ss.fail())
                              return false;
                        sd.knot.push_back(temp_double);
                        break;
                  case 41:
                        // weight
                        get_line();
                        ss.str(m_str);
                        ss >> temp_double;
                        temp_double = mm(temp_double);
                        if (ss.fail())
                              return false;
                        sd.weight.push_back(temp_double);
                        break;
                  case 10:
                        // control x
                        get_line();
                        ss.str(m_str);
                        ss >> temp_double;
                        temp_double = mm(temp_double);
                        if (ss.fail())
                              return false;
                        sd.controlx.push_back(temp_double);
                        break;
                  case 20:
                        // control y
                        get_line();
                        ss.str(m_str);
                        ss >> temp_double;
                        temp_double = mm(temp_double);
                        if (ss.fail())
                              return false;
                        sd.controly.push_back(temp_double);
                        break;
                  case 30:
                        // control z
                        get_line();
                        ss.str(m_str);
                        ss >> temp_double;
                        temp_double = mm(temp_double);
                        if (ss.fail())
                              return false;
                        sd.controlz.push_back(temp_double);
                        break;
                  case 11:
                        // fit x
                        get_line();
                        ss.str(m_str);
                        ss >> temp_double;
                        temp_double = mm(temp_double);
                        if (ss.fail())
                              return false;
                        sd.fitx.push_back(temp_double);
                        break;
                  case 21:
                        // fit y
                        get_line();
                        ss.str(m_str);
                        ss >> temp_double;
                        temp_double = mm(temp_double);
                        if (ss.fail())
                              return false;
                        sd.fity.push_back(temp_double);
                        break;
                  case 31:
                        // fit z
                        get_line();
                        ss.str(m_str);
                        ss >> temp_double;
                        temp_double = mm(temp_double);
                        if (ss.fail())
                              return false;
                        sd.fitz.push_back(temp_double);
                        break;
                  case 42:
                  case 43:
                  case 44:
                        // skip the next line
                        get_line();
                        break;
                  default:
                        // skip the next line
                        get_line();
                        break;
                  }
            }
      ResolveColorIndex();
      OnReadSpline(sd);
      return false;
      }

//---------------------------------------------------------
//   ReadCircle
//---------------------------------------------------------

bool DxfReader::ReadCircle() {
      double radius = 0.0;
      double c[3]   = {0, 0, 0}; // centre
      bool hidden   = false;

      while (!((*m_ifs).eof())) {
            get_line();
            int n;
            if (sscanf(m_str.c_str(), "%d", &n) != 1) {
                  Debug("DxfReader::ReadCircle() Failed to read integer from '{}'", m_str);
                  return false;
                  }
            std::istringstream ss;
            ss.imbue(std::locale("C"));
            switch (n) {
                  case 0:
                        // next item found, so finish with Circle
                        ResolveColorIndex();
                        OnReadCircle(c, radius, hidden);
                        hidden = false;
                        return true;

                  case 6: // line style name follows
                        get_line();
                        if (m_str[0] == 'h' || m_str[0] == 'H')
                              hidden = true;
                        break;

                  case 8: // Layer name follows
                        get_line();
                        m_layer_name = m_str;
                        break;

                  case 10:
                        // centre x
                        get_line();
                        ss.str(m_str);
                        ss >> c[0];
                        c[0] = mm(c[0]);
                        if (ss.fail())
                              return false;
                        break;
                  case 20:
                        // centre y
                        get_line();
                        ss.str(m_str);
                        ss >> c[1];
                        c[1] = mm(c[1]);
                        if (ss.fail())
                              return false;
                        break;
                  case 30:
                        // centre z
                        get_line();
                        ss.str(m_str);
                        ss >> c[2];
                        c[2] = mm(c[2]);
                        if (ss.fail())
                              return false;
                        break;
                  case 40:
                        // radius
                        get_line();
                        ss.str(m_str);
                        ss >> radius;
                        radius = mm(radius);
                        if (ss.fail())
                              return false;
                        break;
                  case 62:
                        // color index
                        get_line();
                        ss.str(m_str);
                        ss >> m_ColorIndex;
                        if (ss.fail())
                              return false;
                        break;

                  case 100:
                  case 39:
                  case 210:
                  case 220:
                  case 230:
                        // skip the next line
                        get_line();
                        break;
                  default:
                        // skip the next line
                        get_line();
                        break;
                  }
            }
      ResolveColorIndex();
      OnReadCircle(c, radius, false);
      return false;
      }

//---------------------------------------------------------
//   ReadText
//---------------------------------------------------------

bool DxfReader::ReadText() {
      double c[3]; // coordinate
      double height = 0.03082;
      std::string textPrefix;

      memset(c, 0, sizeof(c));

      while (!((*m_ifs).eof())) {
            get_line();
            int n;
            if (sscanf(m_str.c_str(), "%d", &n) != 1) {
                  Debug("DxfReader::ReadText() Failed to read integer from '{}'", m_str);
                  return false;
                  }
            std::istringstream ss;
            ss.imbue(std::locale("C"));
            switch (n) {
                  case 0: return false;
                  case 8: // Layer name follows
                        get_line();
                        m_layer_name = m_str;
                        break;

                  case 10:
                        // centre x
                        get_line();
                        ss.str(m_str);
                        ss >> c[0];
                        c[0] = mm(c[0]);
                        if (ss.fail())
                              return false;
                        break;
                  case 20:
                        // centre y
                        get_line();
                        ss.str(m_str);
                        ss >> c[1];
                        c[1] = mm(c[1]);
                        if (ss.fail())
                              return false;
                        break;
                  case 30:
                        // centre z
                        get_line();
                        ss.str(m_str);
                        ss >> c[2];
                        c[2] = mm(c[2]);
                        if (ss.fail())
                              return false;
                        break;
                  case 40:
                        // text height
                        get_line();
                        ss.str(m_str);
                        ss >> height;
                        height = mm(height);
                        if (ss.fail())
                              return false;
                        break;
                  case 3:
                        // Additional text that goes before the type 1 text
                        // Note that if breaking the text into type-3 records splits a UFT-8 encoding we do the decoding
                        // after splicing the lines together. I'm not sure if this actually occurs, but handling the text
                        // this way will treat this condition properly.
                        get_line();
                        textPrefix.append(m_str);
                        break;
                  case 1:
                        // final text
                        // Note that we treat this as the end of the TEXT or MTEXT entity but this may cause us to miss
                        // other properties. Officially the entity ends at the start of the next entity, the BLKEND record
                        // that ends the containing BLOCK, or the ENDSEC record that ends the ENTITIES section. These are
                        // all code 0 records. Changing this would require either some sort of peek/pushback ability or the understanding
                        // that ReadText() and all the other Read... methods return having already read a code 0.
                        get_line();
                        textPrefix.append(m_str);
                        ResolveColorIndex();
                              {
                              const char* utfStr = (this->*stringToUTF8)(textPrefix.c_str());
                              OnReadText(c, height * 25.4 / 72.0, utfStr);
                              delete utfStr;
                              }
                        return (true);

                  case 62:
                        // color index
                        get_line();
                        ss.str(m_str);
                        ss >> m_ColorIndex;
                        if (ss.fail())
                              return false;
                        break;

                  case 100:
                  case 39:
                  case 210:
                  case 220:
                  case 230:
                        // skip the next line
                        get_line();
                        break;
                  default:
                        // skip the next line
                        get_line();
                        break;
                  }
            }

      return false;
      }

//---------------------------------------------------------
//   ReadEllipse
//---------------------------------------------------------

bool DxfReader::ReadEllipse() {
      double c[3]  = {0, 0, 0}; // centre
      double m[3]  = {0, 0, 0}; // major axis point
      double ratio = 0;         // ratio of major to minor axis
      double start = 0;         // start of arc
      double end   = 0;         // end of arc

      while (!((*m_ifs).eof())) {
            get_line();
            int n;
            if (sscanf(m_str.c_str(), "%d", &n) != 1) {
                  Debug("DxfReader::ReadEllipse() Failed to read integer from '{}'", m_str);
                  return false;
                  }
            std::istringstream ss;
            ss.imbue(std::locale("C"));
            switch (n) {
                  case 0:
                        // next item found, so finish with Ellipse
                        ResolveColorIndex();
                        OnReadEllipse(c, m, ratio, start, end);
                        return true;
                  case 8: // Layer name follows
                        get_line();
                        m_layer_name = m_str;
                        break;

                  case 10:
                        // centre x
                        get_line();
                        ss.str(m_str);
                        ss >> c[0];
                        c[0] = mm(c[0]);
                        if (ss.fail())
                              return false;
                        break;
                  case 20:
                        // centre y
                        get_line();
                        ss.str(m_str);
                        ss >> c[1];
                        c[1] = mm(c[1]);
                        if (ss.fail())
                              return false;
                        break;
                  case 30:
                        // centre z
                        get_line();
                        ss.str(m_str);
                        ss >> c[2];
                        c[2] = mm(c[2]);
                        if (ss.fail())
                              return false;
                        break;
                  case 11:
                        // major x
                        get_line();
                        ss.str(m_str);
                        ss >> m[0];
                        m[0] = mm(m[0]);
                        if (ss.fail())
                              return false;
                        break;
                  case 21:
                        // major y
                        get_line();
                        ss.str(m_str);
                        ss >> m[1];
                        m[1] = mm(m[1]);
                        if (ss.fail())
                              return false;
                        break;
                  case 31:
                        // major z
                        get_line();
                        ss.str(m_str);
                        ss >> m[2];
                        m[2] = mm(m[2]);
                        if (ss.fail())
                              return false;
                        break;
                  case 40:
                        // ratio
                        get_line();
                        ss.str(m_str);
                        ss >> ratio;
                        if (ss.fail())
                              return false;
                        break;
                  case 41:
                        // start
                        get_line();
                        ss.str(m_str);
                        ss >> start;
                        if (ss.fail())
                              return false;
                        break;
                  case 42:
                        // end
                        get_line();
                        ss.str(m_str);
                        ss >> end;
                        if (ss.fail())
                              return false;
                        break;
                  case 62:
                        // color index
                        get_line();
                        ss.str(m_str);
                        ss >> m_ColorIndex;
                        if (ss.fail())
                              return false;
                        break;
                  case 100:
                  case 210:
                  case 220:
                  case 230:
                        // skip the next line
                        get_line();
                        break;
                  default:
                        // skip the next line
                        get_line();
                        break;
                  }
            }
      ResolveColorIndex();
      OnReadEllipse(c, m, ratio, start, end);
      return false;
      }

static bool poly_prev_found = false;
static double poly_prev_x;
static double poly_prev_y;
static double poly_prev_z;
static bool poly_prev_bulge_found = false;
static double poly_prev_bulge;
static bool poly_first_found = false;
static double poly_first_x;
static double poly_first_y;
static double poly_first_z;

//---------------------------------------------------------
//   AddPolyLinePoint
//---------------------------------------------------------

static void AddPolyLinePoint(DxfReader* dxf_read, double x, double y, double z, bool bulge_found, double bulge) {
      if (poly_prev_found) {
            bool arc_done = false;
            if (poly_prev_bulge_found) {
                  Debug("Bulge not implemented");
#if 0
                        double cot = 0.5 * ((1.0 / poly_prev_bulge) - poly_prev_bulge);
                        double cx = ((poly_prev_x + x) - ((y - poly_prev_y) * cot)) / 2.0;
                        double cy = ((poly_prev_y + y) + ((x - poly_prev_x) * cot)) / 2.0;
                        double ps[3] = {poly_prev_x, poly_prev_y, poly_prev_z};
                        double pe[3] = {x, y, z};
                        double pc[3] = {cx, cy, (poly_prev_z + z) / 2.0};
                        dxf_read->OnReadArc(ps, pe, pc, poly_prev_bulge >= 0, false);
#endif
                  arc_done = true;
                  }

            if (!arc_done) {
                  double s[3] = {poly_prev_x, poly_prev_y, poly_prev_z};
                  double e[3] = {x, y, z};
                  dxf_read->OnReadLine(s, e, false);
                  }
            }

      poly_prev_found = true;
      poly_prev_x     = x;
      poly_prev_y     = y;
      poly_prev_z     = z;
      if (!poly_first_found) {
            poly_first_x     = x;
            poly_first_y     = y;
            poly_first_z     = z;
            poly_first_found = true;
            }
      poly_prev_bulge_found = bulge_found;
      poly_prev_bulge       = bulge;
      }

static void PolyLineStart() {
      poly_prev_found  = false;
      poly_first_found = false;
      }

//---------------------------------------------------------
//   ReadLwPolyLine
//---------------------------------------------------------

bool DxfReader::ReadLwPolyLine() {
      PolyLineStart();

      Line* line = createElement<Line>(zcam);
      zcam->undo()->push(new InsertElement(layer, line, 0, zcam));

      bool x_found = false;
      bool y_found = false;
      double x     = 0.0;
      double y     = 0.0;
      double z     = 0.0;
      //      bool bulge_found = false;
      double bulge = 0.0;
      //      bool closed = false;
      int flags;
      bool next_item_found = false;

      Debug("import PolyLine");
      while (!m_ifs->eof() && !next_item_found) {
            get_line();
            size_t nn;
            int n = stoi(m_str, &nn);
            if (nn == 0) {
                  Debug("DxfReader::ReadLwPolyLine() Failed to read integer from '{}'", m_str);
                  return false;
                  }
            std::istringstream ss;
            ss.imbue(std::locale("C"));
            switch (n) {
                  case 0:
                        // next item found
                        ResolveColorIndex();
                        if (x_found && y_found)
                              line->lineTo({x, y});
                        Debug("=====END");
                        return true;
                  case 8: // Layer name follows
                        get_line();
                        m_layer_name = m_str;
                        break;
                  case 10:
                        // x
                        get_line();
                        if (x_found && y_found) {
                              // add point
                              line->lineTo({x, y});
                              // bulge_found = false;
                              x_found = false;
                              y_found = false;
                              }
                        ss.str(m_str);
                        ss >> x;
                        x = mm(x);
                        if (ss.fail())
                              return false;
                        x_found = true;
                        break;
                  case 20:
                        // y
                        get_line();
                        ss.str(m_str);
                        ss >> y;
                        y = mm(y);
                        if (ss.fail())
                              return false;
                        y_found = true;
                        break;
                  case 38:
                        // elevation
                        get_line();
                        ss.str(m_str);
                        ss >> z;
                        z = mm(z);
                        if (ss.fail())
                              return false;
                        break;
                  case 42:
                        // bulge
                        get_line();
                        ss.str(m_str);
                        ss >> bulge;
                        if (ss.fail())
                              return false;
                        // bulge_found = true;
                        break;
                  case 70:
                        // flags
                        get_line();
                        if (sscanf(m_str.c_str(), "%d", &flags) != 1)
                              return false;
                        //                        closed = flags & 1;
                        //TODO                        line->setClosePath(closed);
                        break;
                  case 62:
                        // color index
                        get_line();
                        ss.str(m_str);
                        ss >> m_ColorIndex;
                        if (ss.fail())
                              return false;
                        break;
                  default:
                        // skip the next line
                        get_line();
                        break;
                  }
            }
      Debug(">>");
      return false;
      }

//---------------------------------------------------------
//   ReadVertex
//---------------------------------------------------------

bool DxfReader::ReadVertex(double* pVertex, bool* bulge_found, double* bulge) {
      bool x_found = false;
      bool y_found = false;

      double x     = 0.0;
      double y     = 0.0;
      double z     = 0.0;
      *bulge       = 0.0;
      *bulge_found = false;

      pVertex[0] = 0.0;
      pVertex[1] = 0.0;
      pVertex[2] = 0.0;

      while (!(*m_ifs).eof()) {
            get_line();
            int n;
            if (sscanf(m_str.c_str(), "%d", &n) != 1) {
                  Debug("DxfReader::ReadVertex() Failed to read integer from '{}'", m_str);
                  return false;
                  }
            std::istringstream ss;
            ss.imbue(std::locale("C"));
            switch (n) {
                  case 0:
                        ResolveColorIndex();
                        put_line(m_str); // read one line too many.  put it back.
                        return (x_found && y_found);
                        break;

                  case 8: // Layer name follows
                        get_line();
                        m_layer_name = m_str;
                        break;

                  case 10:
                        // x
                        get_line();
                        ss.str(m_str);
                        ss >> x;
                        pVertex[0] = mm(x);
                        if (ss.fail())
                              return false;
                        x_found = true;
                        break;
                  case 20:
                        // y
                        get_line();
                        ss.str(m_str);
                        ss >> y;
                        pVertex[1] = mm(y);
                        if (ss.fail())
                              return false;
                        y_found = true;
                        break;
                  case 30:
                        // z
                        get_line();
                        ss.str(m_str);
                        ss >> z;
                        pVertex[2] = mm(z);
                        if (ss.fail())
                              return false;
                        break;

                  case 42:
                        get_line();
                        *bulge_found = true;
                        ss.str(m_str);
                        ss >> *bulge;
                        if (ss.fail())
                              return false;
                        break;
                  case 62:
                        // color index
                        get_line();
                        ss.str(m_str);
                        ss >> m_ColorIndex;
                        if (ss.fail())
                              return false;
                        break;

                  default:
                        // skip the next line
                        get_line();
                        break;
                  }
            }

      return false;
      }

//---------------------------------------------------------
//   ReadPolyLine
//---------------------------------------------------------

bool DxfReader::ReadPolyLine() {
      PolyLineStart();

      bool closed = false;
      int flags;
      bool first_vertex_section_found = false;
      double first_vertex[3]          = {0, 0, 0};
      bool bulge_found;
      double bulge;

      while (!(*m_ifs).eof()) {
            get_line();
            int n;
            if (sscanf(m_str.c_str(), "%d", &n) != 1) {
                  Debug("DxfReader::ReadPolyLine() Failed to read integer from '{}'", m_str);
                  return false;
                  }
            std::istringstream ss;
            ss.imbue(std::locale("C"));
            switch (n) {
                  case 0:
                        // next item found
                        ResolveColorIndex();
                        get_line();
                        if (m_str == "VERTEX") {
                              double vertex[3] = {0, 0, 0};
                              if (DxfReader::ReadVertex(vertex, &bulge_found, &bulge)) {
                                    if (!first_vertex_section_found) {
                                          first_vertex_section_found = true;
                                          memcpy(first_vertex, vertex, 3 * sizeof(double));
                                          }
                                    Debug("   vertex");
                                    AddPolyLinePoint(this, vertex[0], vertex[1], vertex[2], bulge_found, bulge);
                                    break;
                                    }
                              }
                        if (m_str == "SEQEND") {
                              if (closed && first_vertex_section_found) {
                                    Debug("   vertex end");
                                    AddPolyLinePoint(this, first_vertex[0], first_vertex[1], first_vertex[2], 0, 0);
                                    }
                              first_vertex_section_found = false;
                              PolyLineStart();
                              return (true);
                              }
                        break;
                  case 70:
                        // flags
                        get_line();
                        if (sscanf(m_str.c_str(), "%d", &flags) != 1)
                              return false;
                        closed = ((flags & 1) != 0);
                        break;
                  case 62:
                        // color index
                        get_line();
                        ss.str(m_str);
                        ss >> m_ColorIndex;
                        if (ss.fail())
                              return false;
                        break;
                  default:
                        // skip the next line
                        get_line();
                        break;
                  }
            }

      return false;
      }

//---------------------------------------------------------
//   OnReadEllipse
//---------------------------------------------------------

void DxfReader::OnReadEllipse(const double* c, const double* m, double ratio, double start_angle, double end_angle) {
      double major_radius = sqrt(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
      double minor_radius = major_radius * ratio;

      // Since we only support 2d stuff, we can calculate the rotation from the major axis x and y value only,
      // since z is zero, major_radius is the vector length

      double rotation = atan2(m[1] / major_radius, m[0] / major_radius);

      OnReadEllipse(c, major_radius, minor_radius, rotation, start_angle, end_angle, true);
      }

bool DxfReader::ReadInsert() {
      double c[3] = {0, 0, 0}; // coordinate
      double s[3] = {1, 1, 1}; // scale
      double rot  = 0.0;       // rotation
      string name;

      while (!((*m_ifs).eof())) {
            get_line();
            int n;
            if (sscanf(m_str.c_str(), "%d", &n) != 1) {
                  Debug("DxfReader::ReadInsert() Failed to read integer from '{}'", m_str);
                  return false;
                  }
            std::istringstream ss;
            ss.imbue(std::locale("C"));
            switch (n) {
                  case 0:
                        // next item found
                        ResolveColorIndex();
                        OnReadInsert(c, s, name, rot * M_PI / 180);
                        return (true);
                  case 8:
                        // Layer name follows
                        get_line();
                        m_layer_name = m_str;
                        break;
                  case 10:
                        // coord x
                        get_line();
                        ss.str(m_str);
                        ss >> c[0];
                        c[0] = mm(c[0]);
                        if (ss.fail())
                              return false;
                        break;
                  case 20:
                        // coord y
                        get_line();
                        ss.str(m_str);
                        ss >> c[1];
                        c[1] = mm(c[1]);
                        if (ss.fail())
                              return false;
                        break;
                  case 30:
                        // coord z
                        get_line();
                        ss.str(m_str);
                        ss >> c[2];
                        c[2] = mm(c[2]);
                        if (ss.fail())
                              return false;
                        break;
                  case 41:
                        // scale x
                        get_line();
                        ss.str(m_str);
                        ss >> s[0];
                        if (ss.fail())
                              return false;
                        break;
                  case 42:
                        // scale y
                        get_line();
                        ss.str(m_str);
                        ss >> s[1];
                        if (ss.fail())
                              return false;
                        break;
                  case 43:
                        // scale z
                        get_line();
                        ss.str(m_str);
                        ss >> s[2];
                        if (ss.fail())
                              return false;
                        break;
                  case 50:
                        // rotation
                        get_line();
                        ss.str(m_str);
                        ss >> rot;
                        if (ss.fail())
                              return false;
                        break;
                  case 2:
                        // block name
                        get_line();
                        name = m_str;
                        break;
                  case 62:
                        // color index
                        get_line();
                        ss.str(m_str);
                        ss >> m_ColorIndex;
                        if (ss.fail())
                              return false;
                        break;
                  case 100:
                  case 39:
                  case 210:
                  case 220:
                  case 230:
                        // skip the next line
                        get_line();
                        break;
                  default:
                        // skip the next line
                        get_line();
                        break;
                  }
            }
      return false;
      }

//---------------------------------------------------------
//   ReadDimension
//---------------------------------------------------------

bool DxfReader::ReadDimension() {
      double s[3] = {0, 0, 0}; // startpoint
      double e[3] = {0, 0, 0}; // endpoint
      double p[3] = {0, 0, 0}; // dimpoint
      double rot  = -1.0;      // rotation

      while (!((*m_ifs).eof())) {
            get_line();
            int n;
            if (sscanf(m_str.c_str(), "%d", &n) != 1) {
                  Debug("DxfReader::ReadInsert() Failed to read integer from '{}'", m_str);
                  return false;
                  }
            std::istringstream ss;
            ss.imbue(std::locale("C"));
            switch (n) {
                  case 0:
                        // next item found
                        ResolveColorIndex();
                        OnReadDimension(s, e, p, rot * M_PI / 180);
                        return (true);
                  case 8:
                        // Layer name follows
                        get_line();
                        m_layer_name = m_str;
                        break;
                  case 13:
                        // start x
                        get_line();
                        ss.str(m_str);
                        ss >> s[0];
                        s[0] = mm(s[0]);
                        if (ss.fail())
                              return false;
                        break;
                  case 23:
                        // start y
                        get_line();
                        ss.str(m_str);
                        ss >> s[1];
                        s[1] = mm(s[1]);
                        if (ss.fail())
                              return false;
                        break;
                  case 33:
                        // start z
                        get_line();
                        ss.str(m_str);
                        ss >> s[2];
                        s[2] = mm(s[2]);
                        if (ss.fail())
                              return false;
                        break;
                  case 14:
                        // end x
                        get_line();
                        ss.str(m_str);
                        ss >> e[0];
                        e[0] = mm(e[0]);
                        if (ss.fail())
                              return false;
                        break;
                  case 24:
                        // end y
                        get_line();
                        ss.str(m_str);
                        ss >> e[1];
                        e[1] = mm(e[1]);
                        if (ss.fail())
                              return false;
                        break;
                  case 34:
                        // end z
                        get_line();
                        ss.str(m_str);
                        ss >> e[2];
                        e[2] = mm(e[2]);
                        if (ss.fail())
                              return false;
                        break;
                  case 10:
                        // dimline x
                        get_line();
                        ss.str(m_str);
                        ss >> p[0];
                        p[0] = mm(p[0]);
                        if (ss.fail())
                              return false;
                        break;
                  case 20:
                        // dimline y
                        get_line();
                        ss.str(m_str);
                        ss >> p[1];
                        p[1] = mm(p[1]);
                        if (ss.fail())
                              return false;
                        break;
                  case 30:
                        // dimline z
                        get_line();
                        ss.str(m_str);
                        ss >> p[2];
                        p[2] = mm(p[2]);
                        if (ss.fail())
                              return false;
                        break;
                  case 50:
                        // rotation
                        get_line();
                        ss.str(m_str);
                        ss >> rot;
                        if (ss.fail())
                              return false;
                        break;
                  case 62:
                        // color index
                        get_line();
                        ss.str(m_str);
                        ss >> m_ColorIndex;
                        if (ss.fail())
                              return false;
                        break;
                  case 100:
                  case 39:
                  case 210:
                  case 220:
                  case 230:
                        // skip the next line
                        get_line();
                        break;
                  default:
                        // skip the next line
                        get_line();
                        break;
                  }
            }
      return false;
      }

//---------------------------------------------------------
//   ReadBlockInfo
//---------------------------------------------------------

bool DxfReader::ReadBlockInfo() {
      while (!((*m_ifs).eof())) {
            get_line();
            int n;
            if (sscanf(m_str.c_str(), "%d", &n) != 1) {
                  Debug("DxfReader::ReadBlockInfo() Failed to read integer from '{}'", m_str);
                  return false;
                  }
            std::istringstream ss;
            ss.imbue(std::locale("C"));
            switch (n) {
                  case 2:
                        // block name
                        get_line();
                        m_block_name = m_str;
                        return true;
                  case 3:
                        // block name too???
                        get_line();
                        m_block_name = m_str;
                        return true;
                  default:
                        // skip the next line
                        get_line();
                        break;
                  }
            }
      return false;
      }

//---------------------------------------------------------
//   get_line
//---------------------------------------------------------

const string& DxfReader::get_line() {
      if (!m_unused_line.empty()) {
            m_str = m_unused_line;
            m_unused_line.clear();
            }
      else {
            std::getline(*m_ifs, m_str);
            ++lineNo;
            m_str.erase(0, m_str.find_first_not_of("\t "));
            m_str.erase(m_str.find_last_not_of("\r\n") + 1);
            }
      return m_str;
      }

//---------------------------------------------------------
//   put_line
//---------------------------------------------------------

void DxfReader::put_line(const string& value) {
      m_unused_line = value;
      }

//---------------------------------------------------------
//   ReadUnits
//---------------------------------------------------------

bool DxfReader::ReadUnits() {
      get_line(); // Skip to next line.
      get_line(); // Skip to next line.
      int n = 0;
      if (sscanf(m_str.c_str(), "%d", &n) == 1) {
            m_eUnits = eDxfUnits_t(n);
            return (true);
            }
      else {
            Debug("DxfReader::ReadUnits() Failed to get integer from '{}'", m_str);
            return (false);
            }
      }

//---------------------------------------------------------
//   ReadLayer
//---------------------------------------------------------

bool DxfReader::ReadLayer() {
      std::string layername;
      ColorIndex_t colorIndex = -1;

      while (!((*m_ifs).eof())) {
            get_line();
            int n;

            if (sscanf(m_str.c_str(), "%d", &n) != 1) {
                  Debug("DxfReader::ReadLayer() Failed to read integer from '{}'", m_str);
                  return false;
                  }

            std::istringstream ss;
            ss.imbue(std::locale("C"));
            switch (n) {
                  case 0: // next item found, so finish with line
                        if (layername.empty()) {
                              Debug("DxfReader::ReadLayer() - no layer name");
                              return false;
                              }
                        m_layer_ColorIndex_map[layername] = colorIndex;
                        return true;

                  case 2: // Layer name follows
                        get_line();
                        layername = m_str;
                        break;

                  case 62:
                        // layer color ; if negative, layer is off
                        get_line();
                        if (sscanf(m_str.c_str(), "%d", &colorIndex) != 1)
                              return false;
                        break;

                  case 6:  // linetype name
                  case 70: // layer flags
                  case 100:
                  case 290:
                  case 370:
                  case 390:
                        // skip the next line
                        get_line();
                        break;
                  default:
                        // skip the next line
                        get_line();
                        break;
                  }
            }
      return false;
      }

//---------------------------------------------------------
//   ReadVersion
//---------------------------------------------------------

bool DxfReader::ReadVersion() {
      // This table is indexed by eDXFVersion_t - (ROlder+1)
      static const std::vector<std::string> VersionNames = {"AC1006", "AC1009", "AC1012", "AC1014", "AC1015",
                                                            "AC1018", "AC1021", "AC1024", "AC1027", "AC1032"};

      assert(VersionNames.size() == RNewer - ROlder - 1);
      get_line();
      get_line();
      std::vector<std::string>::const_iterator first = VersionNames.cbegin();
      std::vector<std::string>::const_iterator last  = VersionNames.cend();
      std::vector<std::string>::const_iterator found = std::lower_bound(first, last, m_str);
      if (found == last)
            m_version = RNewer;
      else if (*found == m_str)
            m_version = (eDXFVersion_t)(std::distance(first, found) + (ROlder + 1));
      else if (found == first)
            m_version = ROlder;
      else
            m_version = RUnknown;

      return true;
      }

//---------------------------------------------------------
//   ReadDWGCodePage
//---------------------------------------------------------

bool DxfReader::ReadDWGCodePage() {
      get_line();
      get_line();
      //m_CodePage = m_str;
      return true;
      }

//---------------------------------------------------------
//   ResolveColorIndex
//---------------------------------------------------------

void DxfReader::ResolveColorIndex() {
      if (m_ColorIndex == ColorBylayer) // if color = layer color, replace by color from layer
            m_ColorIndex = m_layer_ColorIndex_map[std::string(m_layer_name)];
      }

//---------------------------------------------------------
//   LayerName
//---------------------------------------------------------

std::string DxfReader::LayerName() const {
      std::string result;

      if (m_section_name.size() > 0) {
            result.append(m_section_name);
            result.append(" ");
            }

      if (m_block_name.size() > 0) {
            result.append(m_block_name);
            result.append(" ");
            }

      if (m_layer_name.size() > 0)
            result.append(m_layer_name);

      return result;
      }

//---------------------------------------------------------
//   OnReadSpline
//---------------------------------------------------------

void DxfReader::OnReadSpline(SplineData& sd) {
      Debug("spline  knots {} control points {} fit points {}", sd.knots, sd.control_points, sd.fit_points);

      Assert(sd.control_points == sd.controlx.size());
      Assert(sd.control_points == sd.controly.size());
      Assert(sd.control_points == sd.controlz.size());

      Line* line = createElement<Line>(zcam);
      zcam->undo()->push(new InsertElement(layer, line, 0, zcam));

      bool first = true;
      for (size_t i = 0; i < sd.controlx.size(); ++i) {
            if (first)
                  line->moveTo(Vec3d(sd.controlx[i], sd.controly[i], sd.controlz[i]));
            else
                  line->lineTo(Vec3d(sd.controlx[i], sd.controly[i], sd.controlz[i]));
            first = false;
            }
      line->setLineWidth(.02);
      //      line->setStroke(true);
      line->setFill(true);
      //      line->setClosePath(false);
      line->update();
      }

//---------------------------------------------------------
//   OnReadLine
//---------------------------------------------------------

void DxfReader::OnReadLine(const double* s, const double* e, bool /* hidden */) {
      Line* line = createElement<Line>(zcam);
      zcam->undo()->push(new InsertElement(layer, line, 0, zcam));

      line->moveTo({s[0], s[1]});
      line->lineTo({e[0], e[1]});
      line->setLineWidth(.02);
      //      line->setStroke(true);
      line->setFill(true);
      //      line->setClosePath(false);

      Debug("import Line");
      // model->addLine({s[0], s[1], s[2]}, {e[0], e[1], e[2]});
      Debug("import line {} {} -- {} {}", s[0], s[1], e[0], e[1]);
      line->update();
      }

//---------------------------------------------------------
//   OnReadCircle
//---------------------------------------------------------

void DxfReader::OnReadCircle(const double* c, double radius, bool /*hidden*/) {
      // TODO      model->addElement(new Circle({c[0], c[1], c[2]}, radius));
      Debug("import circle {} {} {} {}", c[0], c[1], c[2], radius);
      }

//---------------------------------------------------------
//   OnReadArc
//---------------------------------------------------------

void DxfReader::OnReadArc(double startAngle, double endAngle, double r, const double* c, double z_extrusion_dir, bool hidden) {
      //
      // convert arc to PolyLine
      //
      ::Path points;
      constexpr double precision = 0.01;
      int s                      = (std::numbers::pi / acos(1.0 - (precision / r))) / 4.0 + 0.5;
      if (s < 2) // minimum is 2 segments per quadrant
            s = 2;
      double step = (0.5 * std::numbers::pi) / double(s);
      startAngle  = (startAngle * numbers::pi) / 180.0;
      endAngle    = (endAngle * numbers::pi) / 180.0;

      if (startAngle > endAngle) {
            endAngle = 2.0 * M_PI + endAngle;

            for (double angle = startAngle; angle < endAngle; angle += step) {
                  double x = cos(angle) * r;
                  double y = sin(angle) * r;
                  points.push_back(Vec3d(x + c[0], y + c[1], 0.0));
                  }
            points.push_back(Vec3d(cos(endAngle) * r + c[0], sin(endAngle) * r + c[1], 0.0));
            }
      else {
            for (double angle = startAngle; angle < endAngle; angle += step) {
                  double x = cos(angle) * r;
                  double y = sin(angle) * r;
                  points.push_back(Vec3d(x + c[0], y + c[1], 0.0));
                  }
            points.push_back(Vec3d(cos(endAngle) * r + c[0], sin(endAngle) * r + c[1], 0.0));
            }

      Line* line = createElement<Line>(zcam);
      zcam->undo()->push(new InsertElement(layer, line, 0, zcam));

      bool first = true;
      for (const auto& a : points) {
            if (first)
                  line->moveTo(a);
            else
                  line->lineTo(a);
            first = false;
            }
      line->setLineWidth(.02);
      //      line->setStroke(true);
      line->setFill(true);
      //      line->setClosePath(false);
      line->update();
      Debug("import Arc");
      }

//---------------------------------------------------------
//   readZero
//---------------------------------------------------------

void DxfReader::readZero() {
      get_line();
      if (m_str == "SECTION") {
            m_section_name = "";
            get_line();
            get_line();
            if (m_str != "ENTITIES")
                  m_section_name = m_str;
            m_block_name = "";
            get_line();
            }
      else if (m_str == "TABLE") {
            get_line();
            get_line();
            get_line();
            }
      else if (m_str == "LAYER") {
            get_line();
            get_line();
            if (!ReadLayer()) {
                  Debug("DxfReader::DoRead() Failed to read layer");
                  // return; Some objects or tables can have "LAYER" as name...
                  }
            }
      else if (m_str == "BLOCK") {
            if (!ReadBlockInfo())
                  Debug("DxfReader::DoRead() Failed to read block info");
            }
      else if (m_str == "ENDSEC") {
            m_section_name = "";
            m_block_name   = "";
            get_line();
            }
      else if (m_str == "LINE") {
            if (!ReadLine())
                  Debug("DxfReader::DoRead() Failed to read line");
            }
      else if (m_str == "ARC") {
            if (!ReadArc())
                  Debug("DxfReader::DoRead() Failed to read arc");
            }
      else if (m_str == "CIRCLE") {
            if (!ReadCircle())
                  Debug("DxfReader::DoRead() Failed to read circle");
            }
      else if (m_str == "MTEXT") {
            if (!ReadText())
                  Debug("DxfReader::DoRead() Failed to read text");
            }
      else if (m_str == "TEXT") {
            if (!ReadText())
                  Debug("DxfReader::DoRead() Failed to read text");
            }
      else if (m_str == "ELLIPSE") {
            if (!ReadEllipse())
                  Debug("DxfReader::DoRead() Failed to read ellipse");
            }
      else if (m_str == "SPLINE") {
            if (!ReadSpline())
                  Debug("DxfReader::DoRead() Failed to read spline");
            }
      else if (m_str == "LWPOLYLINE") {
            if (!ReadLwPolyLine())
                  Debug("DxfReader::DoRead() Failed to read LW Polyline");
            }
      else if (m_str == "POLYLINE") {
            if (!ReadPolyLine())
                  Debug("DxfReader::DoRead() Failed to read Polyline");
            }
      else if (m_str == "POINT") {
            if (!ReadPoint())
                  Debug("DxfReader::DoRead() Failed to read Point");
            }
      else if (m_str == "INSERT") {
            if (!ReadInsert())
                  Debug("DxfReader::DoRead() Failed to read Insert");
            }
      else if (m_str == "DIMENSION") {
            if (!ReadDimension())
                  Debug("DxfReader::DoRead() Failed to read Dimension");
            }
      }

//---------------------------------------------------------
//   DoRead
//---------------------------------------------------------

void DxfReader::DoRead(const char* filepath) {
      Debug("import dxf: {}", filepath);

      lineNo = 0;
      m_ifs  = new ifstream(filepath);
      if (!(*m_ifs)) {
            Debug("DXF file didn't load");
            return;
            }
      m_ifs->imbue(std::locale("C"));

      get_line();
      while (!m_ifs->eof()) {
            if (m_str == "$INSUNITS") {
                  if (!ReadUnits())
                        return;
                  }
            else if (m_str == "$MEASUREMENT") {
                  get_line();
                  get_line();
                  int n = 1;
                  if (sscanf(m_str.c_str(), "%d", &n) == 1) {
                        if (n == 0)
                              m_measurement_inch = true;
                        }
                  }
            else if (m_str == "$ACADVER") {
                  if (!ReadVersion())
                        return;
                  }
            else if (m_str == "$DWGCODEPAGE") {
                  if (!ReadDWGCodePage())
                        return;
                  }
            else if (m_str == "0")
                  readZero();
            else
                  get_line();
            }
      // AddGraphics();
      }

//---------------------------------------------------------
//   importDXF
//---------------------------------------------------------

bool Wcam::importDXF(const QString& path) {
      Debug("{}", path);
      QFileInfo fi(path);

      zcam->startCmd();
      Cad* cad     = topLevel()->cad();
      Layer* layer = createElement<Layer>(this, format("{}", fi.baseName()));
      layer->setExpanded(true);
      zcam->undo()->push(new InsertElement(cad, layer, 0, this));

      auto fixture = topLevel()->fixture();
      if (!fixture) {
            if (topLevel()->fixtures().empty())
                  Fatal("no fixtures");
            fixture = topLevel()->fixtures().at(0);
            }

      LaserLayer* ll = createElement<LaserLayer>(this, format("LL-{}", fi.baseName()));
      ll->setExpanded(false);
      ll->setBaseElement(layer);
      ll->setKerfOffset(-0.05);
      undo()->push(new InsertElement(fixture, ll, 0, this));

      //      _projectTreeModel->beginLoad();
      DxfReader dxf(this);
      bool rv = dxf.read(path.toLocal8Bit().data(), layer);
      if (!rv)
            Critical("dxf import failed");
      //      _projectTreeModel->endLoad();
      zcam->endCmd();
      return rv;
      }
