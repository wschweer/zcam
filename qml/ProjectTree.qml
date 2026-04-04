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

import QtCore
import QtQuick
import QtQuick3D
import QtQuick.Controls.Material
import ZCam

//*******************************************************
//      Project Tree
//*******************************************************

Node {
    id: base

    Component.onCompleted: {
        console.log("===on completed, root:=="+ZCam.rootElement);
        if (ZCam.rootElement) {
            handleRootElementChanged(ZCam.rootElement);
            }
        }

    function handleRootElementChanged(e) {
        console.log("====root element changed ==="+e);
        // destroy old tree
        var n = base.children.length;
        for (var i = 0; i < n; ++i) {
            base.children[i].destroy(100);
            }
        if (e)
            base.addElement(base, e);    // add Shape component
        }

    Connections {
        id: connections
        target: ZCam

        function onRemove3dElement(e) {
            base.removeElement(base, e);
            }
        function onAdd3dElement(e) {
            if (!e.parent) {
                console.log("onAdd3eElement: no parent");
                return;
                }
            var be = base.searchBase(base, e.parent());
            if (be) {
                base.addElement(be, e);
                } else
                console.log("onAdd3dElement: searchBase: for <" + e.parent().name + "> not found");
            }
        function onAddSubElement(e, parent) {
            //                        console.log("onAddSubElement <"+e.name+"> base <"+parent.name+">");
            var be = base.searchBase(base, parent);
            if (be) {
                // base.addSubElement(be, e)
                base.addElement(be, e);
                } else
                console.log("onAddSubElement: searchBase: for <" + parent.name + "> not found");
            }

        //***********************************************
        //      onRootElementChanged
        //***********************************************

        function onRootElementChanged() {
            base.handleRootElementChanged(ZCam.rootElement);
            }
        function onStartDragElement(e) {
            if (!e)
                mouseArea.curNode = e;
            else {
                var m = base.searchBase(base, e);
                if (m) {
                    //                                console.log("on start drag node "+ m);
                    mouseArea.curNode = m;
                    }
                }
            }
        }

    // search Model for Element3d
    function searchBase(parent, element) {
        var n = parent.children.length;
        for (var i = 0; i < n; ++i) {
            var child = parent.children[i];
            if (!child.element)
                continue;
            if (child.element === element)
                return child;
            var e = searchBase(parent.children[i], element);
            if (e)
                return e;
            }
        return null;
        }

    function removeElement(parent, element) {
        var n = parent.children.length;
        for (var i = 0; i < n; ++i) {
            if (parent.children[i].element === element) {
                parent.children[i].destroy();
                return true;
                }
            if (removeElement(parent.children[i], element))
                return true;
            }
        return false;
        }

    //---------------------------------------------------------
    //   addElement
    //---------------------------------------------------------

    function addElement(parent, element) {
//        console.log("addElement: parent =="+parent+"===element=="+element+"=="+element.model+"==");
        element.update();    // ?!
        var shapeComponent = Qt.createComponent(element.model);
        if (shapeComponent.status === Component.Ready) {
            var instance = shapeComponent.createObject(parent, {
                position: element.pos
                });
            instance.element = element;
            if (element.geometry)
                instance.geometry = Qt.binding(function () {
                    return element.geometry;
                    });
            if (element.constantSize)
                instance.scale = Qt.binding(function () {
                    return Qt.vector3d(10. / root.scale.x, 10. / root.scale.y, 10. / root.scale.z);
                    });
            else {
                instance.scale = Qt.binding(function () {
                    return Qt.vector3d(element.scale.x * (element.mirrorX ? -1.0 : 1.0), element.scale.y * (element.mirrorY ? -1.0 : 1.0), element.scale.z);
                    });
                }
            instance.position = Qt.binding(function () {
                return element.pos;
                });
            instance.eulerRotation = Qt.binding(function () {
                return element.rot;
                });
            instance.visible = Qt.binding(function () {
                return element.active;
                });
            if (element.curColor)
                instance.color = Qt.binding(function () {
                    return element.curColor;
                    });
            var n = element.children.length;
            for (var i = 0; i < n; ++i)
                addElement(instance, element.children[i]);
            }
        else if (shapeComponent.status === Component.Error) {
            console.log("shape error =="+shapeComponent.errorString()+"===");
            }
        }
    }
