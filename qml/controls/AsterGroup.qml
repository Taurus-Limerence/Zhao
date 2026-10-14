import QtQuick
import QtQuick.Layouts
import "../theme"

Rectangle {
    id: root
    default property alias contentData: body.data
    property alias spacing: body.spacing
    implicitHeight: body.implicitHeight + 2 * AsterTheme.space3
    radius: AsterTheme.radiusMedium
    color: AsterTheme.surface
    border.width: 1
    border.color: AsterTheme.stroke

    ColumnLayout {
        id: body
        anchors.fill: parent
        anchors.margins: AsterTheme.space3
        spacing: AsterTheme.space2
    }
}
