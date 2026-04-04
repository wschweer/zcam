//=============================================================================
//  ZCam
//
//  Copyright (C) 2026 Werner Schweer
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENSE.GPL
//=============================================================================

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import ZCam

Item {
    id: root

    Material.theme: Material.Dark

    TreeView {
        id: treeView
        anchors.fill: parent
        anchors.margins: 4
        model: ZCam.treeModel
        clip: true

        selectionModel: ItemSelectionModel {
            id: selModel
            model: ZCam.treeModel
            }

        // ── Row delegate ──────────────────────────────────────────────────────
        delegate: ItemDelegate {
            id: delegateItem

            // Required TreeView properties
            required property TreeView treeView
            required property bool isTreeNode
            required property bool expanded
            required property int hasChildren
            required property int depth
            required property int row
            required property int column
            required property bool current

            readonly property real levelIndent: 20

            // Full width of the view, Material ripple needs it
            implicitWidth: treeView.width
            implicitHeight: 28

            // Material highlight when this row is selected
            highlighted: current

            // Horizontal padding so text never touches the edge
            leftPadding: 8 + depth * levelIndent
            rightPadding: 8
            topPadding: 2
            bottomPadding: 2

            // Custom content: optional chevron + element name
            contentItem: RowLayout {
                spacing: 4

                // ── Expand / collapse chevron ─────────────────────────────────
                Label {
                    id: indicator
                    visible: delegateItem.isTreeNode && delegateItem.hasChildren
                    text: "▸"
                    // One point smaller than the body font
                    font.pointSize: Math.max(6, Qt.application.font.pointSize - 1)
                    color: delegateItem.current ? Material.primaryHighlightedTextColor : Material.foreground
                    rotation: delegateItem.expanded ? 90 : 0
                    Layout.alignment: Qt.AlignVCenter

                    Behavior on rotation {
                        NumberAnimation {
                            duration: 120
                            easing.type: Easing.OutCubic
                            }
                        }

                    TapHandler {
                        onTapped: {
                            if (delegateItem.expanded)
                                delegateItem.treeView.collapse(delegateItem.row);
                            else
                                delegateItem.treeView.expand(delegateItem.row);
                            }
                        }
                    }

                // Placeholder so non-node leaves align with node labels
                Item {
                    visible: !(delegateItem.isTreeNode && delegateItem.hasChildren)
                    Layout.preferredWidth: indicator.implicitWidth + 4
                    height: 1
                    }

                // ── Element name ──────────────────────────────────────────────
                Label {
                    id: nameLabel
                    text: model.name ?? ""
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    color: delegateItem.current ? Material.primaryHighlightedTextColor : Material.foreground
                    Layout.alignment: Qt.AlignVCenter
                    }
                }

            // Select element on tap (row tap, not the chevron tap)
            onClicked: {
                treeView.selectionModel.setCurrentIndex(treeView.index(row, column), ItemSelectionModel.ClearAndSelect);
                ZCam.currentElement = model.element;
                }
            onDoubleClicked: {
                if (delegateItem.isTreeNode && delegateItem.hasChildren) {
                    if (delegateItem.expanded)
                        delegateItem.treeView.collapse(delegateItem.row);
                    else
                        delegateItem.treeView.expand(delegateItem.row);
                    }
                }
            }
        }
    }
