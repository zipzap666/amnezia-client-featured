import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Style 1.0
import "../Controls2"
import "../Controls2/TextTypes"
import "../Components"

PageType {
    id: root
    property var statistics: TrafficStatistics
    property int period: 60

    function bytes(value) {
        if (value >= 1073741824) return (value / 1073741824).toFixed(2) + qsTr(" GiB")
        if (value >= 1048576) return (value / 1048576).toFixed(2) + qsTr(" MiB")
        if (value >= 1024) return (value / 1024).toFixed(2) + qsTr(" KiB")
        return value.toFixed(0) + qsTr(" B")
    }
    function rate(value) { return bytes(value) + qsTr("/s") }

    BackButtonType {
        id: back
        objectName: "statisticsBackButton"
        visible: !root.stackView || root.stackView.depth > 1
        height: visible ? implicitHeight : 0
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: visible ? 20 + PageController.safeAreaTopMargin : 0
        backButtonFunction: function() {
            if (root.stackView && root.stackView.depth <= 1)
                PageController.goToPageHome()
            else
                PageController.closePage()
        }
    }
    Flickable {
        id: scroll
        anchors.top: back.bottom
        anchors.topMargin: back.visible ? 0 : 24 + PageController.safeAreaTopMargin
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        contentWidth: width
        contentHeight: content.implicitHeight + 32
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}
        ColumnLayout {
            id: content
            x: 16
            width: scroll.width - 32
            spacing: 16
            BaseHeaderType {
                Layout.fillWidth: true
                headerText: qsTr("Statistics")
                descriptionText: qsTr("VPN state: ") + (ConnectionController.isConnected || ConnectionController.isConnectionInProgress
                                 ? ConnectionController.connectionStateText : qsTr("Disconnected"))
            }
            RowLayout {
                Layout.fillWidth: true
                BasicButtonType {
                    Layout.fillWidth: true
                    text: qsTr("60 seconds")
                    opacity: root.period === 60 ? 1 : 0.55
                    clickedFunc: function() { root.period = 60 }
                }
                BasicButtonType {
                    Layout.fillWidth: true
                    text: qsTr("5 minutes")
                    opacity: root.period === 300 ? 1 : 0.55
                    clickedFunc: function() { root.period = 300 }
                }
            }
            Repeater {
                model: 2
                delegate: ColumnLayout {
                    id: card
                    required property int index
                    readonly property var counters: index === 0 ? root.statistics.vpn : root.statistics.physical
                    Layout.fillWidth: true
                    spacing: 8
                    BaseHeaderType {
                        Layout.fillWidth: true
                        headerText: card.index === 0 ? qsTr("Through VPN") : qsTr("All adapter traffic")
                        headerTextMaximumLineCount: 4
                        headerTextElide: Text.ElideNone
                        descriptionText: card.index === 1 ? qsTr("Includes VPN and local network traffic") : ""
                    }
                    ParagraphTextType {
                        Layout.fillWidth: true
                        text: card.counters.available ? qsTr("Receiving data") : qsTr("Data unavailable / waiting for counters")
                    }
                    ParagraphTextType {
                        Layout.fillWidth: true
                        color: AmneziaStyle.color.goldenApricot
                        text: qsTr("IN · Receive: ") + (card.counters.available ? root.rate(card.counters.rx) : "—")
                              + "\n" + qsTr("Received: ") + (card.counters.observed ? root.bytes(card.counters.received) : "—")
                    }
                    ParagraphTextType {
                        Layout.fillWidth: true
                        color: AmneziaStyle.color.softViolet
                        text: qsTr("OUT · Send: ") + (card.counters.available ? root.rate(card.counters.tx) : "—")
                              + "\n" + qsTr("Sent: ") + (card.counters.observed ? root.bytes(card.counters.sent) : "—")
                    }
                    TrafficChart {
                        Layout.fillWidth: true
                        samples: card.counters.history
                        now: root.statistics.now
                        seconds: root.period
                        formatRate: root.rate
                    }
                    DividerType { Layout.fillWidth: true }
                }
            }
            Button {
                id: measurementInfoButton
                objectName: "measurementInfoButton"
                Layout.fillWidth: true
                implicitHeight: Math.max(40, infoLabel.implicitHeight + 16)
                checkable: true
                property bool isFocusable: true
                text: qsTr("How traffic is measured")
                Accessible.name: text
                contentItem: ParagraphTextType {
                    id: infoLabel
                    text: measurementInfoButton.text
                    font.pixelSize: 14
                    color: measurementInfoButton.hovered || measurementInfoButton.checked
                           ? AmneziaStyle.color.paleGray : AmneziaStyle.color.mutedGray
                }
                background: Rectangle {
                    color: measurementInfoButton.hovered ? AmneziaStyle.color.onyxBlack : "transparent"
                    radius: 8
                    border.width: measurementInfoButton.activeFocus ? 1 : 0
                    border.color: AmneziaStyle.color.mutedGray
                }
            }
            ColumnLayout {
                objectName: "measurementInfoPanel"
                Layout.fillWidth: true
                visible: measurementInfoButton.checked
                spacing: 12
                ParagraphTextType {
                    Layout.fillWidth: true
                    font.pixelSize: 14
                    color: AmneziaStyle.color.mutedGray
                    text: qsTr("Outside VPN: unavailable. Physical counters include encrypted VPN traffic, overhead and LAN traffic. Subtracting tunnel counters cannot reliably measure bypass traffic, including with split tunneling.")
                }
                ParagraphTextType {
                    Layout.fillWidth: true
                    font.pixelSize: 14
                    color: AmneziaStyle.color.mutedGray
                    text: qsTr("VPN: OpenVPN management counters or WireGuard/AmneziaWG peer counters. Other protocols: unavailable. Physical: active hardware adapters only; virtual adapters excluded. These graphs measure different layers and must not be added together.")
                }
                ParagraphTextType {
                    Layout.fillWidth: true
                    font.pixelSize: 14
                    color: AmneziaStyle.color.mutedGray
                    text: qsTr("Volumes count observed intervals since statistics reset or application start. VPN volumes restart on connection state changes. Missing intervals are excluded; gaps indicate unavailable data. History stays in memory for up to 5 minutes.")
                }
            }
            BasicButtonType {
                Layout.fillWidth: true
                text: qsTr("Reset statistics")
                clickedFunc: function() { root.statistics.reset() }
            }
        }
    }
}
