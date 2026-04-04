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
#include <QQmlEngine>
#include "element3d.h"
#include "text.h"
#include <nlohmann/json.hpp>

//---------------------------------------------------------
//   InspectorModel
//---------------------------------------------------------

class InspectorModel : public QAbstractListModel
      {
      Q_OBJECT
      QML_ELEMENT
      Q_PROPERTY(Element3d* element READ element WRITE setElement NOTIFY elementChanged)
      Q_PROPERTY(QString title READ title NOTIFY titleChanged)

    public:
      explicit InspectorModel(QObject* parent = nullptr);
      Element3d* element() const { return _element; }
      void setElement(Element3d* element);
      QString title() const { return _title; }
      enum Roles { PropNameRole = Qt::UserRole + 1, PropLabelRole, PropTypeRole, PropValueRole, PropUnitRole, PropMinRole, PropMaxRole };
      int rowCount(const QModelIndex& parent = QModelIndex()) const override;
      QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
      bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override;
      QHash<int, QByteArray> roleNames() const override;

    signals:
      void elementChanged();
      void titleChanged();

    private:
      void parseProperties();

      Element3d* _element = nullptr;
      QString _title;
      struct PropEntry {
            QString name;
            QString label;
            QString type;
            QVariant defaultValue;
            QString unit;
            double minVal = 0;
            double maxVal = 0;
            };
      QList<PropEntry> _properties;
      };
