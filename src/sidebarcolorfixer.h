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
#include <QQuickWindow>
#include <QQuickItem>
#include <QTimer>
#include <QSet>

//--------------------------------------------------------------------
//     SideBarColorFixer
//--------------------------------------------------------------------
//  Qt 6.12 regression: the Material SideBar.qml buttonDelegate's IconLabel
//  no longer sets an explicit `color`, causing the sidebar button text to
//  default to black (QColor() = invalid = black) — unreadable on the dark
//  Material background.
//
//  This event filter catches Expose events on QQuickWindows that look like
//  file dialogs (by title), waits 50ms for the QML delegates to instantiate,
//  then walks the item tree and sets every QQuickIconLabel's `color`
//  property to white.

class SideBarColorFixer : public QObject {
    Q_OBJECT
public:
    SideBarColorFixer(QObject* parent = nullptr)
        : QObject(parent)
    {
        qApp->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject* obj, QEvent* event) override
    {
        if (event->type() == QEvent::Expose) {
            if (auto* win = qobject_cast<QQuickWindow*>(obj)) {
                // Only process windows that are NOT the main application window.
                // File dialogs have titles like "Open Project", "Save Project As", etc.
                // and they are transient windows.
                const QString title = win->title();
                if (!title.isEmpty() && win->transientParent()) {
                    // Delay the fix to allow QML delegates to be created
                    QTimer::singleShot(50, this, [win]() {
                        if (win->contentItem())
                            fixIconLabels(win->contentItem());
                    });
                }
            }
        }
        return false;
    }

private:
    static void fixIconLabels(QQuickItem* item)
    {
        if (!item)
            return;

        const char* className = item->metaObject()->className();
        if (strcmp(className, "QQuickIconLabel") == 0) {
            // Fix text color (the `color` property)
            QVariant colorVar = item->property("color");
            if (colorVar.isValid()) {
                QColor color = colorVar.value<QColor>();
                if (!color.isValid() || color == Qt::black) {
                    item->setProperty("color", QColor(Qt::white));
                }
            }
            // Fix icon color (the `defaultIconColor` property).
            // Default is Qt::transparent (not black), making icons invisible.
            QVariant iconColorVar = item->property("defaultIconColor");
            if (iconColorVar.isValid()) {
                QColor iconColor = iconColorVar.value<QColor>();
                if (!iconColor.isValid() || iconColor == Qt::black || iconColor == Qt::transparent) {
                    item->setProperty("defaultIconColor", QColor(Qt::white));
                }
            }
        }

        const auto children = item->childItems();
        for (QQuickItem* child : children)
            fixIconLabels(child);
    }
};