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
#include <nlohmann/json.hpp>
using json = nlohmann::json;
#include "macros.h"
//---------------------------------------------------------
//   LaserLayerSetting
//---------------------------------------------------------

class LaserPass
{
      Q_GADGET
      QML_VALUE_TYPE(laserLayerSetting)

      PROP_GADGET(QString, name)
      PROPV_GADGET(bool, enabled, false)
      PROPV_GADGET(double, power, 20.0)
      PROPV_GADGET(double, speed, 1000.0)
      PROPV_GADGET(double, travelSpeed, 4000.0)
      PROPV_GADGET(double, frequency, 40.0)
      PROPV_GADGET(int, pulseWidth, 200)
      PROPV_GADGET(int, numPasses, 1)
      PROPV_GADGET(double, interval, 0.02)
      PROPV_GADGET(double, startAngle, 0)
      PROPV_GADGET(double, angleIncrement, 90.0)
      PROPV_GADGET(bool, zigzag, true)
      PROPV_GADGET(int, interleave, 1)
      PROPV_GADGET(bool, wobble, false)
      PROPV_GADGET(double, wobbleStep, 0.05)
      PROPV_GADGET(double, wobbleSize, 0.1)

      inline static constexpr std::string_view _properties {R"({
            "class": "Layer Setting",
            "items": [
                  { "name": "name", "label": "Name", "type": "singleline" },
                  { "row":
                        {
                              "enabled":    { "label": "enabled", "type": "bool", "default": false },
                              "numPasses":  { "label": "passes",  "type": "int", "min": 1, "max": 10000, "default": 1 }
                        },
                        "label": " "
                     },
                  { "name": "line", "type": "line" },
                  { "columns": {
                        "count": 2,
                        "items": [
                              { "row":
                                    {
                                          "power":       { "label": "Power",       "type": "float", "unit": "%",  "min": 0.0,    "max": 100.0,    "default": 20.0 },
                                          "frequency":   { "label": "Frequency",  "type": "float", "unit": "kHz", "default": 40.0 }
                                    },
                                    "label": "Laser"
                              },
                              { "row":
                                    {
                                          "speed":       { "label": "burn",   "type": "float", "unit": "mm/s","min": 0.0, "max": 100000.0,  "default": 1000.0 },
                                          "travelSpeed": { "label": "travel", "type": "float", "unit": "mm/s","min": 0.0, "max": 100000.0,  "default": 4000.0 }
                                    },
                                    "label": "Speed"
                              },
                              { "row":
                                    {
                                          "pulseWidth": { "label": "Pulse", "type": "pulsewidth", "unit": "ns" },
                                          "_empty":  { "label": "",       "type": "empty" }
                                    },
                                    "label": " "
                              },
                              { "name": "interval",       "label": "Interval",   "type": "float", "unit": "mm",  "min": 0.0,    "max": 100.0,   "default": 0.02 },
                              { "row":
                                    {
                                          "startAngle":     { "label": "Start", "type": "float", "unit": "°", "min": -360.0, "max": 360.0, "default": 0.0 },
                                          "angleIncrement": { "label": "Incr",  "type": "float", "unit": "°", "min": -360.0, "max": 360.0, "default": 90.0 }
                                    },
                                    "label": "Angle"
                              },
                              { "name": "line", "type": "line", "colSpan": 2 },
                              { "row":
                                    {
                                          "zigzag":     { "label": "Zigzag",    "type": "bool", "default": true },
                                          "interleave": { "label": "Interleave","type": "int",  "min": 1, "max": 100, "default": 1 }
                                    },
                                    "label": " "
                              },
                              { "row":
                                    {
                                          "wobble":     { "label": "enable", "type": "bool", "default": false },
                                          "wobbleStep": { "label": "Step",   "type": "float", "unit": "mm", "min": 0.0, "max": 10.0, "default": 0.05 },
                                          "wobbleSize": { "label": "Size",   "type": "float", "unit": "mm", "min": 0.0, "max": 10.0, "default": 0.1 }
                                    },
                                    "label": "Wobble"
                              }
                        ]
                        }
                  }
                  ]
                                    })"};

    public:
      LaserPass() {}
      json toJson() const;
      void fromJson(const json&);
      const std::string_view properties() const { return _properties; }
};
//---------------------------------------------------------
//   LaserLayersSettings
//---------------------------------------------------------

class LaserPasses : public std::vector<LaserPass>
{
      Q_GADGET

    public:
      json toJson() const;
      void fromJson(const json&);
};
//---------------------------------------------------------
//   Recipe
//---------------------------------------------------------

class LaserRecipe
{
      Q_GADGET
      QML_VALUE_TYPE(recipe)

      PROP_GADGET(QString, name)
      PROP_GADGET(QString, description)
      PROPV_GADGET(int, numPasses, 1)
      LaserPasses _passes;

      // Internal metadata: relative file path within the recipes directory.
      // Not serialized in toJson() / not read from fromJson().
      // Set during loadFromDirectory() or when a new recipe is created.
      QString _relativeFilePath;

    public:
      LaserRecipe() {}
      json toJson() const;
      void fromJson(const json&);
      const LaserPasses* layers() const { return &_passes; }
      const LaserPass* layer(int idx) const { return &_passes[idx]; }
      LaserPasses* passes() { return &_passes; }
      LaserPass* pass(int idx) { return &_passes[idx]; }
      QString relativeFilePath() const { return _relativeFilePath; }
      void setRelativeFilePath(const QString& p) { _relativeFilePath = p; }
};
//=========================================================
//   RecipeTreeModel
//    A QAbstractItemModel that represents the recipe directory
//    structure as a tree.  Folders are branch nodes, recipe
//    files are leaf nodes.  Each leaf carries a recipeIdx that
//    indexes into LaserReceipes::recipes.
//=========================================================

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
      Q_INVOKABLE bool isDir(const QModelIndex& idx) const;
      Q_INVOKABLE QString path(const QModelIndex& idx) const;
      Q_INVOKABLE QModelIndex rootIndex() const { return {}; }

    private:
      struct Node;
      std::unique_ptr<Node> _root;
      Node* nodeForIndex(const QModelIndex& idx) const;
      QModelIndex indexForNode(Node* node) const;
};
//---------------------------------------------------------
//   Recipes
//---------------------------------------------------------

class LaserReceipes : public QObject
{
      Q_OBJECT
      QML_ELEMENT
      QML_UNCREATABLE("no no")

      Q_PROPERTY(QStringList recipeModel READ recipeModel NOTIFY recipeModelChanged)
      Q_PROPERTY(RecipeTreeModel* recipeTreeModel READ recipeTreeModel CONSTANT)

      std::vector<LaserRecipe> recipes;
      RecipeTreeModel* _treeModel;
      QString _rootDir; // root directory, saved for folder operations

      /// Rebuild the tree model from the in-memory recipes list.
      void rebuildTreeModel();

    signals:
      void recipeModelChanged();
      void recipeChanged(int idx);

    public:
      LaserReceipes(QObject* parent = nullptr);
      ~LaserReceipes();
      Q_INVOKABLE LaserRecipe recipe(int idx) const {
            if (idx >= 0 && idx < static_cast<int>(recipes.size()))
                  return recipes[idx];
            return LaserRecipe();
      }
      int recipeCount() const { return static_cast<int>(recipes.size()); }
      LaserRecipe* recipePtr(int idx) {
            if (idx >= 0 && idx < static_cast<int>(recipes.size()))
                  return &recipes[idx];
            return nullptr;
      }
      Q_INVOKABLE void updateRecipe(int idx, const LaserRecipe& r);
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
      /// recursively descending into subdirectories.
      void loadFromDirectory(const QString& dir);
      /// Save all recipes as individual .json files into dir,
      /// preserving subdirectory structure.
      void saveToDirectory(const QString& dir) const;

      /// Create a new recipe in the given subdirectory (relative to root).
      /// If relDir is empty, the recipe is created in the root.
      Q_INVOKABLE void addRecipeInDir(const QString& name, const QString& relDir);

      /// Create a new subdirectory under the given parent directory
      /// (relative to root).  If parentRelDir is empty, the folder is
      /// created in the root.
      Q_INVOKABLE bool addFolder(const QString& folderName, const QString& parentRelDir);

      /// Remove a folder and all recipes inside it.
      Q_INVOKABLE bool removeFolder(const QString& relDir);
};

Q_DECLARE_METATYPE(LaserPass)
Q_DECLARE_METATYPE(LaserRecipe)