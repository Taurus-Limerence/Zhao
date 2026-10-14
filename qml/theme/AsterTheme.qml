pragma Singleton
import QtQuick

QtObject {
    readonly property color canvas: "#090e12"
    readonly property color canvasRaised: "#0d1419"
    readonly property color surface: "#11181d"
    readonly property color surfaceRaised: "#151e24"
    readonly property color surfaceHover: "#1a252d"
    readonly property color stroke: "#2d3b45"
    readonly property color strokeStrong: "#425462"
    readonly property color textPrimary: "#d8d8dc"
    readonly property color textSecondary: "#a3a3a8"
    readonly property color textMuted: "#818b94"
    readonly property color textDisabled: "#505158"
    readonly property color textSelected: "#49a7f2"
    readonly property color deviceTitleText: textPrimary
    readonly property color deviceBodyText: textPrimary
    readonly property color deviceMutedText: textSecondary
    readonly property color deviceDisabledText: textMuted
    readonly property color deviceSelectedText: textSelected
    readonly property color accent: "#2f9def"
    readonly property color accentPressed: "#1d83ce"
    readonly property color accentSurface: "#173047"
    readonly property color danger: "#d27b83"
    readonly property color dangerSurface: "#c42b1c"
    readonly property color dangerPressed: "#a4262c"
    readonly property color warning: "#d3a85b"

    readonly property color statusOk: "#4e9a6f"
    readonly property color imageCanvas: "#000000"
    readonly property color scrim: "#88000000"
    readonly property color crosshair: "#60818b94"
    readonly property color accentText: "#090e12"

    readonly property int radiusSmall: 3
    readonly property int radiusMedium: 4
    readonly property int radiusLarge: 6
    readonly property int space1: 4
    readonly property int space2: 8
    readonly property int space3: 12
    readonly property int space4: 16
    readonly property int space5: 24
    readonly property int space6: 32
    readonly property string uiFont: "Segoe UI"
    readonly property string monoFont: "Cascadia Mono"
}
