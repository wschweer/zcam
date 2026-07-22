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

#include "mediabrowser.h"
#include "logger.h"
#include "libdxfrw.h"
#include "drw_interface.h"
#include "drw_base.h"
#include "drw_entities.h"
#include "drw_header.h"

#include <QFontDatabase>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QSettings>
#include <QRectF>
#include <numbers>
#include <cmath>
#include <limits>
#include <algorithm>
#include <sstream>
#include <iomanip>

//---------------------------------------------------------
//   FontModel
//---------------------------------------------------------

FontModel::FontModel(QObject* parent) : QAbstractListModel(parent) {
      _allFamilies = QFontDatabase::families();
      loadFavorites();
      rebuildList();
      }

//---------------------------------------------------------
//   rebuildList
//    Rebuild the visible list based on _showFavorites.
//---------------------------------------------------------

void FontModel::rebuildList() {
      beginResetModel();
      if (_showFavorites)
            _visibleFamilies = _favorites;
      else
            _visibleFamilies = _allFamilies;
      endResetModel();
      }

//---------------------------------------------------------
//   rowCount
//---------------------------------------------------------

int FontModel::rowCount(const QModelIndex& parent) const {
      if (parent.isValid())
            return 0;
      return _visibleFamilies.size();
      }

//---------------------------------------------------------
//   data
//---------------------------------------------------------

QVariant FontModel::data(const QModelIndex& index, int role) const {
      if (!index.isValid() || index.row() >= _visibleFamilies.size())
            return {};
      const QString& family = _visibleFamilies[index.row()];
      switch (role) {
            case FamilyRole: return family;
            case IsFavoriteRole: return _favorites.contains(family);
            default: return {};
            }
      }

//---------------------------------------------------------
//   roleNames
//---------------------------------------------------------

QHash<int, QByteArray> FontModel::roleNames() const {
      QHash<int, QByteArray> roles;
      roles[FamilyRole]     = "family";
      roles[IsFavoriteRole] = "isFavorite";
      return roles;
      }

//---------------------------------------------------------
//   setShowFavorites
//---------------------------------------------------------

void FontModel::setShowFavorites(bool v) {
      if (v == _showFavorites)
            return;
      _showFavorites = v;
      rebuildList();
      emit showFavoritesChanged();
      }

//---------------------------------------------------------
//   setCurrentFamily
//---------------------------------------------------------

void FontModel::setCurrentFamily(const QString& v) {
      if (v == _currentFamily)
            return;
      _currentFamily = v;
      emit currentFamilyChanged();
      }

//---------------------------------------------------------
//   setCurrentStyle
//---------------------------------------------------------

void FontModel::setCurrentStyle(const QString& v) {
      if (v == _currentStyle)
            return;
      _currentStyle = v;
      emit currentStyleChanged();
      }

//---------------------------------------------------------
//   stylesForFamily
//    Returns the list of style names for a given font family.
//---------------------------------------------------------

QStringList FontModel::stylesForFamily(const QString& family) const {
      return QFontDatabase::styles(family);
      }

//---------------------------------------------------------
//   weightsForFamily
//    Returns a list of weight names that have at least one
//    style in the given family.
//---------------------------------------------------------

QStringList FontModel::weightsForFamily(const QString& family) const {
      QStringList weights;
      static const QStringList weightNames = {"Thin",     "ExtraLight", "Light",     "Normal", "Medium",
                                              "DemiBold", "Bold",       "ExtraBold", "Black"};
      static const QList<int> weightValues = {QFont::Thin,   QFont::ExtraLight, QFont::Light,
                                              QFont::Normal, QFont::Medium,     QFont::DemiBold,
                                              QFont::Bold,   QFont::ExtraBold,  QFont::Black};

      QStringList styles = QFontDatabase::styles(family);
      QSet<int> availableWeights;
      for (const QString& style : styles)
            availableWeights.insert(QFontDatabase::weight(family, style));

      for (int i = 0; i < weightValues.size(); ++i)
            if (availableWeights.contains(weightValues[i]))
                  weights.append(weightNames[i]);
      return weights;
      }

//---------------------------------------------------------
//   familyAt
//---------------------------------------------------------

QString FontModel::familyAt(int row) const {
      if (row < 0 || row >= _visibleFamilies.size())
            return {};
      return _visibleFamilies[row];
      }

//---------------------------------------------------------
//   addFavorite
//---------------------------------------------------------

void FontModel::addFavorite(const QString& family) {
      if (_favorites.contains(family))
            return;
      _favorites.append(family);
      saveFavorites();
      emit favoritesChanged();
      // Update the favorite flag in the visible list
      if (_showFavorites)
            rebuildList();
      else {
            int row = _visibleFamilies.indexOf(family);
            if (row >= 0) {
                  QModelIndex idx = index(row);
                  emit dataChanged(idx, idx, {IsFavoriteRole});
                  }
            }
      }

//---------------------------------------------------------
//   removeFavorite
//---------------------------------------------------------

void FontModel::removeFavorite(const QString& family) {
      if (!_favorites.contains(family))
            return;
      _favorites.removeAll(family);
      saveFavorites();
      emit favoritesChanged();
      if (_showFavorites)
            rebuildList();
      else {
            int row = _visibleFamilies.indexOf(family);
            if (row >= 0) {
                  QModelIndex idx = index(row);
                  emit dataChanged(idx, idx, {IsFavoriteRole});
                  }
            }
      }

//---------------------------------------------------------
//   isFavorite
//---------------------------------------------------------

bool FontModel::isFavorite(const QString& family) const {
      return _favorites.contains(family);
      }

//---------------------------------------------------------
//   allFamilies
//---------------------------------------------------------

QStringList FontModel::allFamilies() const {
      return _allFamilies;
      }

//---------------------------------------------------------
//   favoriteFamilies
//---------------------------------------------------------

QStringList FontModel::favoriteFamilies() const {
      return _favorites;
      }

//---------------------------------------------------------
//   loadFavorites
//---------------------------------------------------------

void FontModel::loadFavorites() {
      QSettings settings;
      _favorites = settings.value("MediaBrowser/fontFavorites").toStringList();
      }

//---------------------------------------------------------
//   saveFavorites
//---------------------------------------------------------

void FontModel::saveFavorites() {
      QSettings settings;
      settings.setValue("MediaBrowser/fontFavorites", _favorites);
      }

//---------------------------------------------------------
//   ArtworkTreeModel
//---------------------------------------------------------

ArtworkTreeModel::ArtworkTreeModel(QObject* parent) : QAbstractItemModel(parent) {
      }

ArtworkTreeModel::~ArtworkTreeModel() = default;

//---------------------------------------------------------
//   setRootPath
//---------------------------------------------------------

void ArtworkTreeModel::setRootPath(const QString& v) {
      // Expand a leading '~' to the user's home directory.
      // The config stores paths like "~/ZCam/artwork" but QDir does not
      // understand '~', so without expansion the directory appears
      // non-existent and the configured path is silently ignored.
      QString expanded = v;
      if (expanded.startsWith('~'))
            expanded = QDir::homePath() + expanded.mid(1);
      if (expanded == _rootPath)
            return;
      _rootPath = expanded;
      emit rootPathChanged();
      rebuildTree();
      }

//---------------------------------------------------------
//   rebuildTree
//---------------------------------------------------------

void ArtworkTreeModel::rebuildTree() {
      beginResetModel();
      _root       = std::make_unique<ArtworkNode>();
      _root->name = "Root";
      _root->path = _rootPath;
      if (!_rootPath.isEmpty() && QDir(_rootPath).exists()) {
            loadChildren(_root.get());
            // Only keep children that have images (recursively)
            _root->children.erase(
                std::remove_if(_root->children.begin(), _root->children.end(),
                               [](const std::unique_ptr<ArtworkNode>& c) { return !c->hasImages; }),
                _root->children.end());
            for (auto& c : _root->children) {
                  c->parent = _root.get();
                  // Mark children as not-yet-loaded so that canFetchMore()
                  // returns true and TreeView shows the expand arrow.
                  c->loaded = false;
                  }
            }
      endResetModel();
      }

//---------------------------------------------------------
//   directoryHasImages
//    Check if a directory contains image files directly.
//---------------------------------------------------------

bool ArtworkTreeModel::directoryHasImages(const QString& dirPath) const {
      QDir dir(dirPath);
      if (!dir.exists())
            return false;
      QStringList filters;
      for (const QString& ext : _imageExtensions)
            filters.append("*." + ext);
      dir.setFilter(QDir::Files);
      dir.setNameFilters(filters);
      return !dir.entryList().isEmpty();
      }

//---------------------------------------------------------
//   directoryHasImagesRecursive
//    Check if a directory or any descendant contains image files.
//---------------------------------------------------------

bool ArtworkTreeModel::directoryHasImagesRecursive(const QString& dirPath) const {
      if (directoryHasImages(dirPath))
            return true;
      QDir dir(dirPath);
      if (!dir.exists())
            return false;
      dir.setFilter(QDir::Dirs | QDir::NoDotAndDotDot);
      for (const QString& sub : dir.entryList())
            if (directoryHasImagesRecursive(dir.filePath(sub)))
                  return true;
      return false;
      }

//---------------------------------------------------------
//   loadChildren
//    Populate node->children with immediate subdirectories.
//    Sets hasImages recursively for each child.
//---------------------------------------------------------

void ArtworkTreeModel::loadChildren(ArtworkNode* node) {
      if (node->loaded)
            return;
      node->loaded = true;
      QDir dir(node->path);
      if (!dir.exists())
            return;
      dir.setFilter(QDir::Dirs | QDir::NoDotAndDotDot);
      for (const QString& sub : dir.entryList()) {
            auto child       = std::make_unique<ArtworkNode>();
            child->name      = sub;
            child->path      = dir.filePath(sub);
            child->parent    = node;
            child->hasImages = directoryHasImagesRecursive(child->path);
            // Determine whether this directory has subdirectories.
            QDir subdir(child->path);
            subdir.setFilter(QDir::Dirs | QDir::NoDotAndDotDot);
            child->hasChildren = !subdir.entryList().isEmpty();
            node->children.push_back(std::move(child));
            }
      // Remove children without images
      node->children.erase(
          std::remove_if(node->children.begin(), node->children.end(),
                         [](const std::unique_ptr<ArtworkNode>& c) { return !c->hasImages; }),
          node->children.end());
      node->hasChildren = !node->children.empty();
      // Mark all children as not-yet-loaded so they can be lazily expanded.
      for (auto& c : node->children)
            c->loaded = false;
      }

//---------------------------------------------------------
//   rowCount
//---------------------------------------------------------

int ArtworkTreeModel::rowCount(const QModelIndex& parent) const {
      if (!parent.isValid())
            return _root ? static_cast<int>(_root->children.size()) : 0;
      ArtworkNode* node = static_cast<ArtworkNode*>(parent.internalPointer());
      if (!node)
            return 0;
      return static_cast<int>(node->children.size());
      }

//---------------------------------------------------------
//   columnCount
//---------------------------------------------------------

int ArtworkTreeModel::columnCount(const QModelIndex& /*parent*/) const {
      return 1;
      }

//---------------------------------------------------------
//   index
//---------------------------------------------------------

QModelIndex ArtworkTreeModel::index(int row, int column, const QModelIndex& parent) const {
      if (!hasIndex(row, column, parent))
            return {};
      ArtworkNode* parentNode = nullptr;
      if (!parent.isValid())
            parentNode = _root.get();
      else
            parentNode = static_cast<ArtworkNode*>(parent.internalPointer());
      if (!parentNode || row >= static_cast<int>(parentNode->children.size()))
            return {};
      return createIndex(row, column, parentNode->children[row].get());
      }

//---------------------------------------------------------
//   parent
//---------------------------------------------------------

QModelIndex ArtworkTreeModel::parent(const QModelIndex& child) const {
      if (!child.isValid())
            return {};
      ArtworkNode* node = static_cast<ArtworkNode*>(child.internalPointer());
      if (!node || !node->parent)
            return {};
      ArtworkNode* grandparent = node->parent->parent;
      if (!grandparent)
            return {};
      // Find the parent's row in grandparent's children
      for (int i = 0; i < static_cast<int>(grandparent->children.size()); ++i)
            if (grandparent->children[i].get() == node->parent)
                  return createIndex(i, 0, node->parent);
      return {};
      }

//---------------------------------------------------------
//   data
//---------------------------------------------------------

QVariant ArtworkTreeModel::data(const QModelIndex& index, int role) const {
      if (!index.isValid())
            return {};
      ArtworkNode* node = static_cast<ArtworkNode*>(index.internalPointer());
      if (!node)
            return {};
      switch (role) {
            case NameRole: return node->name;
            case PathRole: return node->path;
            case HasImagesRole: return node->hasImages;
            case HasChildrenRole:
                  return node->hasChildren || !node->loaded; // not-yet-loaded nodes may have children
            default: return {};
            }
      }

//---------------------------------------------------------
//   roleNames
//---------------------------------------------------------

QHash<int, QByteArray> ArtworkTreeModel::roleNames() const {
      QHash<int, QByteArray> roles;
      roles[NameRole]        = "dirName";
      roles[PathRole]        = "dirPath";
      roles[HasImagesRole]   = "hasImages";
      roles[HasChildrenRole] = "hasChildrenRole";
      return roles;
      }

//---------------------------------------------------------
//   hasChildren
//    Returns true if the node might have children.
//    For not-yet-loaded nodes, returns true so TreeView shows
//    the expand arrow. Once loaded, returns whether the node
//    actually has children.
//---------------------------------------------------------

bool ArtworkTreeModel::hasChildren(const QModelIndex& parent) const {
      if (!parent.isValid())
            return _root && !_root->children.empty();
      ArtworkNode* node = static_cast<ArtworkNode*>(parent.internalPointer());
      if (!node)
            return false;
      // If not yet loaded, assume it has children (lazy load).
      // Once loaded, return whether it actually has children.
      if (!node->loaded)
            return true;
      return !node->children.empty();
      }

//---------------------------------------------------------
//   canFetchMore
//---------------------------------------------------------

bool ArtworkTreeModel::canFetchMore(const QModelIndex& parent) const {
      if (!parent.isValid())
            return false;
      ArtworkNode* node = static_cast<ArtworkNode*>(parent.internalPointer());
      return node && !node->loaded;
      }

//---------------------------------------------------------
//   fetchMore
//---------------------------------------------------------

void ArtworkTreeModel::fetchMore(const QModelIndex& parent) {
      if (!parent.isValid())
            return;
      ArtworkNode* node = static_cast<ArtworkNode*>(parent.internalPointer());
      if (!node || node->loaded)
            return;
      int existing = static_cast<int>(node->children.size());
      loadChildren(node);
      int added = static_cast<int>(node->children.size()) - existing;
      if (added > 0) {
            beginInsertRows(parent, existing, existing + added - 1);
            endInsertRows();
            }
      }

//---------------------------------------------------------
//   imageFiles
//    Return image files in a directory as QVariantList of
//    {fileName, filePath, fileType} objects.
//---------------------------------------------------------

QVariantList ArtworkTreeModel::imageFiles(const QString& dirPath) const {
      QVariantList result;
      QDir dir(dirPath);
      if (!dir.exists())
            return result;
      for (const QString& ext : _imageExtensions) {
            dir.setNameFilters({"*." + ext});
            dir.setFilter(QDir::Files);
            for (const QString& file : dir.entryList(QDir::Files, QDir::Name)) {
                  QFileInfo fi(dir.filePath(file));
                  QVariantMap entry;
                  entry["fileName"] = fi.fileName();
                  entry["filePath"] = fi.absoluteFilePath();
                  entry["fileType"] = ext.toLower();
                  result.append(entry);
                  }
            }
      // Sort by fileName
      std::sort(result.begin(), result.end(), [](const QVariant& a, const QVariant& b) {
            return a.toMap()["fileName"].toString() < b.toMap()["fileName"].toString();
            });
      return result;
      }

//---------------------------------------------------------
//   hasImages
//---------------------------------------------------------

bool ArtworkTreeModel::hasImages(const QString& dirPath) const {
      return directoryHasImages(dirPath);
      }

//---------------------------------------------------------
//   refresh
//---------------------------------------------------------

void ArtworkTreeModel::refresh() {
      rebuildTree();
      }

//---------------------------------------------------------
//   findIndexForPath
//    Walk the tree from the root to the node whose path matches
//    dirPath, lazily loading each ancestor along the way.
//    Returns the QModelIndex of the matching node, or an invalid
//    index if the path is not found in the tree.
//---------------------------------------------------------

QModelIndex ArtworkTreeModel::findIndexForPath(const QString& dirPath) const {
      if (!_root || dirPath.isEmpty())
            return {};
      // Build the list of path components from rootPath to dirPath.
      // e.g. rootPath="/usr/share/icons", dirPath="/usr/share/icons/Adwaita/16x16/actions"
      //       → components = ["Adwaita", "16x16", "actions"]
      QString relative = QDir(_rootPath).relativeFilePath(dirPath);
      if (relative.isEmpty())
            return {}; // dirPath is the root itself — not a child node
      if (relative.startsWith(".."))
            return {}; // dirPath is outside the root
      QStringList components = relative.split('/', Qt::SkipEmptyParts);
      if (components.isEmpty())
            return {};

      // Walk the tree, loading children as needed.
      // We use const_cast because loadChildren / fetchMore modify the model.
      ArtworkNode* current = _root.get();
      QModelIndex currentIdx;
      for (int depth = 0; depth < components.size(); ++depth) {
            if (!current->loaded) {
                  // Lazily load children of this node.
                  const_cast<ArtworkTreeModel*>(this)->fetchMore(currentIdx);
                  }
            // Find the child whose name matches the component.
            ArtworkNode* found = nullptr;
            int foundRow       = -1;
            for (int i = 0; i < static_cast<int>(current->children.size()); ++i) {
                  if (current->children[i]->name == components[depth]) {
                        found    = current->children[i].get();
                        foundRow = i;
                        break;
                        }
                  }
            if (!found)
                  return {};
            current    = found;
            currentIdx = index(foundRow, 0, currentIdx);
            }
      return currentIdx;
      }

//=========================================================
//   DxfToSvgConverter
//    Implements DRW_Interface to collect all DXF entities
//    and convert them into an SVG string for preview display.
//=========================================================

class DxfToSvgConverter final : public DRW_Interface {
      double m_unitScale {1.0};
      double m_dxfScale {72.0};  // dpmm for $INSUNITS=0 (pixel units)
      double m_minX {std::numeric_limits<double>::max()};
      double m_minY {std::numeric_limits<double>::max()};
      double m_maxX {std::numeric_limits<double>::lowest()};
      double m_maxY {std::numeric_limits<double>::lowest()};
      std::ostringstream m_svgBody;
      // Block support
      struct BlockEntity {
            enum class Type { Line, Arc, Circle, LWPolyline, Ellipse, Point };
            Type type;
            DRW_Coord p1, p2;
            double radius {0.0};
            double startAng {0.0};
            double endAng {0.0};
            double ratio {0.0};
            double staparam {0.0};
            double endparam {0.0};
            int isccw {1};
            std::vector<DRW_Vertex2D> vertices;
            int flags {0};
            };
      std::unordered_map<std::string, std::vector<BlockEntity>> m_blocks;
      std::string m_currentBlockName;
      bool m_inBlock {false};

      double mm(double v) const { return v * m_unitScale; }
      double mmX(double v) { double vmm = mm(v); if (vmm < m_minX) m_minX = vmm; if (vmm > m_maxX) m_maxX = vmm; return vmm; }
      double mmY(double v) { double vmm = mm(v); if (vmm < m_minY) m_minY = vmm; if (vmm > m_maxY) m_maxY = vmm; return vmm; }

      static double unitToMm(int unit) {
            switch (unit) {
                  case 0: return 1.0;
                  case 1: return 25.4;
                  case 2: return 25.4 * 12;
                  case 3: return 1609344.0;
                  case 4: return 1.0;
                  case 5: return 10.0;
                  case 6: return 1000.0;
                  case 7: return 1000000.0;
                  case 8: return 25.4 / 1000000.0;
                  case 9: return 25.4 / 1000.0;
                  case 10: return 25.4 * 36;
                  default: return 1.0;
                  }
            }

      void emitLine(double x1, double y1, double x2, double y2) {
            m_svgBody << "<path d=\"M" << std::fixed << std::setprecision(3)
                  << x1 << "," << y1 << " L" << x2 << "," << y2
                  << "\" fill=\"none\" stroke=\"#333333\" stroke-width=\"0.3\"/>\n";
            }
      void emitCircle(double cx, double cy, double r) {
            m_svgBody << "<circle cx=\"" << std::fixed << std::setprecision(3)
                  << cx << "\" cy=\"" << cy << "\" r=\"" << r
                  << "\" fill=\"none\" stroke=\"#333333\" stroke-width=\"0.3\"/>\n";
            }
      void emitArc(double cx, double cy, double r, double sa, double ea, bool ccw) {
            if (!ccw) std::swap(sa, ea);
            if (sa > ea) ea += 2.0 * std::numbers::pi;
            double sx = cx + std::cos(sa) * r;
            double sy = cy + std::sin(sa) * r;
            double ex = cx + std::cos(ea) * r;
            double ey = cy + std::sin(ea) * r;
            double sweep = ea - sa;
            bool largeArc = sweep > std::numbers::pi;
            int sweepFlag = ccw ? 1 : 0;
            m_svgBody << "<path d=\"M" << std::fixed << std::setprecision(3)
                  << sx << "," << sy << " A" << r << "," << r
                  << " 0 " << (largeArc ? 1 : 0) << " " << sweepFlag
                  << " " << ex << "," << ey
                  << "\" fill=\"none\" stroke=\"#333333\" stroke-width=\"0.3\"/>\n";
            }
      void emitPolyline(const std::vector<std::pair<double,double>>& pts, bool closed) {
            if (pts.empty()) return;
            m_svgBody << "<path d=\"" << std::fixed << std::setprecision(3)
                  << "M" << pts[0].first << "," << pts[0].second;
            for (size_t i = 1; i < pts.size(); ++i)
                  m_svgBody << " L" << pts[i].first << "," << pts[i].second;
            if (closed)
                  m_svgBody << " Z";
            m_svgBody << "\" fill=\"none\" stroke=\"#333333\" stroke-width=\"0.3\"/>\n";
            }
      void emitEllipseArc(double cx, double cy, double majorR, double minorR,
                          double rotation, double sa, double ea, bool ccw) {
            if (!ccw) std::swap(sa, ea);
            if (sa > ea) ea += 2.0 * std::numbers::pi;
            constexpr int segs = 64;
            double step = (ea - sa) / segs;
            std::vector<std::pair<double,double>> pts;
            double cosR = std::cos(rotation);
            double sinR = std::sin(rotation);
            for (int i = 0; i <= segs; ++i) {
                  double t = sa + step * i;
                  double ex = majorR * std::cos(t);
                  double ey = minorR * std::sin(t);
                  double rx = ex * cosR - ey * sinR;
                  double ry = ex * sinR + ey * cosR;
                  pts.emplace_back(rx + cx, ry + cy);
                  }
            emitPolyline(pts, false);
            }

      void expandBBox(double x, double y) {
            if (x < m_minX) m_minX = x;
            if (x > m_maxX) m_maxX = x;
            if (y < m_minY) m_minY = y;
            if (y > m_maxY) m_maxY = y;
            }

public:
      explicit DxfToSvgConverter(double dxfScale = 72.0) : m_dxfScale(dxfScale > 0.0 ? dxfScale : 72.0) {}

      QString svgString() const {
            if (m_minX > m_maxX || m_minY > m_maxY)
                  return {};
            double w = m_maxX - m_minX;
            double h = m_maxY - m_minY;
            if (w < 0.001) w = 0.001;
            if (h < 0.001) h = 0.001;

            // Target size in pixels for the generated SVG. The preview tile will
            // scale the SVG with preserveAspectRatio="xMidYMid meet", so the
            // drawing should fill the whole viewBox to be as large as possible.
            // A larger targetSize gives higher resolution previews.
            constexpr double targetSize = 1024.0;
            // Padding around the content so thin strokes on the outer edges are
            // not clipped by the SVG's own boundaries.
            constexpr double padding = 6.0;

            const double scale = targetSize / std::max(w, h);
            const double viewW  = w * scale + 2.0 * padding;
            const double viewH  = h * scale + 2.0 * padding;

            std::ostringstream oss;
            oss << std::fixed << std::setprecision(3);
            oss << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 "
                << viewW << " " << viewH
                << "\" width=\"" << viewW << "\" height=\"" << viewH
                << "\" preserveAspectRatio=\"xMidYMid meet\">\n";
            // Flip Y (SVG origin top-left, DXF origin bottom-left) and scale
            // the content so its bounding box fills the viewBox.  The drawing
            // is shifted so its bounding box starts at (padding, padding) inside
            // the viewBox [0, 0, viewW, viewH].
            // Transform order is right-to-left:
            // 1) translate(-minX, -minY)   move lower-left corner to (0,0)
            // 2) scale(scale, -scale)    scale to target size AND flip Y
            // 3) translate(padding, viewH-padding)  place inside padded viewBox
            oss << "<g transform=\"translate(" << padding << " " << viewH - padding
                << ") scale(" << scale << " " << -scale
                << ") translate(" << -m_minX << " " << -m_minY << ")\">\n";
            // Stroke width in the drawing's local (pre-transform) coordinate
            // system.  Because the <g> transform applies scale(scale, -scale),
            // a stroke-width of S becomes S*scale device pixels.  To get a
            // final width of ~targetSize*0.012 (≈12px for 1024), divide the
            // desired device-space width by the scale factor.  The clamp
            // ensures a minimum of 0.01 (for very large drawings) and a
            // maximum of 0.5 (for tiny drawings where scale > 1).
            const double sw = std::clamp(targetSize * 0.012 / scale, 0.01, 0.5);
            std::string body = m_svgBody.str();
            std::ostringstream swss;
            swss << std::fixed << std::setprecision(4) << sw;
            std::string old_sw = "stroke-width=\"0.3\"";
            std::string new_sw = "stroke-width=\"" + swss.str() + "\"";
            size_t pos = 0;
            while ((pos = body.find(old_sw, pos)) != std::string::npos) {
                  body.replace(pos, old_sw.length(), new_sw);
                  pos += new_sw.length();
                  }
            oss << body;
            oss << "</g>\n</svg>\n";
            return QString::fromStdString(oss.str());
            }
      //---- DRW_Interface overrides (read side) -------------------------
      void addHeader(const DRW_Header* data) override {
            auto it = data->vars.find("$INSUNITS");
            if (it != data->vars.end() && it->second->type() == DRW_Variant::INTEGER) {
                  int unit = it->second->content.i;
                  if (unit == 0) {
                        // pixel units -> apply configured dpmm scale
                        m_unitScale = 1.0 / m_dxfScale;
                        }
                  else {
                        m_unitScale = unitToMm(unit);
                        }
                  }
            else {
                  // Fall back to $MEASUREMENT only when $INSUNITS was not present
                  auto mit = data->vars.find("$MEASUREMENT");
                  if (mit != data->vars.end() && mit->second->type() == DRW_Variant::INTEGER) {
                        if (mit->second->content.i == 0)
                              m_unitScale = 25.4;
                        }
                  }
            }
      void addLType(const DRW_LType&) override {}
      void addLayer(const DRW_Layer&) override {}
      void addDimStyle(const DRW_Dimstyle&) override {}
      void addVport(const DRW_Vport&) override {}
      void addTextStyle(const DRW_Textstyle&) override {}
      void addAppId(const DRW_AppId&) override {}
      void addBlock(const DRW_Block& data) override {
            m_currentBlockName = data.name;
            m_blocks[m_currentBlockName].clear();
            m_inBlock = true;
            }
      void setBlock(int) override {}
      void endBlock() override { m_inBlock = false; m_currentBlockName.clear(); }

      void addPoint(const DRW_Point& data) override {
            if (m_inBlock) {
                  BlockEntity e; e.type = BlockEntity::Type::Point; e.p1 = data.basePoint;
                  m_blocks[m_currentBlockName].push_back(std::move(e)); return;
                  }
            double x = mmX(data.basePoint.x);
            double y = mmY(data.basePoint.y);
            emitCircle(x, y, 0.3);
            expandBBox(x - 0.3, y - 0.3);
            expandBBox(x + 0.3, y + 0.3);
            }
      void addLine(const DRW_Line& data) override {
            if (m_inBlock) {
                  BlockEntity e; e.type = BlockEntity::Type::Line; e.p1 = data.basePoint; e.p2 = data.secPoint;
                  m_blocks[m_currentBlockName].push_back(std::move(e)); return;
                  }
            double x1 = mmX(data.basePoint.x);
            double y1 = mmY(data.basePoint.y);
            double x2 = mmX(data.secPoint.x);
            double y2 = mmY(data.secPoint.y);
            emitLine(x1, y1, x2, y2);
            }
      void addRay(const DRW_Ray&) override {}
      void addXline(const DRW_Xline&) override {}

      void addArc(const DRW_Arc& data) override {
            if (m_inBlock) {
                  BlockEntity e; e.type = BlockEntity::Type::Arc; e.p1 = data.basePoint;
                  e.radius = data.radious; e.startAng = data.staangle; e.endAng = data.endangle; e.isccw = data.isccw;
                  m_blocks[m_currentBlockName].push_back(std::move(e)); return;
                  }
            double r = mm(data.radious);
            if (r <= 0.0) return;
            double cx = mm(data.basePoint.x);
            double cy = mm(data.basePoint.y);
            // Expand bbox by actual arc extent, not full circle.
            // Note: use mm() not mmX/mmY for center — the arc center may be
            // far outside the actual arc segment (e.g. huge radius, tiny
            // sweep) and must NOT pollute the bounding box.
            double sa = data.staangle;
            double ea = data.endangle;
            if (data.isccw == 0)
                  std::swap(sa, ea);
            if (sa > ea)
                  ea += 2.0 * std::numbers::pi;
            expandBBox(cx + r * std::cos(sa), cy + r * std::sin(sa));
            expandBBox(cx + r * std::cos(ea), cy + r * std::sin(ea));
            for (double a : {0.0, std::numbers::pi / 2, std::numbers::pi, 3.0 * std::numbers::pi / 2}) {
                  double na = a;
                  while (na < sa) na += 2.0 * std::numbers::pi;
                  while (na > sa + 2.0 * std::numbers::pi + 1e-10) na -= 2.0 * std::numbers::pi;
                  if (na >= sa - 1e-10 && na <= ea + 1e-10)
                        expandBBox(cx + r * std::cos(na), cy + r * std::sin(na));
                  }
            emitArc(cx, cy, r, data.staangle, data.endangle, data.isccw != 0);
            }
      void addCircle(const DRW_Circle& data) override {
            if (m_inBlock) {
                  BlockEntity e; e.type = BlockEntity::Type::Circle; e.p1 = data.basePoint; e.radius = data.radious;
                  m_blocks[m_currentBlockName].push_back(std::move(e)); return;
                  }
            double r = mm(data.radious);
            if (r <= 0.0) return;
            double cx = mmX(data.basePoint.x);
            double cy = mmY(data.basePoint.y);
            expandBBox(cx - r, cy - r);
            expandBBox(cx + r, cy + r);
            emitCircle(cx, cy, r);
            }
      void addEllipse(const DRW_Ellipse& data) override {
            if (m_inBlock) {
                  BlockEntity e; e.type = BlockEntity::Type::Ellipse; e.p1 = data.basePoint; e.p2 = data.secPoint;
                  e.ratio = data.ratio; e.staparam = data.staparam; e.endparam = data.endparam; e.isccw = data.isccw;
                  m_blocks[m_currentBlockName].push_back(std::move(e)); return;
                  }
            double majorR = std::sqrt(data.secPoint.x * data.secPoint.x + data.secPoint.y * data.secPoint.y);
            majorR = mm(majorR);
            double minorR = majorR * data.ratio;
            double rotation = std::atan2(data.secPoint.y, data.secPoint.x);
            double cx = mm(data.basePoint.x);
            double cy = mm(data.basePoint.y);
            // Note: use mm() not mmX/mmY for center — the ellipse center may
            // be far outside the actual arc segment and must NOT pollute the bbox.
            bool full = (data.staparam == 0.0 && std::abs(data.endparam - 2.0 * std::numbers::pi) < 1e-6);
            if (full) {
                  expandBBox(cx - majorR, cy - majorR);
                  expandBBox(cx + majorR, cy + majorR);
                  emitEllipseArc(cx, cy, majorR, minorR, rotation, 0.0, 2.0 * std::numbers::pi, true);
                  }
            else {
                  // For elliptical arcs, tessellate and expand by actual points.
                  constexpr int segs = 64;
                  double sa = data.staparam;
                  double ea = data.endparam;
                  if (data.isccw == 0)
                        std::swap(sa, ea);
                  if (sa > ea)
                        ea += 2.0 * std::numbers::pi;
                  double step = (ea - sa) / segs;
                  double cosR = std::cos(rotation);
                  double sinR = std::sin(rotation);
                  for (int i = 0; i <= segs; ++i) {
                        double t = sa + step * i;
                        double ex = majorR * std::cos(t);
                        double ey = minorR * std::sin(t);
                        expandBBox(ex * cosR - ey * sinR + cx, ex * sinR + ey * cosR + cy);
                        }
                  emitEllipseArc(cx, cy, majorR, minorR, rotation, data.staparam, data.endparam, data.isccw != 0);
                  }
            }
      void addLWPolyline(const DRW_LWPolyline& data) override {
            if (data.vertlist.empty()) return;
            if (m_inBlock) {
                  BlockEntity e; e.type = BlockEntity::Type::LWPolyline;
                  for (const auto& v : data.vertlist) e.vertices.push_back(*v);
                  e.flags = data.flags;
                  m_blocks[m_currentBlockName].push_back(std::move(e)); return;
                  }
            std::vector<std::pair<double,double>> pts;
            for (const auto& v : data.vertlist) {
                  double x = mmX(v->x);
                  double y = mmY(v->y);
                  pts.emplace_back(x, y);
                  }
            emitPolyline(pts, (data.flags & 1) != 0);
            }
      void addPolyline(const DRW_Polyline& data) override {
            if (data.vertlist.empty()) return;
            std::vector<std::pair<double,double>> pts;
            for (const auto& v : data.vertlist) {
                  double x = mmX(v->basePoint.x);
                  double y = mmY(v->basePoint.y);
                  pts.emplace_back(x, y);
                  }
            emitPolyline(pts, (data.flags & 1) != 0);
            }
      void addSpline(const DRW_Spline* data) override {
            if (!data || data->controllist.empty()) return;
            std::vector<std::pair<double,double>> pts;
            for (const auto& cp : data->controllist) {
                  double x = mmX(cp->x);
                  double y = mmY(cp->y);
                  pts.emplace_back(x, y);
                  }
            emitPolyline(pts, false);
            }
      void addKnot(const DRW_Entity&) override {}
      void addInsert(const DRW_Insert& data) override {
            auto it = m_blocks.find(data.name);
            if (it == m_blocks.end()) return;
            double ang = data.angle;
            double sx = data.xscale;
            double sy = data.yscale;
            double cx = mm(data.basePoint.x);
            double cy = mm(data.basePoint.y);
            double cosA = std::cos(ang);
            double sinA = std::sin(ang);
            auto apply = [&](const DRW_Coord& p) -> std::pair<double,double> {
                  double px = p.x * sx;
                  double py = p.y * sy;
                  double rx = px * cosA - py * sinA;
                  double ry = px * sinA + py * cosA;
                  return {mm(rx) + cx, mm(ry) + cy};
                  };
            for (const auto& e : it->second) {
                  switch (e.type) {
                        case BlockEntity::Type::Line: {
                              auto p1 = apply(e.p1);
                              auto p2 = apply(e.p2);
                              expandBBox(p1.first, p1.second);
                              expandBBox(p2.first, p2.second);
                              emitLine(p1.first, p1.second, p2.first, p2.second);
                              break;
                              }
                        case BlockEntity::Type::Circle: {
                              double r = mm(e.radius) * std::abs(sx);
                              auto c = apply(e.p1);
                              expandBBox(c.first - r, c.second - r);
                              expandBBox(c.first + r, c.second + r);
                              emitCircle(c.first, c.second, r);
                              break;
                              }
                        case BlockEntity::Type::Arc: {
                              double r = mm(e.radius);
                              auto c = apply(e.p1);
                              expandBBox(c.first - r, c.second - r);
                              expandBBox(c.first + r, c.second + r);
                              emitArc(c.first, c.second, r, e.startAng, e.endAng, e.isccw != 0);
                              break;
                              }
                        case BlockEntity::Type::LWPolyline: {
                              std::vector<std::pair<double,double>> pts;
                              for (const auto& v : e.vertices)
                                    pts.push_back(apply(DRW_Coord(v.x, v.y, 0)));
                              emitPolyline(pts, (e.flags & 1) != 0);
                              break;
                              }
                        case BlockEntity::Type::Point: {
                              auto p = apply(e.p1);
                              expandBBox(p.first - 0.3, p.second - 0.3);
                              expandBBox(p.first + 0.3, p.second + 0.3);
                              emitCircle(p.first, p.second, 0.3);
                              break;
                              }
                        case BlockEntity::Type::Ellipse: {
                              double majorR = std::sqrt(e.p2.x * e.p2.x + e.p2.y * e.p2.y);
                              majorR = mm(majorR);
                              double minorR = majorR * e.ratio;
                              double rotation = std::atan2(e.p2.y, e.p2.x);
                              auto c = apply(e.p1);
                              expandBBox(c.first - majorR, c.second - majorR);
                              expandBBox(c.first + majorR, c.second + majorR);
                              emitEllipseArc(c.first, c.second, majorR, minorR, rotation,
                                             e.staparam, e.endparam, e.isccw != 0);
                              break;
                              }
                        default: break;
                        }
                  }
            }
      void addTrace(const DRW_Trace& data) override {
            std::vector<std::pair<double,double>> pts;
            pts.push_back({mmX(data.basePoint.x), mmY(data.basePoint.y)});
            pts.push_back({mmX(data.secPoint.x), mmY(data.secPoint.y)});
            pts.push_back({mmX(data.thirdPoint.x), mmY(data.thirdPoint.y)});
            pts.push_back({mmX(data.fourPoint.x), mmY(data.fourPoint.y)});
            emitPolyline(pts, true);
            }
      void add3dFace(const DRW_3Dface& data) override {
            std::vector<std::pair<double,double>> pts;
            pts.push_back({mmX(data.basePoint.x), mmY(data.basePoint.y)});
            pts.push_back({mmX(data.secPoint.x), mmY(data.secPoint.y)});
            pts.push_back({mmX(data.thirdPoint.x), mmY(data.thirdPoint.y)});
            if (!(data.invisibleflag & DRW_3Dface::FourthEdge))
                  pts.push_back({mmX(data.fourPoint.x), mmY(data.fourPoint.y)});
            emitPolyline(pts, true);
            }
      void addSolid(const DRW_Solid& data) override {
            std::vector<std::pair<double,double>> pts;
            pts.push_back({mmX(data.basePoint.x), mmY(data.basePoint.y)});
            pts.push_back({mmX(data.secPoint.x), mmY(data.secPoint.y)});
            pts.push_back({mmX(data.thirdPoint.x), mmY(data.thirdPoint.y)});
            pts.push_back({mmX(data.fourPoint.x), mmY(data.fourPoint.y)});
            emitPolyline(pts, true);
            }
      void addMText(const DRW_MText&) override {}
      void addText(const DRW_Text&) override {}
      void addDimAlign(const DRW_DimAligned*) override {}
      void addDimLinear(const DRW_DimLinear*) override {}
      void addDimRadial(const DRW_DimRadial*) override {}
      void addDimDiametric(const DRW_DimDiametric*) override {}
      void addDimAngular(const DRW_DimAngular*) override {}
      void addDimAngular3P(const DRW_DimAngular3p*) override {}
      void addDimOrdinate(const DRW_DimOrdinate*) override {}
      void addLeader(const DRW_Leader*) override {}
      void addHatch(const DRW_Hatch*) override {}
      void addViewport(const DRW_Viewport&) override {}
      void addImage(const DRW_Image*) override {}
      void linkImage(const DRW_ImageDef*) override {}
      void addComment(const char*) override {}
      void addPlotSettings(const DRW_PlotSettings*) override {}
      void writeHeader(DRW_Header&) override {}
      void writeBlocks() override {}
      void writeBlockRecords() override {}
      void writeEntities() override {}
      void writeLTypes() override {}
      void writeLayers() override {}
      void writeTextstyles() override {}
      void writeVports() override {}
      void writeDimstyles() override {}
      void writeObjects() override {}
      void writeAppId() override {}
      };

//---------------------------------------------------------
//   ArtworkTreeModel::dxfToSvg
//    Reads a DXF file and converts it to an SVG string
//    for preview display in the media browser.
//---------------------------------------------------------

QString ArtworkTreeModel::dxfToSvg(const QString& filePath, double dxfScale) const {
      DxfToSvgConverter converter(dxfScale);
      dxfRW dxf(filePath.toUtf8().constData());
      if (!dxf.read(&converter, false)) {
            Warning("dxfToSvg: failed to read DXF file: {}", filePath.toUtf8().constData());
            return {};
            }
      return converter.svgString();
      }

//---------------------------------------------------------
//   ArtworkTreeModel::dxfToSvgFile
//    Reads a DXF file, converts it to SVG, writes the SVG
//    to a temporary file, and returns a file:// URL
//    suitable for use as an Image source in QML.
//---------------------------------------------------------

QString ArtworkTreeModel::dxfToSvgFile(const QString& filePath, double dxfScale) const {
      QString svg = dxfToSvg(filePath, dxfScale);
      if (svg.isEmpty())
            return {};
      // Write the SVG preview to the system temp directory. Using the DXF
      // folder is unreliable because artwork directories may be read-only or
      // on read-only media. A deterministic filename lets QML reload the
      // preview whenever the DXF file changes.
      QFileInfo fi(filePath);
      const QString base = QStringLiteral("%1_%2_dxfpreview.svg")
                               .arg(QString::fromUtf8(fi.absoluteFilePath().toUtf8().toBase64().replace('/', '_').replace('+', '-').constData()))
                               .arg(fi.lastModified().toMSecsSinceEpoch());
      QString tempPath = QDir::tempPath() + "/" + base;
      if (!QFile::remove(tempPath)) {
            // remove may legitimately fail when the file does not exist yet
            }
      QFile f(tempPath);
      if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            Warning("dxfToSvgFile: cannot write temp file: {} ({})", tempPath.toUtf8().constData(), f.errorString().toUtf8().constData());
            return {};
            }
      f.write(svg.toUtf8());
      f.close();
      return QStringLiteral("file://%1").arg(tempPath);
      }