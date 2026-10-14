import QtQuick
import "../controls"

SettingsPage {
    sections: [
        { title: "设备连接", rows: [
            { label: "设备", type: "select", options: ["G3M678C · USB3 #G078", "SkyEye 24AC · USB3 #C114", "None"], wide: true },
            { label: "连接", type: "toggle", checked: true },
            { label: "导星镜焦距", value: "120 mm" },
            { label: "导入主镜焦距", type: "button", value: "导入 480 mm" }
        ]},
        { title: "曝光参数", rows: [
            { label: "增益等级", type: "segment", options: ["低", "中", "高"], currentIndex: 1 },
            { label: "增益", value: "68" },
            { label: "曝光时间", value: "2.00 s" },
            { label: "BIN", type: "select", options: ["1 × 1", "2 × 2"] },
            { label: "偏移设置", value: "0" }
        ]},
        { title: "导星", rows: [
            { label: "多星导星", type: "toggle", checked: true },
            { label: "导星丢失蜂鸣", type: "toggle", checked: false },
            { label: "RA 修正最大时长", value: "2500 ms" },
            { label: "Dec 修正最大时长", value: "2500 ms" },
            { label: "RA 强度修正系数", value: "65%" },
            { label: "RA 最小移动量", value: "0.15 px" },
            { label: "Dec 强度修正系数", value: "65%" },
            { label: "Dec 最小移动量", value: "0.15 px" },
            { label: "校准步长", value: "1000 ms" }
        ]},
        { title: "抖动", rows: [
            { label: "抖动", type: "toggle", checked: true },
            { label: "抖动范围", value: "5 px" },
            { label: "抖动模式", type: "segment", options: ["随机", "螺旋"] },
            { label: "抖动间隔", value: "每 3 帧" },
            { label: "稳定系数", type: "select", options: ["0.6 px", "1.0 px"] },
            { label: "稳定时间", type: "select", options: ["10 s", "15 s"] },
            { label: "稳定超时", value: "60 s" }
        ]},
        { title: "", rows: [{ label: "高级设置", type: "button", value: "打开 ›" }] }
    ]
}
