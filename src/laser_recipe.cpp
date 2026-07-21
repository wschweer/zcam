//=============================================================================
//  ZCam - manufacturing tool for G-code machines and Fiber Laser
//
//  Copyright (C) 2026 Werner Schweer
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#include "recipe.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStringList>
#include <QTextStream>
#include "logger.h"
#include <memory>
#include <map>
#include <functional>
//---------------------------------------------------------
//   LaserLayerSetting
//---------------------------------------------------------

json LaserPass::toJson() const {
      json data;
      data["name"]           = _name.toStdString();
      data["enabled"]        = _enabled;
      data["power"]          = _power;
      data["speed"]          = _speed;
      data["travelSpeed"]    = _travelSpeed;
      data["frequency"]      = _frequency;
      data["pulseWidth"]     = _pulseWidth;
      data["numPasses"]      = _numPasses;
      data["interval"]       = _interval;
      data["startAngle"]     = _startAngle;
      data["angleIncrement"] = _angleIncrement;
      data["zigzag"]         = _zigzag;
      data["interleave"]     = _interleave;
      data["wobble"]         = _wobble;
      data["wobbleStep"]     = _wobbleStep;
      data["wobbleSize"]     = _wobbleSize;
      return data;
}
//---------------------------------------------------------
//   fromJson
//---------------------------------------------------------

void LaserPass::fromJson(const json& data) {
      if (data.contains("name"))
            _name = QString::fromStdString(data.at("name").get<std::string>());
      if (data.contains("enabled"))
            _enabled = data.at("enabled");
      if (data.contains("power"))
            _power = data.at("power");
      if (data.contains("speed"))
            _speed = data.at("speed");
      if (data.contains("travelSpeed"))
            _travelSpeed = data.at("travelSpeed");
      if (data.contains("frequency"))
            _frequency = data.at("frequency");
      if (data.contains("pulseWidth"))
            _pulseWidth = data.at("pulseWidth");
      if (data.contains("numPasses"))
            _numPasses = data.at("numPasses");
      if (data.contains("interval"))
            _interval = data.at("interval");
      if (data.contains("startAngle"))
            _startAngle = data.at("startAngle");
      if (data.contains("angleIncrement"))
            _angleIncrement = data.at("angleIncrement");
      if (data.contains("zigzag"))
            _zigzag = data.at("zigzag");
      if (data.contains("interleave"))
            _interleave = data.at("interleave");
      if (data.contains("wobble"))
            _wobble = data.at("wobble");
      if (data.contains("wobbleStep"))
            _wobbleStep = data.at("wobbleStep");
      if (data.contains("wobbleSize"))
            _wobbleSize = data.at("wobbleSize");
}
//---------------------------------------------------------
//   LaserLayersSettings
//---------------------------------------------------------

json LaserPasses::toJson() const {
      json data = json::array();
      for (const auto& layer : *this)
            data.push_back(layer.toJson());
      return data;
}
//---------------------------------------------------------
//   fromJson
//---------------------------------------------------------

void LaserPasses::fromJson(const json& data) {
      clear();
      if (data.is_array()) {
            for (const auto& jlayer : data) {
                  LaserPass l;
                  l.fromJson(jlayer);
                  push_back(l);
            }
      }
}
//---------------------------------------------------------
//   Recipe
//---------------------------------------------------------

json LaserRecipe::toJson() const {
      json data;
      data["name"]        = _name.toStdString();
      data["description"] = _description.toStdString();
      data["numPasses"]   = _numPasses;
      data["layer"]       = _passes.toJson();
      return data;
}
//---------------------------------------------------------
//   fromJson
//---------------------------------------------------------

void LaserRecipe::fromJson(const json& data) {
      if (data.contains("name"))
            _name = QString::fromStdString(data.at("name").get<std::string>());
      if (data.contains("description"))
            _description = QString::fromStdString(data.at("description").get<std::string>());
      if (data.contains("numPasses"))
            _numPasses = data.at("numPasses");
      if (data.contains("layer"))
            _passes.fromJson(data.at("layer"));
}
//=========================================================
//   RecipeTreeModel
//=========================================================

struct RecipeTreeModel::Node {
      QString name;
      QString relativePath; // relative to recipes root
      bool isDir    = false;
      int recipeIdx = -1; // valid for leaf nodes only
      Node* parent  = nullptr;
      std::vector<std::unique_ptr<Node>> children;
};
RecipeTreeModel::RecipeTreeModel(QObject* parent)
    : QAbstractItemModel(parent), _root(std::make_unique<Node>()) {
      _root->name  = QStringLiteral("root");
      _root->isDir = true;
}
RecipeTreeModel::~RecipeTreeModel() = default;
//---------------------------------------------------------
//   nodeForIndex
//---------------------------------------------------------

RecipeTreeModel::Node* RecipeTreeModel::nodeForIndex(const QModelIndex& idx) const {
      if (!idx.isValid())
            return _root.get();
      return static_cast<Node*>(idx.internalPointer());
}
//---------------------------------------------------------
//   indexForNode
//---------------------------------------------------------

QModelIndex RecipeTreeModel::indexForNode(Node* node) const {
      if (!node || node == _root.get())
            return {};
      Node* parent = node->parent;
      if (!parent)
            return {};
      int row = 0;
      for (const auto& c : parent->children) {
            if (c.get() == node)
                  return createIndex(row, 0, node);
            ++row;
      }
      return {};
}
//---------------------------------------------------------
//   index
//---------------------------------------------------------

QModelIndex RecipeTreeModel::index(int row, int column, const QModelIndex& parent) const {
      if (!hasIndex(row, column, parent))
            return {};
      Node* parentNode = nodeForIndex(parent);
      if (parentNode && row >= 0 && row < static_cast<int>(parentNode->children.size()))
            return createIndex(row, column, parentNode->children[row].get());
      return {};
}
//---------------------------------------------------------
//   parent
//---------------------------------------------------------

QModelIndex RecipeTreeModel::parent(const QModelIndex& child) const {
      if (!child.isValid())
            return {};
      Node* childNode = static_cast<Node*>(child.internalPointer());
      if (!childNode || !childNode->parent)
            return {};
      Node* parentNode = childNode->parent;
      if (parentNode == _root.get())
            return {};
      return indexForNode(parentNode);
}
//---------------------------------------------------------
//   rowCount
//---------------------------------------------------------

int RecipeTreeModel::rowCount(const QModelIndex& parent) const {
      Node* node = nodeForIndex(parent);
      if (!node)
            return 0;
      return static_cast<int>(node->children.size());
}
//---------------------------------------------------------
//   columnCount
//---------------------------------------------------------

int RecipeTreeModel::columnCount(const QModelIndex& parent) const {
      Q_UNUSED(parent)
      return 1;
}
//---------------------------------------------------------
//   data
//---------------------------------------------------------

QVariant RecipeTreeModel::data(const QModelIndex& index, int role) const {
      Node* node = nodeForIndex(index);
      if (!node || node == _root.get())
            return {};
      switch (role) {
            case Qt::DisplayRole:
            case NameRole: return node->name;
            case IsDirRole: return node->isDir;
            case RecipeIdxRole: return node->recipeIdx;
            case PathRole: return node->relativePath;
      }
      return {};
}
//---------------------------------------------------------
//   roleNames
//---------------------------------------------------------

QHash<int, QByteArray> RecipeTreeModel::roleNames() const {
      QHash<int, QByteArray> roles;
      roles[NameRole]      = "nodeName";
      roles[IsDirRole]     = "isDir";
      roles[RecipeIdxRole] = "recipeIdx";
      roles[PathRole]      = "nodePath";
      return roles;
}
//---------------------------------------------------------
//   clear / beginBuild / endBuild
//---------------------------------------------------------

void RecipeTreeModel::clear() {
      beginResetModel();
      _root->children.clear();
      endResetModel();
}
void RecipeTreeModel::beginBuild() {
      beginResetModel();
      _root->children.clear();
}
void RecipeTreeModel::endBuild() {
      endResetModel();
}
//---------------------------------------------------------
//   addDirNode
//---------------------------------------------------------

void* RecipeTreeModel::addDirNode(const QString& name, const QString& relativePath, void* parent) {
      Node* parentNode   = parent ? static_cast<Node*>(parent) : _root.get();
      auto node          = std::make_unique<Node>();
      node->name         = name;
      node->relativePath = relativePath;
      node->isDir        = true;
      node->parent       = parentNode;
      Node* raw          = node.get();
      parentNode->children.push_back(std::move(node));
      return raw;
}
//---------------------------------------------------------
//   addRecipeNode
//---------------------------------------------------------

void* RecipeTreeModel::addRecipeNode(const QString& name, const QString& relativePath, int recipeIdx,
                                     void* parent) {
      Node* parentNode   = parent ? static_cast<Node*>(parent) : _root.get();
      auto node          = std::make_unique<Node>();
      node->name         = name;
      node->relativePath = relativePath;
      node->isDir        = false;
      node->recipeIdx    = recipeIdx;
      node->parent       = parentNode;
      Node* raw          = node.get();
      parentNode->children.push_back(std::move(node));
      return raw;
}
//---------------------------------------------------------
//   removeNode
//---------------------------------------------------------

void RecipeTreeModel::removeNode(const QModelIndex& idx) {
      Node* node = nodeForIndex(idx);
      if (!node || node == _root.get())
            return;
      Node* parent = node->parent;
      if (!parent)
            return;
      int row = 0;
      for (auto& c : parent->children) {
            if (c.get() == node) {
                  beginRemoveRows(indexForNode(parent), row, row);
                  parent->children.erase(parent->children.begin() + row);
                  endRemoveRows();
                  return;
            }
            ++row;
      }
}
//---------------------------------------------------------
//   Q_INVOKABLE helpers
//---------------------------------------------------------

int RecipeTreeModel::recipeIndex(const QModelIndex& idx) const {
      Node* node = nodeForIndex(idx);
      if (!node || node->isDir)
            return -1;
      return node->recipeIdx;
}
bool RecipeTreeModel::isDir(const QModelIndex& idx) const {
      Node* node = nodeForIndex(idx);
      return node && node->isDir;
}
QString RecipeTreeModel::path(const QModelIndex& idx) const {
      Node* node = nodeForIndex(idx);
      return node ? node->relativePath : QString();
}
//=========================================================
//   LaserReceipes
//=========================================================

LaserReceipes::LaserReceipes(QObject* parent) : QObject(parent), _treeModel(new RecipeTreeModel(this)) {
}
LaserReceipes::~LaserReceipes() = default;
void LaserReceipes::updateRecipe(int idx, const LaserRecipe& r) {
      if (idx >= 0 && idx < recipes.size()) {
            recipes[idx] = r;
            emit recipeModelChanged();
            emit recipeChanged(idx);
      }
}
void LaserReceipes::addRecipe(const QString& name) {
      LaserRecipe r;
      r.set_name(name);
      recipes.push_back(r);
      emit recipeModelChanged();
      rebuildTreeModel();
}
void LaserReceipes::removeRecipe(int idx) {
      if (idx >= 0 && idx < recipes.size()) {
            recipes.erase(recipes.begin() + idx);
            emit recipeModelChanged();
            rebuildTreeModel();
      }
}
LaserPass LaserReceipes::layer(int recipeIdx, int layerIdx) {
      if (recipeIdx >= 0 && recipeIdx < recipes.size()) {
            const auto& r = recipes[recipeIdx];
            if (layerIdx >= 0 && layerIdx < r.layers()->size())
                  return *r.layer(layerIdx);
      }
      return LaserPass();
}
LaserPass* LaserReceipes::layerPtr(int recipeIdx, int layerIdx) {
      if (recipeIdx >= 0 && recipeIdx < recipes.size()) {
            auto& r = recipes[recipeIdx];
            if (layerIdx >= 0 && layerIdx < r.passes()->size())
                  return r.pass(layerIdx);
      }
      return nullptr;
}
void LaserReceipes::updateLayer(int recipeIdx, int layerIdx, const LaserPass& l) {
      if (recipeIdx >= 0 && recipeIdx < recipes.size()) {
            auto& r = recipes[recipeIdx];
            if (layerIdx >= 0 && layerIdx < r.passes()->size()) {
                  *r.pass(layerIdx) = l;
                  emit recipeChanged(recipeIdx);
            }
      }
}
void LaserReceipes::addLayer(int recipeIdx, const QString& name) {
      if (recipeIdx >= 0 && recipeIdx < recipes.size()) {
            LaserPass l;
            l.set_name(name);
            recipes[recipeIdx].passes()->push_back(l);
            emit recipeChanged(recipeIdx);
      }
}
void LaserReceipes::removeLayer(int recipeIdx, int layerIdx) {
      if (recipeIdx >= 0 && recipeIdx < recipes.size()) {
            auto& r = recipes[recipeIdx];
            if (layerIdx >= 0 && layerIdx < r.passes()->size()) {
                  r.passes()->erase(r.passes()->begin() + layerIdx);
                  emit recipeChanged(recipeIdx);
            }
      }
}
QStringList LaserReceipes::layerModel(int recipeIdx) const {
      QStringList names;
      if (recipeIdx >= 0 && recipeIdx < recipes.size())
            for (const auto& l : *recipes[recipeIdx].layers())
                  names.append(l.name());
      return names;
}
QStringList LaserReceipes::recipeModel() const {
      QStringList names;
      for (const auto& r : recipes)
            names.append(r.name());
      return names;
}
//---------------------------------------------------------
//   toJson
//---------------------------------------------------------

json LaserReceipes::toJson() const {
      json data = json::array();
      for (const auto& r : recipes)
            data.push_back(r.toJson());
      return data;
}
//---------------------------------------------------------
//   fromJson
//---------------------------------------------------------

void LaserReceipes::fromJson(const json& data) {
      recipes.clear();
      if (data.is_array()) {
            for (const auto& r : data) {
                  LaserRecipe recipe;
                  recipe.fromJson(r);
                  recipes.push_back(recipe);
            }
      }
      emit recipeModelChanged();
}
//---------------------------------------------------------
//   sanitiseFileName
//---------------------------------------------------------

static QString sanitiseFileName(QString name) {
      if (name.isEmpty())
            name = QStringLiteral("unnamed");
      name.replace(QRegularExpression("[^a-zA-Z0-9_\\-]"), "_");
      return name;
}
//---------------------------------------------------------
//   loadFromDirectory
//    Recursively load all .json recipe files from dir,
//    descending into subdirectories.  Builds the tree model.
//---------------------------------------------------------

void LaserReceipes::loadFromDirectory(const QString& dir) {
      recipes.clear();
      _rootDir = dir;

      _treeModel->beginBuild();

      QDir d(dir);
      if (d.exists()) {
            // Recursive lambda
            std::function<void(const QString& absPath, const QString& relPath, void* parentNode)> recurse =
                [&](const QString& absPath, const QString& relPath, void* parentNode) {
                      QDir d2(absPath);
                      // Process subdirectories first
                      const auto dirs = d2.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
                      for (const QString& subDir : dirs) {
                            QString subAbs = d2.filePath(subDir);
                            QString subRel = relPath.isEmpty() ? subDir : (relPath + "/" + subDir);
                            void* dirNode  = _treeModel->addDirNode(subDir, subRel, parentNode);
                            recurse(subAbs, subRel, dirNode);
                      }
                      // Process recipe files
                      const auto files = d2.entryList({"*.json"}, QDir::Files, QDir::Name);
                      for (const QString& fileName : files) {
                            QString filePath = d2.filePath(fileName);
                            QFile file(filePath);
                            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                                  Warning("LaserReceipes::loadFromDirectory: cannot open {}",
                                          filePath.toStdString());
                                  continue;
                            }
                            QByteArray data = file.readAll();
                            file.close();
                            try {
                                  json jr = json::parse(data.toStdString());
                                  LaserRecipe recipe;
                                  recipe.fromJson(jr);
                                  recipe.setRelativeFilePath(relPath);
                                  int idx = static_cast<int>(recipes.size());
                                  recipes.push_back(recipe);
                                  // Display name: use recipe name if available, otherwise file base name
                                  QString displayName = recipe.name().isEmpty()
                                                            ? QFileInfo(fileName).baseName()
                                                            : recipe.name();
                                  _treeModel->addRecipeNode(displayName, relPath, idx, parentNode);
                            }
                            catch (const json::parse_error& e) {
                                  Warning("LaserReceipes::loadFromDirectory: parse error in {}: {}",
                                          filePath.toStdString(), e.what());
                            }
                      }
                };
            recurse(dir, QString(), nullptr);
      }

      _treeModel->endBuild();
      emit recipeModelChanged();
}
//---------------------------------------------------------
//   saveToDirectory
//    Save all recipes as individual .json files into dir,
//    preserving subdirectory structure.
//---------------------------------------------------------

void LaserReceipes::saveToDirectory(const QString& dir) const {
      QDir rootDir(dir);
      if (!rootDir.exists())
            rootDir.mkpath(".");

      for (const auto& r : recipes) {
            QString relPath  = r.relativeFilePath();
            QString fileName = sanitiseFileName(r.name()) + ".json";

            // Ensure the subdirectory exists
            QString subDirPath = dir;
            if (!relPath.isEmpty())
                  subDirPath = QDir(dir).filePath(relPath);

            QDir subDir(subDirPath);
            if (!subDir.exists())
                  subDir.mkpath(".");

            QString filePath = QDir(subDirPath).filePath(fileName);
            QFile file(filePath);
            if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                  Warning("LaserReceipes::saveToDirectory: cannot open {} for writing",
                          filePath.toStdString());
                  continue;
            }
            json jr = r.toJson();
            QTextStream out(&file);
            out << QString::fromStdString(jr.dump(4));
            file.close();
      }
}
//---------------------------------------------------------
//   addRecipeInDir
//    Create a new recipe in the given subdirectory (relative
//    to root).  If relDir is empty, the recipe is created in
//    the root.
//---------------------------------------------------------

//---------------------------------------------------------
//   rebuildTreeModel
//    Rebuild the tree model from the in-memory recipes list.
//    Creates directory nodes for each unique relative path
//    and recipe leaf nodes under their respective directories.
//---------------------------------------------------------

void LaserReceipes::rebuildTreeModel() {
      _treeModel->beginBuild();

      // Collect unique directory paths and build folder nodes
      std::map<QString, void*> dirNodes; // relPath -> node ptr
      dirNodes[""] = nullptr;            // root

      // First pass: create all directory nodes
      for (const auto& r : recipes) {
            QString relPath = r.relativeFilePath();
            if (relPath.isEmpty())
                  continue;
            // Create intermediate directories
            QStringList parts = relPath.split("/", Qt::SkipEmptyParts);
            QString current;
            for (int i = 0; i < parts.size(); ++i) {
                  QString parent = current;
                  current        = parent.isEmpty() ? parts[i] : (parent + "/" + parts[i]);
                  if (dirNodes.find(current) == dirNodes.end()) {
                        void* parentNode  = dirNodes[parent];
                        void* dirNode     = _treeModel->addDirNode(parts[i], current, parentNode);
                        dirNodes[current] = dirNode;
                  }
            }
      }

      // Second pass: add recipe nodes
      for (int i = 0; i < static_cast<int>(recipes.size()); ++i) {
            const auto& r       = recipes[i];
            QString relPath     = r.relativeFilePath();
            void* parentNode    = dirNodes[relPath];
            QString displayName = r.name().isEmpty() ? QStringLiteral("unnamed") : r.name();
            _treeModel->addRecipeNode(displayName, relPath, i, parentNode);
      }

      _treeModel->endBuild();
}
void LaserReceipes::addRecipeInDir(const QString& name, const QString& relDir) {
      LaserRecipe r;
      r.set_name(name);
      r.setRelativeFilePath(relDir);
      recipes.push_back(r);
      emit recipeModelChanged();

      // Rebuild the tree model from the in-memory recipes list.
      // We don't reload from disk because the new recipe hasn't been saved yet.
      rebuildTreeModel();
}
//---------------------------------------------------------
//   addFolder
//    Create a new subdirectory under the given parent directory
//    (relative to root).  If parentRelDir is empty, the folder
//    is created in the root.
//---------------------------------------------------------

bool LaserReceipes::addFolder(const QString& folderName, const QString& parentRelDir) {
      if (_rootDir.isEmpty() || folderName.isEmpty())
            return false;

      QString sanitised = sanitiseFileName(folderName);
      QString dirPath   = parentRelDir.isEmpty()
                              ? QDir(_rootDir).filePath(sanitised)
                              : QDir(QDir(_rootDir).filePath(parentRelDir)).filePath(sanitised);

      QDir d(dirPath);
      if (d.exists())
            return true; // already exists, no error
      if (!d.mkpath("."))
            return false;

      // Reload from disk to show the new (possibly empty) folder in the tree
      loadFromDirectory(_rootDir);
      return true;
}
//---------------------------------------------------------
//   removeFolder
//    Remove a folder and all recipes inside it.
//---------------------------------------------------------

bool LaserReceipes::removeFolder(const QString& relDir) {
      if (_rootDir.isEmpty() || relDir.isEmpty())
            return false;

      QString dirPath = QDir(_rootDir).filePath(relDir);
      QDir d(dirPath);
      if (!d.exists())
            return false;

      // Remove all recipes that have this relativePath or a subdirectory of it
      auto it = recipes.begin();
      while (it != recipes.end()) {
            QString rp = it->relativeFilePath();
            if (rp == relDir || rp.startsWith(relDir + "/"))
                  it = recipes.erase(it);
            else
                  ++it;
      }

      // Remove the directory on disk
      d.removeRecursively();

      emit recipeModelChanged();
      rebuildTreeModel();
      return true;
}