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

//---------------------------------------------------------
//   TreeModel
//---------------------------------------------------------

TreeModel::TreeModel(QObject* parent) : QAbstractItemModel(parent) {
      }

//---------------------------------------------------------
//   setRoot
//---------------------------------------------------------

void TreeModel::setRoot(Element* root) {
      beginResetModel();
      _root = root;
      endResetModel();
      emit rootChanged();
      }

//---------------------------------------------------------
//   index
//---------------------------------------------------------

QModelIndex TreeModel::index(int row, int column, const QModelIndex& parent) const {
      if (!hasIndex(row, column, parent)) {
            Debug("invalid");
            return {};
            }
      Element* parentItem = parent.isValid() ? static_cast<Element*>(parent.internalPointer()) : _root;
      if (parentItem) {
            int r = 0;
            for (auto obj : parentItem->children()) {
                  if (Element* childItem = qobject_cast<Element*>(obj)) {
                        if (r == row)
                              return createIndex(row, column, childItem);
                        r++;
                        }
                  else
                        Debug("no Element");
                  }
            }
      Debug("not found");
      return {};
      }

//---------------------------------------------------------
//   parent
//---------------------------------------------------------

QModelIndex TreeModel::parent(const QModelIndex& child) const {
      if (!child.isValid())
            return {};
      Element* childItem = static_cast<Element*>(child.internalPointer());
      if (!childItem)
            return {};
      Element* parentItem = childItem->parent();
      if (parentItem == _root || !parentItem)
            return {};
      Element* grandParent = qobject_cast<Element*>(parentItem->parent());

      int row = 0;
      if (grandParent) {
            for (auto obj : grandParent->children()) {
                  if (Element* gChild = qobject_cast<Element*>(obj)) {
                        if (gChild == parentItem)
                              break;
                        row++;
                        }
                  }
            }
      return createIndex(row, 0, parentItem);
      }

//---------------------------------------------------------
//   rowCount
//---------------------------------------------------------

int TreeModel::rowCount(const QModelIndex& parent) const {
      if (parent.column() > 0)
            return 0;
      Element* parentItem = parent.isValid() ? static_cast<Element*>(parent.internalPointer()) : _root;
      if (!parentItem)
            return 0;
      return parentItem->children().size();
      }

//---------------------------------------------------------
//   columnCount
//---------------------------------------------------------

int TreeModel::columnCount(const QModelIndex& parent) const {
      return 1;
      }

//---------------------------------------------------------
//   data
//---------------------------------------------------------

QVariant TreeModel::data(const QModelIndex& index, int role) const {
      if (!index.isValid())
            return {};
      Element* item = static_cast<Element*>(index.internalPointer());
      if (role == Qt::DisplayRole || role == Qt::UserRole + 1)
            return item->name();
      if (role == Qt::UserRole + 2)
            return QVariant::fromValue(item);
      return {};
      }

//---------------------------------------------------------
//   roleNames
//---------------------------------------------------------

QHash<int, QByteArray> TreeModel::roleNames() const {
      QHash<int, QByteArray> roles = QAbstractItemModel::roleNames();
      roles[Qt::UserRole + 1]      = "name";
      roles[Qt::UserRole + 2]      = "element";
      return roles;
      }
