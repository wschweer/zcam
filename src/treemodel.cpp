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
#include "element.h"

//---------------------------------------------------------
//   TreeModel
//---------------------------------------------------------

TreeModel::TreeModel(QObject* parent) : QAbstractItemModel(parent) {
      }

//---------------------------------------------------------
//   setRoot
//---------------------------------------------------------

void TreeModel::setRoot(Element* r) {
      beginResetModel();
      if (_root) {
            //            dump("old model");
            _root->deleteLater();
            }
      _root = r;
      endResetModel();
      emit rootChanged();
      //      dump("new model");
      }

//---------------------------------------------------------
//   dump
//---------------------------------------------------------

void TreeModel::dump(std::string s) const {

      std::function<std::string(const Element*, int)> a = [&a](const Element* e, int level) {
            if (e == nullptr)
                  return std::string("???");
            std::string s;
            for (int i = 0; i < level; ++i)
                  s += "    ";
            s += format("{} children: {}\n", e->name(), e->children().size());
            for (const auto ee : e->children())
                  s += a(ee, level + 1);
            return s;
            };
      if (_root)
            Debug("Dump: {}\n{}", s, a(_root, 0));
      }

//---------------------------------------------------------
//   index
//---------------------------------------------------------

QModelIndex TreeModel::index(int row, int column, const QModelIndex& parent) const {
      if (!hasIndex(row, column, parent))
            return {};
      Element* parentItem = parent.isValid() ? static_cast<Element*>(parent.internalPointer()) : _root;
      if (parentItem) {
            int r = 0;
            for (auto obj : parentItem->children()) {
                  if (Element* childItem = qobject_cast<Element*>(obj)) {
                        if (r == row)
                              return createIndex(row, column, childItem);
                        r++;
                        }
                  }
            }
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
      // Only count children that are Element subclasses, matching index().
      int count = 0;
      for (auto obj : parentItem->children())
            if (qobject_cast<Element*>(obj))
                  ++count;
      return count;
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

//---------------------------------------------------------
//   indexForElement
//    Depth-first search for the QModelIndex that corresponds
//    to the given Element.  Returns an invalid index if not
//    found or if the element is the root (root has no valid
//    parent index in the model).
//---------------------------------------------------------

QModelIndex TreeModel::indexForElement(Element* el) const {
      if (!el || !_root || el == _root)
            return {};
      Element* parent = el->parent();
      if (!parent)
            return {};
      // Recursively get the parent's index first.
      QModelIndex parentIdx;
      if (parent != _root)
            parentIdx = indexForElement(parent);
      // Find the row of el within parent's children list.
      int row = 0;
      for (auto obj : parent->children()) {
            if (Element* child = qobject_cast<Element*>(obj)) {
                  if (child == el)
                        return index(row, 0, parentIdx);
                  ++row;
                  }
            }
      return {};
      }

//---------------------------------------------------------
//   notifyChildInserted
//    Call beginInsertRows / endInsertRows so the view updates
//    when a new child has been added to parent at row.
//    Must be called AFTER the child was added to the data.
//---------------------------------------------------------

void TreeModel::notifyChildInserted(Element* parent, int row) {
      QModelIndex parentIdx;
      if (parent && parent != _root)
            parentIdx = indexForElement(parent);
      beginInsertRows(parentIdx, row, row);
      endInsertRows();
      }

//---------------------------------------------------------
//   notifyChildRemoved
//    Call beginRemoveRows / endRemoveRows so the view updates
//    when a child has been removed from parent at row.
//    Must be called AFTER the child was removed from the data.
//---------------------------------------------------------

void TreeModel::notifyChildRemoved(Element* parent, int row) {
      QModelIndex parentIdx;
      if (parent && parent != _root)
            parentIdx = indexForElement(parent);
      beginRemoveRows(parentIdx, row, row);
      endRemoveRows();
      }

//---------------------------------------------------------
//   beginInsertChild / endInsertChild
//    Split API: caller calls beginInsertChild() BEFORE adding
//    the child to the data, then endInsertChild() AFTER.
//---------------------------------------------------------

void TreeModel::beginInsertChild(Element* parent, int row) {
      QModelIndex parentIdx;
      if (parent && parent != _root)
            parentIdx = indexForElement(parent);
      beginInsertRows(parentIdx, row, row);
      }

void TreeModel::endInsertChild() {
      endInsertRows();
      }

//---------------------------------------------------------
//   beginRemoveChild / endRemoveChild
//    Split API: caller calls beginRemoveChild() BEFORE removing
//    the child from the data, then endRemoveChild() AFTER.
//---------------------------------------------------------

void TreeModel::beginRemoveChild(Element* parent, int row) {
      QModelIndex parentIdx;
      if (parent && parent != _root)
            parentIdx = indexForElement(parent);
      beginRemoveRows(parentIdx, row, row);
      }

void TreeModel::endRemoveChild() {
      endRemoveRows();
      }

//---------------------------------------------------------
//   notifyElementRenamed
//    Emit dataChanged for the \"name\" role so the TreeView
//    updates its display when an Element's name changes.
//---------------------------------------------------------

void TreeModel::notifyElementRenamed(Element* el) {
      if (!el || !_root || el == _root)
            return;
      QModelIndex idx = indexForElement(el);
      if (idx.isValid())
            emit dataChanged(idx, idx, {Qt::UserRole + 1});
      }
