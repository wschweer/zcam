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

    InspectorModel {
        id: inspectorModel
        element: ZCam.currentElement
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 6

        // ── Title ─────────────────────────────────────────────────────────────
        Label {
            text: inspectorModel.title.length > 0 ? inspectorModel.title : "No Selection"
            font.bold: true
            Layout.alignment: Qt.AlignHCenter
            Layout.bottomMargin: 2
            elide: Text.ElideRight
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            color: Material.accentColor
        }

        // ── Thin accent divider ───────────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: Material.accentColor
            opacity: 0.4
            Layout.bottomMargin: 2
        }

        // ── Property list ─────────────────────────────────────────────────────
        ListView {
            id: listView
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: inspectorModel
            clip: true
            spacing: 2

            delegate: RowLayout {
                id: delegateRoot
                width: ListView.view.width
                spacing: 6

                required property var model
                required property int index

                // ── Property label ────────────────────────────────────────────
                Label {
                    text: delegateRoot.model.propLabel ?? ""
                    Layout.preferredWidth: 80
                    elide: Text.ElideRight
                    color: Material.foreground
                    opacity: 0.75
                }

                // ── bool: CheckBox ────────────────────────────────────────────
                CheckBox {
                    visible: delegateRoot.model.propType === "bool"
                    Layout.fillWidth: true
                    checked: delegateRoot.model.propValue === true
                    onToggled: delegateRoot.model.propValue = checked
                }

                // ── int: SpinBox ──────────────────────────────────────────────
                SpinBox {
                    visible: delegateRoot.model.propType === "int"
                    Layout.fillWidth: true
                    editable: true
                    from:  delegateRoot.model.propMin !== undefined ? Math.round(delegateRoot.model.propMin) : -1000000
                    to:    delegateRoot.model.propMax !== undefined ? Math.round(delegateRoot.model.propMax) : 1000000
                    value: (delegateRoot.model.propType === "int" && delegateRoot.model.propValue !== undefined) ? Number(delegateRoot.model.propValue) : 0
                    onValueModified: delegateRoot.model.propValue = value
                }

                // ── float: SpinBox ──────────────────────────────────────────────
                DoubleSpinBox {
                    visible: delegateRoot.model.propType === "float"
                    Layout.fillWidth: true
                    editable: true
                    from:  delegateRoot.model.propMin !== undefined ? Math.round(delegateRoot.model.propMin) : -1000000.0
                    to:    delegateRoot.model.propMax !== undefined ? Math.round(delegateRoot.model.propMax) : 1000000.0
                    value: (delegateRoot.model.propType === "float" && delegateRoot.model.propValue !== undefined) ? Number(delegateRoot.model.propValue) : 0.0
                    onValueModified: delegateRoot.model.propValue = value
                }

                // ── float: TextField with validator ───────────────────────────
/*
                TextField {
                    visible: delegateRoot.model.propType === "float"
                    Layout.fillWidth: true
                    text: delegateRoot.model.propValue !== undefined
                          ? Number(delegateRoot.model.propValue).toLocaleString(Qt.locale("C"), 'f', 4)
                          : "0"
                    validator: DoubleValidator {
                        bottom: delegateRoot.model.propMin !== undefined ? delegateRoot.model.propMin : -1e9
                        top:    delegateRoot.model.propMax !== undefined ? delegateRoot.model.propMax : 1e9
                        notation: DoubleValidator.StandardNotation
                        locale: "C"
                    }
                    onEditingFinished: {
                        var num = parseFloat(text)
                        if (!isNaN(num))
                            delegateRoot.model.propValue = num
                    }
                }
*/
                // ── font: family display + picker button ──────────────────────
                FontFamilyButton {
                    visible: delegateRoot.model.propType === "font"
                    Layout.fillWidth: true
                    family: (delegateRoot.model.propType === "font" &&
                             delegateRoot.model.propValue !== undefined)
                            ? delegateRoot.model.propValue : ""
                    onFamilySelected: (fam) => {
                        delegateRoot.model.propValue = fam
                    }
                }

                // ── multiline: ScrollView + TextArea ──────────────────────────
                ScrollView {
                    visible: delegateRoot.model.propType === "multiline"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 80
                    clip: true

                    TextArea {
                        text: delegateRoot.model.propValue !== undefined
                              ? delegateRoot.model.propValue : ""
                        wrapMode: TextEdit.Wrap
                        onEditingFinished: delegateRoot.model.propValue = text
                        onActiveFocusChanged: {
                            if (!activeFocus)
                                delegateRoot.model.propValue = text
                        }
                    }
                }

                // ── string (default): single-line TextField ───────────────────
                TextField {
                    visible: delegateRoot.model.propType !== "bool"
                          && delegateRoot.model.propType !== "int"
                          && delegateRoot.model.propType !== "float"
                          && delegateRoot.model.propType !== "font"
                          && delegateRoot.model.propType !== "multiline"
                    Layout.fillWidth: true
                    text: delegateRoot.model.propValue !== undefined
                          ? delegateRoot.model.propValue : ""
                    onEditingFinished: delegateRoot.model.propValue = text
                }

                // ── Unit label (optional) ─────────────────────────────────────
                Label {
                    visible: (delegateRoot.model.propUnit ?? "").length > 0
                    text: delegateRoot.model.propUnit ?? ""
                    Layout.preferredWidth: 28
                    color: Material.accentColor
                    opacity: 0.85
                }
            }
        }
    }
}
