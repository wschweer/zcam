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

// treemodel.h
#pragma once
#include <QAbstractItemModel>
#include <QQmlEngine>

class Element;

//---------------------------------------------------------
//   TreeModel
//---------------------------------------------------------

class TreeModel : public QAbstractItemModel
      {
      Q_OBJECT
      QML_ELEMENT
      Q_PROPERTY(Element* root READ root WRITE setRoot NOTIFY rootChanged)
      Element* _root = nullptr;

    signals:
      void rootChanged();

    public:
      explicit TreeModel(QObject* parent = nullptr);
      Element* root() const { return _root; }
      void setRoot(Element* root);
      /// Returns the Element* stored behind a model index, or nullptr.
      Q_INVOKABLE Element* elementForIndex(const QModelIndex& idx) const {
            if (!idx.isValid())
                  return nullptr;
            return static_cast<Element*>(idx.internalPointer());
            }
      QModelIndex indexForElement(Element* el) const;
      void notifyChildInserted(Element* parent, int row);
      void notifyChildRemoved(Element* parent, int row);
      void beginInsertChild(Element* parent, int row);
      void endInsertChild();
      void beginRemoveChild(Element* parent, int row);
      void endRemoveChild();
      void notifyElementRenamed(Element* el);
      QModelIndex index(int row, int column, const QModelIndex& parent = QModelIndex()) const override;
      QModelIndex parent(const QModelIndex& child) const override;
      int rowCount(const QModelIndex& parent = QModelIndex()) const override;
      int columnCount(const QModelIndex& parent = QModelIndex()) const override;
      QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
      QHash<int, QByteArray> roleNames() const override;
      void dump(std::string) const;
      };
