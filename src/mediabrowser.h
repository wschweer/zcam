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

#include <QAbstractListModel>
#include <QAbstractItemModel>
#include <QQmlEngine>
#include <QStringList>
#include <QString>
#include <QHash>
#include <QVariantList>
#include <memory>
#include <vector>

//---------------------------------------------------------
//   FontModel
//    Provides a list of available system fonts or favourite
//    fonts to QML.  Favourites are persisted in QSettings.
//    Also exposes font styles and weights for a given family.
//---------------------------------------------------------

class FontModel : public QAbstractListModel
      {
      Q_OBJECT
      QML_ELEMENT

      Q_PROPERTY(bool showFavorites READ showFavorites WRITE setShowFavorites NOTIFY showFavoritesChanged)
      Q_PROPERTY(QString currentFamily READ currentFamily WRITE setCurrentFamily NOTIFY currentFamilyChanged)
      Q_PROPERTY(QString currentStyle READ currentStyle WRITE setCurrentStyle NOTIFY currentStyleChanged)

    public:
      enum Roles {
            FamilyRole = Qt::UserRole + 1,
            IsFavoriteRole,
            };
      explicit FontModel(QObject* parent = nullptr);

      int rowCount(const QModelIndex& parent = QModelIndex()) const override;
      QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
      QHash<int, QByteArray> roleNames() const override;
      bool showFavorites() const { return _showFavorites; }
      void setShowFavorites(bool v);
      QString currentFamily() const { return _currentFamily; }
      void setCurrentFamily(const QString& v);
      QString currentStyle() const { return _currentStyle; }
      void setCurrentStyle(const QString& v);

      Q_INVOKABLE QStringList stylesForFamily(const QString& family) const;
      Q_INVOKABLE QStringList weightsForFamily(const QString& family) const;
      Q_INVOKABLE QString familyAt(int row) const;
      Q_INVOKABLE void addFavorite(const QString& family);
      Q_INVOKABLE void removeFavorite(const QString& family);
      Q_INVOKABLE bool isFavorite(const QString& family) const;
      Q_INVOKABLE QStringList allFamilies() const;
      Q_INVOKABLE QStringList favoriteFamilies() const;

    signals:
      void showFavoritesChanged();
      void currentFamilyChanged();
      void currentStyleChanged();
      void favoritesChanged();

    private:
      void rebuildList();
      void loadFavorites();
      void saveFavorites();

      bool _showFavorites {false};
      QString _currentFamily;
      QString _currentStyle;
      QStringList _allFamilies;
      QStringList _favorites;
      QStringList _visibleFamilies;
      };

//---------------------------------------------------------
//   ArtworkNode
//    A node in the artwork directory tree.
//---------------------------------------------------------

struct ArtworkNode {
      QString name;
      QString path;
      bool hasImages {false};
      bool hasChildren {false};
      bool loaded {false};
      std::vector<std::unique_ptr<ArtworkNode>> children;
      ArtworkNode* parent {nullptr};
      };

//---------------------------------------------------------
//   ArtworkTreeModel
//    A tree model that shows only directories containing
//    image files (*.svg, *.png, *.dxf) – directly or in
//    any descendant directory.  Directories are loaded
//    lazily when expanded.
//---------------------------------------------------------

class ArtworkTreeModel : public QAbstractItemModel
      {
      Q_OBJECT
      QML_ELEMENT
      Q_PROPERTY(QString rootPath READ rootPath WRITE setRootPath NOTIFY rootPathChanged)

    public:
      enum Roles {
            NameRole = Qt::UserRole + 1,
            PathRole,
            HasImagesRole,
            HasChildrenRole,
            };
      Q_ENUM(Roles)
      explicit ArtworkTreeModel(QObject* parent = nullptr);
      ~ArtworkTreeModel() override;

      int rowCount(const QModelIndex& parent = QModelIndex()) const override;
      int columnCount(const QModelIndex& parent = QModelIndex()) const override;
      QModelIndex index(int row, int column, const QModelIndex& parent = QModelIndex()) const override;
      QModelIndex parent(const QModelIndex& child) const override;
      QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
      QHash<int, QByteArray> roleNames() const override;
      bool hasChildren(const QModelIndex& parent = QModelIndex()) const override;

      bool canFetchMore(const QModelIndex& parent) const override;
      void fetchMore(const QModelIndex& parent) override;
      QString rootPath() const { return _rootPath; }
      void setRootPath(const QString& v);

      /// Returns a list of {fileName, filePath, fileType} objects
      /// for all image files directly in the given directory.
      Q_INVOKABLE QVariantList imageFiles(const QString& dirPath) const;
      Q_INVOKABLE bool hasImages(const QString& dirPath) const;
      Q_INVOKABLE void refresh();

      /// Convert a DXF file to an SVG string suitable for
      /// preview display in the media browser. dxfScale is the
      /// pixel density in dots per millimeter used when $INSUNITS=0.
      Q_INVOKABLE QString dxfToSvg(const QString& filePath, double dxfScale = 72.0) const;

      /// Convert a DXF file to a temporary SVG file and return
      /// the file:// URL for use as an Image source in QML.
      Q_INVOKABLE QString dxfToSvgFile(const QString& filePath, double dxfScale = 72.0) const;

      /// Returns the model index for the directory whose path matches
      /// dirPath.  All ancestor nodes are lazily loaded so the caller
      /// can expand and select the index in the TreeView.
      Q_INVOKABLE QModelIndex findIndexForPath(const QString& dirPath) const;

    signals:
      void rootPathChanged();

    private:
      void rebuildTree();
      void loadChildren(ArtworkNode* node);
      bool directoryHasImages(const QString& dirPath) const;
      bool directoryHasImagesRecursive(const QString& dirPath) const;
      ArtworkNode* nodeForIndex(const QModelIndex& idx) const;
      QModelIndex indexForNode(ArtworkNode* node) const;

      QString _rootPath;
      std::unique_ptr<ArtworkNode> _root;
      QStringList _imageExtensions {"svg", "png", "dxf"};
      };