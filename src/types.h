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

#pragma once

#include <QPointF>
#include <QVector3D>
#include "clipper2/clipper.h"

static constexpr double PT_FUZZ = 0.01;

class Vec3d;

//---------------------------------------------------------
//   Vec2d
//---------------------------------------------------------

class Vec2d
      {
    public:
      double _x{0.0}, _y{0.0};
      double x() const { return _x; }
      double y() const { return _y; }
      double& x() { return _x; }
      double& y() { return _y; }
      double length() const { return sqrt(_x * _x + _y * _y); }
      double manhattanLength() { return std::abs(_x) + std::abs(_y); }
      operator QPointF() const { return QPointF(_x, _y); }
      Vec2d() {}
      Vec2d(const Vec3d& v);
      Vec2d(double x, double y) : _x(x), _y(y) {}
      Vec2d(const QPointF& p) : _x(p.x()), _y(p.y()) {}
      Vec2d operator-(const Vec2d& p) const { return {p._x - _x, p._y - _y}; }
      Vec2d operator+(const Vec2d& p) const { return {p._x + _x, p._y + _y}; }
      Vec2d operator+(const Vec3d& p) const;
      Vec2d& operator+=(const Vec2d& p) {
            _x += p._x, _y += p._y;
            return *this;
            }
      Vec2d operator=(const QPointF& p) const { return Vec2d(p.x(), p.y()); }
      Vec2d operator-(const Vec3d& p) const;
      operator Vec3d() const;
      bool operator==(const Vec2d& v) const { return std::abs(v.x() - x()) < 0.01 && std::abs(v.y() - y()) < 0.01; }
      bool operator!=(const Vec2d& v) const { return std::abs(v.x() - x()) > 0.01 || std::abs(v.y() - y()) > 0.01; }
      Vec2d operator*(double d) const { return {_x * d, _y * d}; }
      double dist(const Vec2d& v) { return Vec2d(v.x() - x(), v.y() - y()).length(); }
      };

//---------------------------------------------------------
//   Vec3d
//---------------------------------------------------------

class Vec3d
      {
    public:
      double _x{0.0}, _y{0.0}, _z{0.0};
      double x() const { return _x; }
      double y() const { return _y; }
      double z() const { return _z; }
      double& x() { return _x; }
      double& y() { return _y; }
      double& z() { return _z; }
      bool isNaN() const { return false; } // TODO
      void normalize() {}                  // TODO
      int length() { return sqrt(_x * _x + _y * _y + _z * _z); }
      operator QPointF() const { return QPointF(_x, _y); }
      operator QVector3D() const { return QVector3D(_x, _y, _z); }
      Vec3d() {}
      Vec3d(double x, double y, double z) : _x(x), _y(y), _z(z) {}
      Vec3d(const Vec2d& p) : _x(p.x()), _y(p.y()), _z(0.0) {}
      Vec3d(const QPointF& p) : _x(p.x()), _y(p.y()), _z(0.0) {}
      Vec3d(const QVector3D& p) : _x(p.x()), _y(p.y()), _z(p.z()) {}
      void setX(double v) { _x = v; }
      void setY(double v) { _y = v; }
      void setZ(double v) { _z = v; }
      bool operator<(const Vec3d& p) const { return p._x < _x || p._y < _y || p._z < _z; }
      bool operator==(const Vec3d& p) const { return p._x == _x && p._y == _y && p._z == _z; }
      Vec3d operator+(const Vec3d& p) const { return {p._x + _x, p._y + _y, p._z + _z}; }
      Vec3d operator-(const Vec3d& p) const { return {p._x - _x, p._y - _y, p._z - _z}; }
      Vec3d& operator+=(const Vec3d& p) {
            _x += p._x, _y += p._y, _z += p._z;
            return *this;
            }
      Vec3d& operator*=(double v) {
            _x *= v, _y *= v, _z *= v;
            return *this;
            }
      Vec3d operator*(double v) const { return Vec3d(_x * v, _y * v, _z * v); }
      bool fuzzyCompare(const Vec3d& p) const {
            return std::abs(p._x - _x) < PT_FUZZ && std::abs(p._y - _y) < PT_FUZZ && std::abs(p._z - _z) < PT_FUZZ;
            }
      double manhattanDistance(const Vec3d& p) const { return std::abs(_x - p._x) + std::abs(_y - p._y) + std::abs(_z - p._z); }
      bool isApprox(const Vec3d& v) {
            static const double fuzz = 0.0001;
            return (qAbs(v._x - _x) < fuzz) && (qAbs(v._y - _y) < fuzz) && (qAbs(v._z - _z) < fuzz);
            }
      static Vec3d Zero() { return Vec3d(); }
      //      void write(XmlWriter&, const char* name) const;
      //      void read(XmlReader& r);
      };

inline Vec2d::Vec2d(const Vec3d& v) : _x(v.x()), _y(v.y()) {
      }

inline Vec2d::operator Vec3d() const {
      return Vec3d(_x, _y, 0.0);
      }

inline Vec2d Vec2d::operator+(const Vec3d& p) const {
      return {p._x + _x, p._y + _y};
      }

inline Vec2d Vec2d::operator-(const Vec3d& p) const {
      return {_x - p._x, _y - p._y};
      }

//---------------------------------------------------------
//   Path2d
//    a collection of points building up a path
//    - path can be open or closed
//    - a closed path has equal start- and endpoints
//    - a path can have the attribute "fill"
//---------------------------------------------------------

class Path2d : public std::vector<Vec2d>
      {

    public:
      Path2d() {}
      Path2d(const Clipper2Lib::PathD& path) {
            for (const auto& p : path)
                  push_back(Vec2d(p.x, p.y));
            }
      };

//---------------------------------------------------------
//   PathList
//---------------------------------------------------------

class PathList : public std::vector<Path2d>
      {
      bool _fill{false};

    public:
      void push_back(const Clipper2Lib::PathD& pg) { std::vector<Path2d>::push_back(Path2d(pg)); }
      void push_back(const Path2d& p) { std::vector<Path2d>::push_back(p); }
      Clipper2Lib::PathsD clipper() const {
            Clipper2Lib::PathsD p;
            for (const auto& l : *this) {
                  Clipper2Lib::PathD cp;
                  for (const auto& pt : l)
                        cp.push_back({pt.x(), pt.y()});
                  p.push_back(cp);
                  }
            return p;
            }
      PathList(const Clipper2Lib::PathsD& pl) {
            for (const auto& p : pl) {
                  Path2d v;
                  for (const auto& pt : p)
                        v.push_back(Vec2d(pt.x, pt.y));
                  push_back(p);
                  }
            }
      PathList() {}
      bool fill() const { return _fill; }
      void setFill(bool v) { _fill = v; }
      void clear() {
            _fill = false;
            std::vector<Path2d>::clear();
            }
      };
