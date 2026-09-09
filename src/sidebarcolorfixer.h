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
#include <QFileInfo>
#include <QUrl>
#include <QTimer>
#include <QSet>

//--------------------------------------------------------------------
//     SideBarColorFixer
//--------------------------------------------------------------------
//  Two unrelated file-dialog workarounds, both driven by a single
//  Expose-event filter on the QQuickWindows that back the Qt Quick
//  (quickimpl) file dialogs:
//
//  1) Qt 6.12 regression: the Material SideBar.qml buttonDelegate's
//     IconLabel no longer sets an explicit `color`, so the sidebar
//     button text defaults to black — unreadable on the dark Material
//     background.  After a dialog window is exposed we walk its item
//     tree and set every QQuickIconLabel's `color` to white.
//
//  2) Qt behaviour: in "Save" (export) mode QQuickFileDialogImpl only
//     fills the "File name" text field when the target file already
//     exists on disk (QQuickFileDialogImplPrivate::updateFileNameTextEdit
//     guards the setText() with `if (fileInfo.isFile())`).  For an
//     Export/Save dialog the file does not exist yet, so the field
//     stays empty even though `selectedFile` is set.  After the dialog
//     is exposed we find that (empty) text field and, when the dialog
//     has a selectedFile, fill the field with its file name so the
//     suggested name is visible in the dialog.
//
//  Both fixes are idempotent: they only *add* text / colour where the
//  field is empty or the colour is missing, so they never clobber
//  something the user already typed.
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
                        if (win->contentItem()) {
                            fixIconLabels(win->contentItem());
                            prefillSaveFileName(win);
                        }
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

    // Recursively find a QQuickItem by its `objectName` within an item subtree.
    static QQuickItem* findItemByObjectName(QQuickItem* item, const QString& objectName)
    {
        if (!item)
            return nullptr;
        if (item->objectName() == objectName)
            return item;
        const auto children = item->childItems();
        for (QQuickItem* child : children)
            if (auto* found = findItemByObjectName(child, objectName))
                return found;
        return nullptr;
    }

    // Walk up the parent chain from `from` and return the first ancestor that
    // carries a non-empty `selectedFile` URL property (the enclosing
    // QQuickFileDialogImpl).
    static QUrl selectedFileOf(const QObject* from)
    {
        for (const QObject* o = from; o; o = o->parent()) {
            const QVariant v = o->property("selectedFile");
            if (v.isValid() && v.canConvert<QUrl>()) {
                const QUrl url = v.toUrl();
                if (!url.isEmpty())
                    return url;
                }
            }
        return QUrl();
    }

    // In a Save/Export file dialog, pre-fill the (empty) "File name" text
    // field with the file name of the dialog's selectedFile.  Qt leaves the
    // field blank for files that do not exist on disk yet, which is exactly
    // the case for an export target — so the suggested name is normally not
    // shown.  We only act when the field is still empty, so we never override
    // a name the user has typed.
    static void prefillSaveFileName(QQuickWindow* win)
    {
        if (!win->contentItem())
            return;

        QQuickItem* textField = findItemByObjectName(win->contentItem(), "fileNameTextField");
        if (!textField)
            return;

        const QString existing = textField->property("text").toString();
        if (!existing.isEmpty())
            return; // already has a name — do not touch

        const QUrl selected = selectedFileOf(textField);
        if (selected.isEmpty())
            return;

        const QString name = QFileInfo(selected.toLocalFile()).fileName();
        if (name.isEmpty())
            return;

        textField->setProperty("text", name);
    }
};
