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

#pragma once

#include <QObject>
#include <QAbstractItemModel>
#include <QtQml/qqmlregistration.h>
#include <QQmlEngine>
#include <nlohmann/json.hpp>
#include <memory>
#include <vector>
using json = nlohmann::json;
#include "macros.h"
#include "machine.h"
#include "machinetypes.h"

//---------------------------------------------------------
//   LaserPass
//---------------------------------------------------------

class LaserPass
      {
      Q_GADGET
      QML_VALUE_TYPE(laserLayerSetting)

      PROP_GADGET(QString, name)
      PROPV_GADGET(bool, enabled, false)
      PROPV_GADGET(int, numPasses, 1)

      PROPV_GADGET(double, power, 20.0)
      PROPV_GADGET(double, speed, 1000.0)
      PROPV_GADGET(double, frequency, 40.0)
      PROPV_GADGET(int, pulseWidth, 200)

      PROPV_GADGET(double, interval, 0.02)
      PROPV_GADGET(double, startAngle, 0)
      PROPV_GADGET(double, angleIncrement, 90.0)
      PROPV_GADGET(bool, zigzag, true)
      PROPV_GADGET(int, interleave, 1)
      PROPV_GADGET(bool, wobble, false)
      PROPV_GADGET(double, wobbleStep, 0.05)
      PROPV_GADGET(double, wobbleSize, 0.1)

      PROPV_GADGET(bool, overrideTimings, false)
      PROPV_GADGET(double, onDelay, 100.0)
      PROPV_GADGET(double, offDelay, 100.0)
      PROPV_GADGET(double, endDelay, 200.0)
      PROPV_GADGET(double, polygonDelay, 150.0)
      PROPV_GADGET(double, jumpSpeed, 6000.0)
      PROPV_GADGET(double, minJumpDelay, 200.0)
      PROPV_GADGET(double, maxJumpDelay, 400.0)
      PROPV_GADGET(double, jumpDistanceLimit, 10.0)

      PROPV_GADGET(bool, enableFPK, false)
      PROPV_GADGET(double, fpkStartPower, 10.0)
      PROPV_GADGET(double, fpkIncrement, 10.0)

      PROPV_GADGET(bool, enableTickle, true)
      PROPV_GADGET(double, ticklePulse, 1.0)
      PROPV_GADGET(double, tickleFrequence, 5.0)

      PROPV_GADGET(double, uvMinPulse, 1.0)
      PROPV_GADGET(double, uvMaxPulse, 20.0)

      Q_PROPERTY(double intervalLpi READ intervalLpi WRITE setIntervalLpi NOTIFY intervalChanged)
      Q_PROPERTY(double intervalLpmm READ intervalLpmm WRITE setIntervalLpmm NOTIFY intervalChanged)

    public:
      LaserPass() {}
      json toJson() const;
      void fromJson(const json&);
      double intervalLpi() const { return 25.4 / interval(); }
      double intervalLpmm() const { return 1.0 / interval(); }
      void setIntervalLpi(double v) { set_interval(25.4 / v); }
      void setIntervalLpmm(double v) { set_interval(1.0 / v); }
      };

//---------------------------------------------------------
//   LaserPasses
//---------------------------------------------------------

class LaserPasses : public std::vector<LaserPass>
      {
      Q_GADGET

    public:
      json toJson() const;
      void fromJson(const json&);
      };

//---------------------------------------------------------
//   LaserRecipe
//    as stored on disk as asset
//---------------------------------------------------------

class LaserRecipe : public QObject
      {
      Q_OBJECT
      QML_ELEMENT
      QML_UNCREATABLE("no no")

      PROP(QString, name)
      PROP(QString, description)
      PROPV(int, numPasses, 1)
      PROPV(MachineType, machineType, MachineType::UNKNOWN)

      LaserPasses _passes;

      // Internal metadata: relative file path within the recipes directory.
      // Not serialized in toJson() / not read from fromJson().
      // Set during loadFromDirectory() or when a new recipe is created.
      QString _relativeFilePath;

    public:
      LaserRecipe() {}
      json toJson() const;
      void fromJson(const json&);
      const LaserPasses& passes() const { return _passes; }
      LaserPasses& passes() { return _passes; }
      const LaserPass& pass(int idx) const { return _passes.at(idx); }
      LaserPass& pass(int idx) { return _passes.at(idx); }
      QString relativeFilePath() const { return _relativeFilePath; }
      void setRelativeFilePath(const QString& p) { _relativeFilePath = p; }
      const std::string properties() const;
      const std::string laserPassProperties() const;
      };

//---------------------------------------------------------
//   RecipeTreeModel
//    A QAbstractItemModel that represents the recipe directory
//    structure as a tree.  Folders are branch nodes, recipe
//    files are leaf nodes.  Each leaf carries a recipeIdx that
//    indexes into LaserReceipes::recipes.
//---------------------------------------------------------

class RecipeTreeModel : public QAbstractItemModel
      {
      Q_OBJECT
      QML_ELEMENT

    public:
      explicit RecipeTreeModel(QObject* parent = nullptr);
      ~RecipeTreeModel();

      QModelIndex index(int row, int column, const QModelIndex& parent = QModelIndex()) const override;
      QModelIndex parent(const QModelIndex& child) const override;
      int rowCount(const QModelIndex& parent = QModelIndex()) const override;
      int columnCount(const QModelIndex& parent = QModelIndex()) const override;
      QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
      QHash<int, QByteArray> roleNames() const override;
      enum Roles { NameRole = Qt::UserRole + 1, IsDirRole, RecipeIdxRole, PathRole };
      // ── Public API used by LaserReceipes ───────────────────────────
      void clear();
      void beginBuild();
      void endBuild();

      // Add a directory node under parent (or root if parent is null).
      // Returns the created node pointer.
      void* addDirNode(const QString& name, const QString& relativePath, void* parent);

      // Add a recipe leaf node under parent (or root if parent is null).
      void* addRecipeNode(const QString& name, const QString& relativePath, int recipeIdx, void* parent);

      // Remove the node at the given model index (and all its children).
      void removeNode(const QModelIndex& idx);

      // Q_INVOKABLE helpers for QML
      Q_INVOKABLE int recipeIndex(const QModelIndex& idx) const;
      Q_INVOKABLE QModelIndex indexForRecipe(int recipeIdx) const;
      Q_INVOKABLE bool isDir(const QModelIndex& idx) const;
      Q_INVOKABLE QString path(const QModelIndex& idx) const;
      Q_INVOKABLE QModelIndex rootIndex() const { return {}; }
      // Find the folder (dir) node with the given relative path, or an
      // invalid index if it does not exist.  Used by QML to restore the
      // expanded state of the tree after a model rebuild.
      Q_INVOKABLE QModelIndex indexForPath(const QString& relPath) const;
      // Number of top-level children of the root.  Used to enumerate the
      // top level without passing the (invalid) root QModelIndex from QML.
      Q_INVOKABLE int topRowCount() const;
      // Top-level child of the root at row \a row, or an invalid index if
      // \a row is out of range.
      Q_INVOKABLE QModelIndex topIndexAt(int row) const;

    private:
      struct Node;
      std::unique_ptr<Node> _root;
      Node* nodeForIndex(const QModelIndex& idx) const;
      QModelIndex indexForNode(Node* node) const;
      };

//---------------------------------------------------------
//   Recipes
//---------------------------------------------------------

class Recipe : public QObject
      {
      Q_OBJECT
      QML_ELEMENT
      QML_UNCREATABLE("no no")

      Q_PROPERTY(QStringList recipeModel READ recipeModel NOTIFY recipeModelChanged)
      Q_PROPERTY(RecipeTreeModel* recipeTreeModel READ recipeTreeModel CONSTANT)
      Q_PROPERTY(MachineType machineType READ machineType WRITE set_machineType NOTIFY machineTypeChanged)

      std::vector<std::unique_ptr<LaserRecipe>> recipes;
      RecipeTreeModel* _treeModel;
      QString _rootDir;                                // root directory, saved for folder operations
      MachineType _machineType {MachineType::Q_LASER}; // current machine type filter
      /// Ensure the QML engine does not take JavaScriptOwnership of
      /// a LaserRecipe pointer returned to QML.  LaserRecipe objects
      /// are owned by the Recipe container (std::unique_ptr); the QML
      /// garbage collector must not delete them.
      static LaserRecipe* cppOwned(LaserRecipe* p) {
            if (p)
                  QQmlEngine::setObjectOwnership(p, QQmlEngine::CppOwnership);
            return p;
            }
      /// Reload recipes from disk using the current _rootDir and _machineType.
      void reload();

      /// Rebuild the tree model from the in-memory recipes list.
      void rebuildTreeModel();

    signals:
      void recipeModelChanged();
      void recipeChanged(int idx);
      void machineTypeChanged();

    public:
      Recipe(QObject* parent = nullptr);
      ~Recipe();
      MachineType machineType() const { return _machineType; }
      void set_machineType(MachineType type);
      Q_INVOKABLE LaserRecipe* recipe(int idx) const {
            if (idx >= 0 && idx < static_cast<int>(recipes.size()))
                  return cppOwned(recipes[idx].get());
            return nullptr;
            }
      int recipeCount() const { return static_cast<int>(recipes.size()); }
      Q_INVOKABLE LaserRecipe* recipePtr(int idx) {
            if (idx >= 0 && idx < static_cast<int>(recipes.size()))
                  return cppOwned(recipes[idx].get());
            return nullptr;
            }
      Q_INVOKABLE int recipeIndexByName(const QString& name) const {
            for (int i = 0; i < static_cast<int>(recipes.size()); ++i)
                  if (recipes[i]->name() == name)
                        return i;
            return -1;
            }
      Q_INVOKABLE void updateRecipe(int idx, LaserRecipe* r);
      Q_INVOKABLE void addRecipe(const QString& name);
      Q_INVOKABLE void removeRecipe(int idx);

      Q_INVOKABLE LaserPass layer(int recipeIdx, int layerIdx);
      Q_INVOKABLE LaserPass* layerPtr(int recipeIdx, int layerIdx);
      Q_INVOKABLE void updateLayer(int recipeIdx, int layerIdx, const LaserPass& l);
      Q_INVOKABLE void addLayer(int recipeIdx, const QString& name);
      Q_INVOKABLE void removeLayer(int recipeIdx, int layerIdx);
      Q_INVOKABLE QStringList layerModel(int recipeIdx) const;
      QStringList recipeModel() const;
      RecipeTreeModel* recipeTreeModel() const { return _treeModel; }
      json toJson() const;
      void fromJson(const json&);

      /// Load all recipe files (one .json per recipe) from dir,
      /// recursively descending into subdirectories.  If machineType
      /// is non-default, only recipes under dir/machineTypeName/ are
      /// loaded and the machineType path component is stripped from
      /// relative paths.
      void loadFromDirectory(const QString& dir, MachineType mt = MachineType::Q_LASER);
      /// Save all recipes as individual .json files into dir,
      /// preserving subdirectory structure.  If mt is non-default,
      /// recipes are saved under dir/machineTypeName/.
      void saveToDirectory(const QString& dir, MachineType mt = MachineType::Q_LASER) const;

      /// Create a new recipe in the given subdirectory (relative to root).
      /// If relDir is empty, the recipe is created in the root.
      /// When sourceIdx is a valid index into the recipe list, the new
      /// recipe is a clone of that recipe (name, description, numPasses and
      /// passes are copied).  The machine type is always initialised with
      /// the type intended for the current directory (directories are named
      /// after their machine type).  The name is made unique within the
      /// target directory ("xxx-NNN" on a clash).
      /// Returns the index of the newly created recipe, or -1 on failure.
      Q_INVOKABLE int addRecipeInDir(const QString& name, const QString& relDir, int sourceIdx = -1);

      /// Create a new subdirectory under the given parent directory
      /// (relative to root).  If parentRelDir is empty, the folder is
      /// created in the root.
      Q_INVOKABLE bool addFolder(const QString& folderName, const QString& parentRelDir);

      /// Remove a folder and all recipes inside it.
      Q_INVOKABLE bool removeFolder(const QString& relDir);
      const std::string properties() const;
      };

Q_DECLARE_METATYPE(LaserPass)
Q_DECLARE_OPAQUE_POINTER(LaserRecipe*)
Q_DECLARE_OPAQUE_POINTER(LaserPass*)