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

      PROPV_GADGET(bool, overrideTimings, false)
      PROPV_GADGET(double, onDelay, 100.0)
      PROPV_GADGET(double, offDelay, 100.0)
      PROPV_GADGET(double, endDelay, 1000.0)
      PROPV_GADGET(double, polygonDelay, 100.0)
      PROPV_GADGET(double, jumpSpeed, 6000.0)
      PROPV_GADGET(double, minJumpDelay, 200.0)
      PROPV_GADGET(double, maxJumpDelay, 400.0)
      PROPV_GADGET(double, jumpDistanceLimit, 10.0)

      Q_PROPERTY(double intervalLpi READ intervalLpi WRITE setIntervalLpi NOTIFY intervalChanged)
      Q_PROPERTY(double intervalLpmm READ intervalLpmm WRITE setIntervalLpmm NOTIFY intervalChanged)


      inline static constexpr std::string_view _properties {R"({
    "class": "Layer Setting",
    "rows": [
        {
            "label": "Name",
            "cells": [
                {
                    "name": "name",
                    "type": "singleline"
                }
            ]
        },
        {
            "label": " ",
            "cells": [
                {
                    "type": "bool",
                    "default": false,
                    "name": "enabled",
                    "sublabel": "enabled"
                },
                {
                    "type": "int",
                    "min": 1,
                    "max": 10000,
                    "default": 1,
                    "name": "numPasses",
                    "sublabel": "passes"
                }
            ]
        },
        {
            "cells": [
                {
                    "name": "line",
                    "type": "line"
                }
            ]
        },
        {
            "columns": 2,
            "cells": [
                {
                    "label": "Laser",
                    "colSpan": 2,
                    "cells": [
                        {
                            "type": "float",
                            "unit": "%",
                            "min": 0.0,
                            "max": 100.0,
                            "default": 20.0,
                            "name": "power",
                            "sublabel": "Power"
                        },
                        {
                            "type": "float",
                            "unit": "kHz",
                            "default": 40.0,
                            "name": "frequency",
                            "sublabel": "Frequency"
                        },
                        {
                            "type": "pulsewidth",
                            "unit": "ns",
                            "name": "pulseWidth",
                            "sublabel": "Pulse"
                        },
                        {
                            "type": "float",
                            "unit": "mm/s",
                            "min": 0.0,
                            "max": 100000.0,
                            "default": 1000.0,
                            "name": "speed",
                            "sublabel": "speed"
                        }
                    ]
                },
                {
                    "name": "line",
                    "type": "line",
                    "colSpan": 2
                },
                {
                    "label": "Hatch",
                    "cells": [
                        {
                            "name": "interval",
                            "sublabel": " ",
                            "type": "float",
                            "unit": "mm",
                            "min": 0.001,
                            "max": 100.0,
                            "default": 0.05
                        },
                        {
                            "name": "intervalLpi",
                            "sublabel": "Lpi",
                            "type": "float"
                        },
                        {
                            "name": "intervalLpmm",
                            "sublabel": "Lpmm",
                            "type": "float"
                        }
                      ]
                },
                {
                    "label": "Angle",
                    "cells": [
                        {
                            "type": "float",
                            "unit": "°",
                            "min": -360.0,
                            "max": 360.0,
                            "default": 0.0,
                            "name": "startAngle",
                            "sublabel": "Start"
                        },
                        {
                            "type": "float",
                            "unit": "°",
                            "min": -360.0,
                            "max": 360.0,
                            "default": 90.0,
                            "name": "angleIncrement",
                            "sublabel": "Incr"
                        }
                    ]
                },
                {
                    "label": " ",
                    "cells": [
                        {
                            "type": "bool",
                            "default": true,
                            "name": "zigzag",
                            "sublabel": "Zigzag"
                        },
                        {
                            "type": "int",
                            "min": 1,
                            "max": 100,
                            "default": 1,
                            "name": "interleave",
                            "sublabel": "Interleave"
                        }
                    ]
                },
                {
                    "label": "Wobble",
                    "cells": [
                        {
                            "type": "bool",
                            "default": false,
                            "name": "wobble",
                            "sublabel": "enable"
                        },
                        {
                            "type": "float",
                            "unit": "mm",
                            "min": 0.0,
                            "max": 10.0,
                            "default": 0.05,
                            "name": "wobbleStep",
                            "sublabel": "Step"
                        },
                        {
                            "type": "float",
                            "unit": "mm",
                            "min": 0.0,
                            "max": 10.0,
                            "default": 0.1,
                            "name": "wobbleSize",
                            "sublabel": "Size"
                        }
                    ]
                },
                {
                    "name": "line",
                    "type": "line",
                    "colSpan": 2
                },
                {
                    "label": "Override",
                    "cells": [
                        {
                            "name": "overrideTimings",
                            "sublabel": " ",
                            "type": "bool",
                            "default": false
                        },
                        {
                              "name": "leer",
                              "type": "empty"
                        },
                        {
                              "name": "leer",
                              "type": "empty"
                        }
                    ]
                },
                {
                    "label": "Jump",
                    "colSpan": 2,
                    "cells": [
                        {
                            "name": "jumpSpeed",
                            "sublabel": "speed",
                            "enabled": "overrideTimings",
                            "type": "double",
                            "min": 0,
                            "max": 99999.0,
                            "default": 6000.0,
                            "unit": "mm/s²"
                        },
                        {
                            "name": "jumpDistanceLimit",
                            "sublabel": "limit",
                            "enabled": "overrideTimings",
                            "type": "float",
                            "unit": "mm",
                            "min": 0.0,
                            "max": 100.0,
                            "default": 10.0
                        },
                        {
                            "name": "minJumpDelay",
                            "sublabel": "minDelay",
                            "enabled": "overrideTimings",
                            "type": "float",
                            "unit": "µs",
                            "min": -9999.0,
                            "max": 9999.0,
                            "default": 200.0
                        },
                        {
                            "name": "maxJumpDelay",
                            "sublabel": "maxDelay",
                            "enabled": "overrideTimings",
                            "type": "float",
                            "unit": "µs",
                            "min": -9999.0,
                            "max": 9999.0,
                            "default": 400.0
                        }
                    ]
                },
                {
                    "label": "Delay",
                    "colSpan": 2,
                    "cells": [
                        {
                            "name": "onDelay",
                            "sublabel": "on",
                            "enabled": "overrideTimings",
                            "type": "double",
                            "min": -9999,
                            "max": 9999.0,
                            "default": 100.0,
                            "unit": "µs"
                        },
                        {
                            "name": "offDelay",
                            "sublabel": "off",
                            "enabled": "overrideTimings",
                            "type": "float",
                            "unit": "µs",
                            "min": -9999.0,
                            "max": 9999.0,
                            "default": 100.0
                        },
                        {
                            "name": "endDelay",
                            "sublabel": "end",
                            "enabled": "overrideTimings",
                            "type": "float",
                            "unit": "µs",
                            "min": -9999.0,
                            "max": 9999.0,
                            "default": 100.0
                        },
                        {
                            "name": "polygonDelay",
                            "sublabel": "polygon",
                            "enabled": "overrideTimings",
                            "type": "float",
                            "unit": "µs",
                            "min": -9999.0,
                            "max": 9999.0,
                            "default": 100.0
                        }
                    ]
                }
            ]
        }
    ]
                              })"};

      //      PROPV_GADGET(double, onDelay, 100.0)
      //      PROPV_GADGET(double, offDelay, 100.0)
      //      PROPV_GADGET(double, endDelay, 1000.0)
      //      PROPV_GADGET(double, polygonDelay, 100.0)

    public:
      LaserPass() {}
      json toJson() const;
      void fromJson(const json&);
      const std::string_view properties() const { return _properties; }

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
      Q_INVOKABLE QModelIndex indexForRecipe(int recipeIdx) const;
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
      Q_PROPERTY(QString machineType READ machineType WRITE set_machineType NOTIFY machineTypeChanged)

      std::vector<LaserRecipe> recipes;
      RecipeTreeModel* _treeModel;
      QString _rootDir;     // root directory, saved for folder operations
      QString _machineType; // current machine type filter (empty = load all)

      /// Reload recipes from disk using the current _rootDir and _machineType.
      void reload();

      /// Rebuild the tree model from the in-memory recipes list.
      void rebuildTreeModel();

    signals:
      void recipeModelChanged();
      void recipeChanged(int idx);
      void machineTypeChanged();

    public:
      LaserReceipes(QObject* parent = nullptr);
      ~LaserReceipes();
      QString machineType() const { return _machineType; }
      void set_machineType(const QString& type);
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
      Q_INVOKABLE int recipeIndexByName(const QString& name) const {
            for (int i = 0; i < static_cast<int>(recipes.size()); ++i)
                  if (recipes[i].name() == name)
                        return i;
            return -1;
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
      /// recursively descending into subdirectories.  If machineType is
      /// non-empty, only recipes under dir/machineType/ are loaded and
      /// the machineType path component is stripped from relative paths.
      void loadFromDirectory(const QString& dir, const QString& machineType = {});
      /// Save all recipes as individual .json files into dir,
      /// preserving subdirectory structure.  If machineType is non-empty,
      /// recipes are saved under dir/machineType/.
      void saveToDirectory(const QString& dir, const QString& machineType = {}) const;

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

// Register pointer types as opaque so the QML engine does not
// attempt to manage their lifetime (e.g. via the garbage collector).
// LaserRecipe and LaserPass are Q_GADGET value types stored in a
// std::vector<LaserRecipe> inside LaserReceipes.  Pointers into that
// vector are returned to QML via Q_INVOKABLE methods and stored in
// Q_PROPERTY values.  Without Q_DECLARE_OPAQUE_POINTER the QML engine
// may take JavaScriptOwnership of the wrapper and free the pointed-to
// memory when the wrapper is garbage-collected — causing a double
// free when the std::vector later destroys or reallocates.
Q_DECLARE_OPAQUE_POINTER(LaserRecipe*)
Q_DECLARE_OPAQUE_POINTER(LaserPass*)