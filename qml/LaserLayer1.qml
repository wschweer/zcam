//=============================================================================
//  ZCam
//
//  Copyright (C) 2026 Werner Schweer
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

import QtQuick
import QtQuick3D
import ZCam

// LaserLayer is no longer rendered directly in the 3D canvas.
// Display geometry is collected and rendered by Cam.
// This component serves as an invisible container in the scene graph
// so that child elements (if any) can still be positioned relative to it.
Node {
    id: model
    property Element element
    visible: element ? (element.show && element.ancestorsShow) : false
}