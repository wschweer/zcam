import QtQuick
import QtQuick.Layouts

Item {
    default property alias content: internalLayout.data
    width:  internalLayout.width
    height: internalLayout.height

    Rectangle {
        anchors.fill: parent
        color: "#30000000"
        radius: 10
        }

    ColumnLayout {
        id: internalLayout
        }
    }
