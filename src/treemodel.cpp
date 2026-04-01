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

#include "treemodel.h"
#include "element3d.h"
TreeModel::TreeModel(QObject* parent) : QAbstractItemModel(parent) {
      }

void TreeModel::setRoot(Element3d* root) {
      beginResetModel();
      _root = root;
      endResetModel();
      emit rootChanged();
      }

QModelIndex TreeModel::index(int row, int column, const QModelIndex& parent) const {
      if (!hasIndex(row, column, parent))
            return {};
      Element3d* parentItem = parent.isValid() ? static_cast<Element3d*>(parent.internalPointer()) : _root;
      if (parentItem) {
            int r = 0;
            for (auto obj : parentItem->children()) {
                  if (Element3d* childItem = qobject_cast<Element3d*>(obj)) {
                        if (r == row)
                              return createIndex(row, column, childItem);
                        r++;
                        }
                  }
            }
      return {};
      }

QModelIndex TreeModel::parent(const QModelIndex& child) const {
      if (!child.isValid())
            return {};
      Element3d* childItem = static_cast<Element3d*>(child.internalPointer());
      if (!childItem)
            return {};
      Element3d* parentItem = qobject_cast<Element3d*>(childItem->parent());
      if (parentItem == _root || !parentItem)
            return {};
      Element3d* grandParent = qobject_cast<Element3d*>(parentItem->parent());

      int row = 0;
      if (grandParent) {
            for (auto obj : grandParent->children()) {
                  if (Element3d* gChild = qobject_cast<Element3d*>(obj)) {
                        if (gChild == parentItem)
                              break;
                        row++;
                        }
                  }
            }
      return createIndex(row, 0, parentItem);
      }

int TreeModel::rowCount(const QModelIndex& parent) const {
      if (parent.column() > 0)
            return 0;
      Element3d* parentItem = parent.isValid() ? static_cast<Element3d*>(parent.internalPointer()) : _root;
      if (!parentItem)
            return 0;
      int count = 0;
      for (auto child : parentItem->children())
            if (qobject_cast<Element3d*>(child))
                  count++;
      return count;
      }

int TreeModel::columnCount(const QModelIndex& parent) const {
      return 1;
      }

QVariant TreeModel::data(const QModelIndex& index, int role) const {
      if (!index.isValid())
            return {};
      Element3d* item = static_cast<Element3d*>(index.internalPointer());
      if (role == Qt::DisplayRole || role == Qt::UserRole + 1)
            return item->name();
      if (role == Qt::UserRole + 2)
            return QVariant::fromValue(item);
      return {};
      }

QHash<int, QByteArray> TreeModel::roleNames() const {
      QHash<int, QByteArray> roles = QAbstractItemModel::roleNames();
      roles[Qt::UserRole + 1]      = "name";
      roles[Qt::UserRole + 2]      = "element";
      return roles;
      }
