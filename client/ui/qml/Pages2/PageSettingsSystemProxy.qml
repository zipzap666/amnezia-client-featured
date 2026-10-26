import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Style 1.0
import "./"
import "../Controls2"
import "../Controls2/TextTypes"
import "../Config"

PageType {
    id: root
    function save() {
        if (!httpPort.textField.acceptableInput || !socksPort.textField.acceptableInput
            || !ConnectionController.saveProxySettings(automatic.checked, Number(httpPort.textField.text), Number(socksPort.textField.text))) {
            errorLabel.text = qsTr("Enter different ports between 1 and 65535")
            return
        }
        errorLabel.text = ""
    }
    BackButtonType {
        id: back
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: 20 + PageController.safeAreaTopMargin
    }
    Flickable {
        objectName: "proxySettingsScroll"
        anchors.top: back.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        contentHeight: content.implicitHeight + 24
        clip: true
        ScrollBar.vertical: ScrollBar {}
        ColumnLayout {
            id: content
            x: 16
            width: parent.width - 32
            spacing: 16
            BaseHeaderType { Layout.fillWidth: true; headerText: qsTr("System proxy") }
            SwitcherType {
                id: automatic
                objectName: "proxyAutomatic"
                Layout.fillWidth: true
                text: qsTr("Configure Windows system proxy")
                descriptionText: qsTr("When disabled, configure applications manually using the addresses below")
                checked: ConnectionController.proxySettings.systemProxy
                onToggled: root.save()
            }
            TextFieldWithHeaderType {
                id: httpPort
                objectName: "proxyHttpPort"
                Layout.fillWidth: true
                headerText: qsTr("HTTP / HTTPS port")
                textField.text: String(ConnectionController.proxySettings.httpPort)
                textField.validator: IntValidator { bottom: 1; top: 65535 }
                textField.onEditingFinished: root.save()
            }
            TextFieldWithHeaderType {
                id: socksPort
                objectName: "proxySocksPort"
                Layout.fillWidth: true
                headerText: qsTr("SOCKS5 port")
                textField.text: String(ConnectionController.proxySettings.socksPort)
                textField.validator: IntValidator { bottom: 1; top: 65535 }
                textField.onEditingFinished: root.save()
            }
            ParagraphTextType { id: errorLabel; objectName: "proxySettingsError"; Layout.fillWidth: true; visible: text.length > 0 }
            Repeater {
                model: ["http://127.0.0.1:" + ConnectionController.proxySettings.httpPort,
                        "socks5://127.0.0.1:" + ConnectionController.proxySettings.socksPort]
                LabelWithButtonType {
                    required property string modelData
                    Layout.fillWidth: true
                    text: modelData
                    rightImageSource: "qrc:/images/controls/copy.svg"
                    clickedFunction: function() { GC.copyToClipBoard(modelData); PageController.showNotificationMessage(qsTr("Copied")) }
                }
            }
            ParagraphTextType { Layout.fillWidth: true; text: ConnectionController.proxyStatus }
            ParagraphTextType {
                Layout.fillWidth: true
                text: qsTr("Only applications using the Windows proxy settings are routed through the proxy. UDP and WebRTC may bypass it. Site rules apply inside the proxy; application rules apply only in VPN mode.")
            }
            BasicButtonType {
                objectName: "proxySettingsReconnect"
                Layout.fillWidth: true
                visible: ConnectionController.proxyMode && ConnectionController.isConnected && ConnectionController.hasPendingSplitTunnelChanges
                text: qsTr("Reconnect to apply")
                clickedFunc: function() { ConnectionController.reconnectWithSavedRules() }
            }
            BasicButtonType {
                objectName: "proxySettingsRestore"
                Layout.fillWidth: true
                visible: ConnectionController.proxyRecoveryPending || (ConnectionController.proxyMode && (ConnectionController.isConnected || ConnectionController.isConnectionInProgress))
                text: qsTr("Disconnect and restore Windows settings")
                clickedFunc: function() { ConnectionController.closeConnection() }
            }
            BasicButtonType {
                Layout.fillWidth: true
                text: qsTr("Reset proxy settings")
                clickedFunc: function() {
                    showQuestionDrawer(qsTr("Reset proxy settings?"), "", qsTr("Reset"), qsTr("Cancel"), function() {
                        ConnectionController.saveProxySettings(true, 8080, 1080)
                        automatic.checked = true
                        httpPort.textField.text = "8080"
                        socksPort.textField.text = "1080"
                        errorLabel.text = ""
                    }, function() {})
                }
            }
        }
    }
}
