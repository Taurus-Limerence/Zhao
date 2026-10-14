import QtQuick
import QtTest
import "../../qml/settings"

TestCase {
    name: "MountHome"
    when: windowShown
    width: 800; height: 1000
    visible: true

    Component { id: pageComponent; MountSettings { width: 780; height: 980 } }
    Component { id: plannerComponent; QtObject { property double longitude: 114.165; property double latitude: 22.2753 } }
    function test_localEditingAndExplicitSync() {
        const manager = createTemporaryObject(managerComponent, this)
        manager.devices = [{role: 0, selectedId: "ASCOM.TMount.Mount", connected: true, metrics: {slewing: false}}]
        const planner = createTemporaryObject(plannerComponent, this)
        const page = createTemporaryObject(pageComponent, this, {deviceManager: manager, planner: planner})
        const rows = page.sections.filter(section => section.title.indexOf("本地信息") === 0)[0].rows
        rows[0].onEdited("-73.1234")
        compare(planner.longitude, -73.1234)
        compare(manager.calls.length, 0)
        page.syncObserver(false)
        compare(manager.calls[0][0], false)
        compare(manager.calls[0][1], -73.1234)
        page.syncObserver(true)
        compare(manager.calls[1][0], true)
        manager.mountGuiding = true
        page.syncObserver(false)
        compare(manager.calls.length, 2)
    }
    function test_coordinateFormatting() {
        const page = createTemporaryObject(pageComponent, this)
        compare(page.hoursText(23.999999), "00h 00m 00s")
        compare(page.coordinateText(114.165, true), "E 114° 09′ 54″")
        compare(page.coordinateText(-22.2753, false), "S 22° 16′ 31″")
        compare(page.coordinateText(undefined, false), "—")
    }
    function test_horizontalReadoutUsesDms() {
        const manager = createTemporaryObject(managerComponent, this)
        manager.devices = [{role:0, connected:true, metrics:{altitude:21+34/60+2/3600, azimuth:53+37/60+4/3600}}]
        const page = createTemporaryObject(pageComponent, this, {deviceManager:manager})
        function value(label) {
            return page.sections.filter(s => s.title.indexOf("赤道仪信息") === 0)[0].rows.filter(r => r.label === label)[0].value
        }
        compare(value("高度角"), "+21° 34′ 02″")
        compare(value("方位角"), "053° 37′ 04″")
        manager.devices = [{role:0, connected:true, metrics:{altitude:-0.5, azimuth:359.999999}}]
        compare(value("高度角"), "-00° 30′ 00″")
        compare(value("方位角"), "000° 00′ 00″")
        manager.devices = [{role:0, connected:true, metrics:{altitude:null, azimuth:NaN}}]
        compare(value("高度角"), "—")
        compare(value("方位角"), "—")
    }
    Component {
        id: managerComponent
        QtObject {
            property var devices: [{role: 0, selectedId: "ASCOM.TMount.Mount", connected: true,
                metrics: {canFindHome: true, canPark: true, canSetPark: true, canUnpark: true}}]
            property var calls: []
            property bool mountGuiding: false
            property bool mountManualMoving: false
            function syncMountObserver(timeOnly, lon, lat) { calls = calls.concat([[timeOnly, lon, lat]]); return true }
            function setMountGuideRates(ra, dec) { calls = calls.concat([[ra, dec]]); return true }
            function mountFindHome() { calls = calls.concat(["FindHome"]) }
            function mountSetCurrentPark() { calls = calls.concat(["SetPark"]) }
            function mountPark() { calls = calls.concat(["Park"]) }
            function mountUnpark() { calls = calls.concat(["Unpark"]) }
        }
    }

    function test_singleGuideRateUsesDriverReadback() {
        const manager = createTemporaryObject(managerComponent, this)
        manager.devices = [{role: 0, selectedId: "ASCOM.TMount.Mount", connected: true,
            metrics: {canSetGuideRates: true, guideRateRa: 0.5, guideRateDec: 0.5}}]
        const page = createTemporaryObject(pageComponent, this, {deviceManager: manager})
        function rateRow() {
            const rows = page.sections.reduce((all, section) => all.concat(section.rows), [])
            const rates = rows.filter(row => row.label.indexOf("导星速率") === 0)
            compare(rates.length, 1)
            return rates[0]
        }
        compare(rateRow().currentIndex, 4)
        rateRow().onActivated(6)
        compare(manager.calls.length, 1)
        fuzzyCompare(manager.calls[0][0], 0.7, 1e-12)
        fuzzyCompare(manager.calls[0][1], 0.7, 1e-12)
        // A command is not a measurement: retain the last driver reading.
        compare(rateRow().currentIndex, 4)
        manager.devices = [{role: 0, selectedId: "ASCOM.TMount.Mount", connected: true,
            metrics: {canSetGuideRates: true, guideRateRa: 0.3, guideRateDec: 0.3}}]
        compare(rateRow().currentIndex, 2)
        compare(manager.calls.length, 1)
        manager.devices = [{role: 0, connected: false, metrics: {}}]
        verify(!rateRow().enabled)
        compare(rateRow().currentIndex, -1)
    }

    function test_openGuideRateSurvivesPolling() {
        const manager = createTemporaryObject(managerComponent, this)
        manager.devices = [{role: 0, selectedId: "ASCOM.TMount.Mount", connected: true,
            metrics: {canSetGuideRates: true, guideRateRa: 0.5, ra: 1}}]
        const page = createTemporaryObject(pageComponent, this, {deviceManager: manager})
        function findRate(item) {
            if (item.rowData && item.rowData.label === "导星速率（倍恒星速）")
                return findChild(item, "settingRowEditor").item
            for (let i = 0; i < item.children.length; ++i) {
                const result = findRate(item.children[i])
                if (result) return result
            }
            return null
        }
        const combo = findRate(page)
        verify(combo !== null)
        combo.popup.open()
        tryCompare(combo.popup, "opened", true)
        for (let i = 0; i < 3; ++i) {
            manager.devices = [{role: 0, selectedId: "ASCOM.TMount.Mount", connected: true,
                metrics: {canSetGuideRates: true, guideRateRa: 0.5, ra: 2 + i}}]
            wait(20)
            verify(findRate(page) === combo, "Polling must preserve the dropdown instance")
            verify(combo.popup.opened, "Polling must not dismiss the menu")
        }
        combo.popup.close()
        manager.devices = [{role: 0, selectedId: "ASCOM.TMount.Mount", connected: true,
            metrics: {canSetGuideRates: true, guideRateRa: 0.7, ra: 5}}]
        tryCompare(combo, "currentIndex", 6)
        compare(manager.calls.length, 0)
    }

    function test_homeAndParkRemainDistinct() {
        const manager = createTemporaryObject(managerComponent, this)
        const page = createTemporaryObject(pageComponent, this, {deviceManager: manager})
        const goHome = findChild(page, "mountGoHome")
        verify(goHome !== null, "Return-home must have its own button")
        verify(goHome.enabled)
        verify(!findChild(page, "mountSearchHome").enabled)
        verify(!findChild(page, "mountSetHome").enabled)
        goHome.clicked()
        compare(manager.calls.join(","), "FindHome")
        findChild(page, "mountSetPark").clicked()
        findChild(page, "mountPark").clicked()
        compare(manager.calls.join(","), "FindHome,SetPark,Park")
        manager.devices = [{role: 0, selectedId: "ASCOM.TMount.Mount", connected: true,
            metrics: {atPark: true, canUnpark: true, canFindHome: true, canSetPark: true}}]
        wait(0)
        verify(!findChild(page, "mountGoHome").enabled)
        verify(!findChild(page, "mountSetPark").enabled)
        verify(findChild(page, "mountPark").enabled)
        findChild(page, "mountPark").clicked()
        compare(manager.calls.join(","), "FindHome,SetPark,Park,Unpark")
        manager.devices = [{role: 0, selectedId: "ASCOM.TMount.Mount", connected: true,
            metrics: {slewing: true, canFindHome: true, canSetPark: true, canPark: true}}]
        wait(0)
        verify(!findChild(page, "mountGoHome").enabled)
        verify(!findChild(page, "mountSetPark").enabled)
        verify(!findChild(page, "mountPark").enabled)
        manager.devices = [{role: 0, selectedId: "ASCOM.Other.Mount", connected: true,
            metrics: {canFindHome: true}}]
        wait(0)
        verify(!findChild(page, "mountGoHome").enabled, "Do not assume another driver's FindHome semantics")
        manager.devices = [{role: 0, selectedId: "ASCOM.TMount.Mount", connected: false, metrics: {}}]
        wait(0)
        verify(!findChild(page, "mountGoHome").enabled)
        verify(!findChild(page, "mountSetPark").enabled)
        verify(!findChild(page, "mountPark").enabled)
    }
}
