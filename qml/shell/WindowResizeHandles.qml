pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: root
    property var hostWindow
    readonly property int handleCount: 8
    readonly property int edgeSize: 5
    readonly property int cornerSize: 8

    component ResizeHandle: MouseArea {
        property int resizeEdges
        acceptedButtons: Qt.LeftButton
        onPressed: {
            if (root.hostWindow)
                root.hostWindow.startSystemResize(resizeEdges)
        }
    }

    ResizeHandle {
        x: root.cornerSize
        width: root.width - 2 * root.cornerSize
        height: root.edgeSize
        resizeEdges: Qt.TopEdge
        cursorShape: Qt.SizeVerCursor
    }
    ResizeHandle {
        x: root.cornerSize
        y: root.height - root.edgeSize
        width: root.width - 2 * root.cornerSize
        height: root.edgeSize
        resizeEdges: Qt.BottomEdge
        cursorShape: Qt.SizeVerCursor
    }
    ResizeHandle {
        y: root.cornerSize
        width: root.edgeSize
        height: root.height - 2 * root.cornerSize
        resizeEdges: Qt.LeftEdge
        cursorShape: Qt.SizeHorCursor
    }
    ResizeHandle {
        x: root.width - root.edgeSize
        y: root.cornerSize
        width: root.edgeSize
        height: root.height - 2 * root.cornerSize
        resizeEdges: Qt.RightEdge
        cursorShape: Qt.SizeHorCursor
    }
    ResizeHandle {
        width: root.cornerSize
        height: root.cornerSize
        resizeEdges: Qt.TopEdge | Qt.LeftEdge
        cursorShape: Qt.SizeFDiagCursor
    }
    ResizeHandle {
        x: root.width - root.cornerSize
        width: root.cornerSize
        height: root.cornerSize
        resizeEdges: Qt.TopEdge | Qt.RightEdge
        cursorShape: Qt.SizeBDiagCursor
    }
    ResizeHandle {
        y: root.height - root.cornerSize
        width: root.cornerSize
        height: root.cornerSize
        resizeEdges: Qt.BottomEdge | Qt.LeftEdge
        cursorShape: Qt.SizeBDiagCursor
    }
    ResizeHandle {
        x: root.width - root.cornerSize
        y: root.height - root.cornerSize
        width: root.cornerSize
        height: root.cornerSize
        resizeEdges: Qt.BottomEdge | Qt.RightEdge
        cursorShape: Qt.SizeFDiagCursor
    }
}
