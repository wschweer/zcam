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

#include <QFontDatabase>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <algorithm>

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
      if (v == _rootPath)
            return;
      _rootPath = v;
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
            auto child         = std::make_unique<ArtworkNode>();
            child->name        = sub;
            child->path        = dir.filePath(sub);
            child->parent      = node;
            child->hasImages   = directoryHasImagesRecursive(child->path);
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
            case HasChildrenRole: return node->hasChildren || !node->loaded;  // not-yet-loaded nodes may have children
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