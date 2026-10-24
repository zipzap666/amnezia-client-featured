import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

import QtCore

import SortFilterProxyModel 0.2

import PageEnum 1.0
import ContainerProps 1.0
import Style 1.0

import "./"
import "../Controls2"
import "../Controls2/TextTypes"
import "../Config"
import "../Components"

PageType {
    id: root

    readonly property bool pageEnabled: true

    QtObject {
        id: routeMode
        property int allApps: 0
        property int onlyForwardApps: 1
        property int allExceptApps: 2
    }

    property list<QtObject> routeModesModel: [
        onlyForwardApps,
        allExceptApps
    ]

    QtObject {
        id: onlyForwardApps

        readonly property string name: qsTr("Only the apps from the list should have access via VPN")
        readonly property int type: routeMode.onlyForwardApps
    }

    QtObject {
        id: allExceptApps
        
        readonly property string name: qsTr("Apps from the list should not have access via VPN")
        readonly property int type: routeMode.allExceptApps
    }

    function getRouteModesModelIndex() {
        var currentRouteMode = AppSplitTunnelingController.routeMode
        if ((routeMode.onlyForwardApps === currentRouteMode) || (routeMode.allApps === currentRouteMode)) {
            return 0
        } else if (routeMode.allExceptApps === currentRouteMode) {
            return 1
        }
    }

    ColumnLayout {
        id: header

        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right

        anchors.topMargin: 20 + PageController.safeAreaTopMargin

        BackButtonType {
            id: backButton
        }

        HeaderTypeWithSwitcher {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16

            headerText: qsTr("App split tunneling")

            enabled: root.pageEnabled
            showSwitcher: true
            switcher {
                checked: AppSplitTunnelingController.isSplitTunnelingEnabled
                enabled: root.pageEnabled
            }
            switcherFunction: function(checked) {
                AppSplitTunnelingController.toggleSplitTunneling(checked)
                selector.text = root.routeModesModel[getRouteModesModelIndex()].name
            }
        }

        DropDownType {
            id: selector
            visible: Qt.platform.os !== "windows"

            Layout.fillWidth: true
            Layout.topMargin: 32
            Layout.leftMargin: 16
            Layout.rightMargin: 16

            drawerHeight: 0.4375
            drawerParent: root

            headerText: qsTr("Mode")

            enabled: (Qt.platform.os === "android") && root.pageEnabled

            listView: ListViewWithRadioButtonType {
                rootWidth: root.width

                model: root.routeModesModel

                selectedIndex: getRouteModesModelIndex()

                clickedFunction: function() {
                    selector.text = selectedText
                    selector.closeTriggered()
                    if (AppSplitTunnelingController.routeMode !== root.routeModesModel[selectedIndex].type) {
                        AppSplitTunnelingController.routeMode = root.routeModesModel[selectedIndex].type
                    }
                }

                Component.onCompleted: {
                    if (root.routeModesModel[selectedIndex].type === AppSplitTunnelingController.routeMode) {
                        selector.text = selectedText
                    } else {
                        selector.text = root.routeModesModel[0].name
                    }
                }

                Connections {
                    target: AppSplitTunnelingController
                    function onRouteModeChanged() {
                        selectedIndex = getRouteModesModelIndex()
                    }
                }
            }
        }

        WarningType {
            Layout.fillWidth: true
            Layout.topMargin: 8
            Layout.leftMargin: 16
            Layout.rightMargin: 16

            textString: qsTr("Only <b>\"Apps from the list should not have access via VPN\"</b> mode is available on Windows")
            textFormat: Text.RichText
            iconPath: "qrc:/images/controls/alert-circle.svg"

            visible: (Qt.platform.os === "windows") && root.pageEnabled
        }
        BasicButtonType {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.topMargin: 12
            visible: Qt.platform.os === "windows"
            text: qsTranslate("SplitTunnelTree", "Create group")
            enabled: root.pageEnabled
            clickedFunc: function() { ruleTree.createGroup() }
        }
    }

    ListViewType {
        id: listView
        visible: Qt.platform.os !== "windows"

        ScrollBar.vertical: ScrollBarType { policy: ScrollBar.AlwaysOn }

        anchors.top: header.bottom
        anchors.bottom: parent.bottom
        anchors.bottomMargin: addAppButton.implicitHeight + 48 + PageController.safeAreaBottomMargin + (searchField.textField.activeFocus ? 0 : PageController.imeHeight)
        anchors.left: parent.left
        anchors.right: parent.right
        clip: true

        model: SortFilterProxyModel {
            id: proxyAppSplitTunnelingModel
            sourceModel: AppSplitTunnelingModel
            filters: RegExpFilter {
                roleName: "appPath"
                pattern: Qt.platform.os === "windows" ? "" : ".*" + searchField.textField.text + ".*"
                caseSensitivity: Qt.CaseInsensitive
            }
            sorters: [
                RoleSorter { roleName: "appPath"; sortOrder: Qt.AscendingOrder }
            ]
        }

        delegate: ColumnLayout {
            width: listView.width

            LabelWithButtonType {
                Layout.fillWidth: true

                Layout.leftMargin: 16
                Layout.rightMargin: 16

                text: appPath
                descriptionText: Qt.platform.os === "windows" ? ruleKey : ""
                hideDescription: Qt.platform.os !== "windows"
                rightImageSource: "qrc:/images/controls/trash.svg"
                rightImageColor: AmneziaStyle.color.paleGray

                clickedFunction: function() {
                    var headerText = qsTr("Remove ") + appPath + "?"
                    var yesButtonText = qsTr("Continue")
                    var noButtonText = qsTr("Cancel")

                    var yesButtonFunction = function() {
                        AppSplitTunnelingController.removeApp(proxyAppSplitTunnelingModel.mapToSource(index))
                    }
                    var noButtonFunction = function() {
                    }

                    showQuestionDrawer(headerText, "", yesButtonText, noButtonText, yesButtonFunction, noButtonFunction)
                }
            }

            DividerType {}
        }
    }

    SplitTunnelTree {
        id: ruleTree
        anchors.fill: listView
        visible: Qt.platform.os === "windows"
        enabled: root.pageEnabled
        pageRoot: root
        apps: true
        controller: AppSplitTunnelingController.groupsController
        rules: AppSplitTunnelingController.treeRules
        searchText: searchField.textField.text
        onRemoveRuleRequested: function(key, name) {
            showQuestionDrawer(qsTr("Remove ") + name + "?", "", qsTr("Continue"), qsTr("Cancel"),
                               function() { AppSplitTunnelingController.removeRuleByKey(key) }, function() {})
        }
        onRemoveGroupRequested: function(id, name) {
            showQuestionDrawer(qsTranslate("SplitTunnelTree", "Delete group") + " " + name + "?",
                               qsTranslate("SplitTunnelTree", "Rules will be moved to Ungrouped."),
                               qsTr("Continue"), qsTr("Cancel"),
                               function() { controller.removeGroup(id) }, function() {})
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        
        height: addAppButton.implicitHeight + 48 + PageController.safeAreaBottomMargin
        
        color: AmneziaStyle.color.midnightBlack
        
        RowLayout {
            id: addAppButton

            enabled: root.pageEnabled

            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.topMargin: 24
            anchors.rightMargin: 16
            anchors.leftMargin: 16
            anchors.bottomMargin: 24 + PageController.safeAreaBottomMargin

            TextFieldWithHeaderType {
                id: searchField

                Layout.fillWidth: true

                textField.placeholderText: qsTr("application name")
                buttonImageSource: "qrc:/images/controls/plus.svg"

                rightButtonClickedOnEnter: true

                clickedFunc: function() {
                    searchField.focus = false
                    PageController.showBusyIndicator(true)

                    if (Qt.platform.os === "windows") {
                        appSourceDrawer.openTriggered()
                    } else if (Qt.platform.os === "android"){
                        installedAppDrawer.openTriggered()
                    }

                    PageController.showBusyIndicator(false)
                }
            }
        }
    }

    DrawerType2 {
        id: appSourceDrawer
        anchors.fill: parent
        expandedHeight: Math.min(parent.height, 224 + PageController.safeAreaBottomMargin)
        property string selectedSource: ""
        onAboutToShow: selectedSource = ""
        onClosed: {
            var source = selectedSource
            selectedSource = ""
            // Let DrawerType2 restore focus and drawer depth before opening the next UI.
            Qt.callLater(function() {
                if (source === "running") {
                    runningAppsDrawer.openTriggered()
                } else if (source === "file") {
                    var fileName = SystemController.getFileName(qsTr("Open executable file"),
                                                                qsTr("Executable files (*.exe)"))
                    if (fileName !== "")
                        AppSplitTunnelingController.addApp(fileName)
                }
            })
        }
        expandedStateContent: Item {
            implicitHeight: appSourceDrawer.expandedHeight
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                Header2Type {
                    Layout.fillWidth: true
                    headerText: qsTr("Add application")
                }
                LabelWithButtonType {
                    Layout.fillWidth: true
                    text: qsTr("From running applications")
                    rightImageSource: "qrc:/images/controls/chevron-right.svg"
                    clickedFunction: function() {
                        appSourceDrawer.selectedSource = "running"
                        appSourceDrawer.closeTriggered()
                    }
                }
                LabelWithButtonType {
                    Layout.fillWidth: true
                    text: qsTr("Choose executable file")
                    rightImageSource: "qrc:/images/controls/chevron-right.svg"
                    clickedFunction: function() {
                        appSourceDrawer.selectedSource = "file"
                        appSourceDrawer.closeTriggered()
                    }
                }
                Item { Layout.fillHeight: true }
            }
        }
    }

    DrawerType2 {
        id: runningAppsDrawer
        anchors.fill: parent
        expandedHeight: parent.height * 0.9
        property var selectedPaths: ({})
        property string searchText: ""
        readonly property var filteredApps: {
            var query = searchText.toLowerCase()
            return AppSplitTunnelingController.runningApps.filter(function(app) {
                return app.appName.toLowerCase().indexOf(query) !== -1 ||
                       app.appPath.toLowerCase().indexOf(query) !== -1
            })
        }
        function refresh() {
            // A new snapshot starts a new selection; exited rows cannot be submitted.
            selectedPaths = ({})
            AppSplitTunnelingController.refreshRunningApps()
        }
        onAboutToShow: {
            searchText = ""
            refresh()
        }
        expandedStateContent: Item {
            implicitHeight: runningAppsDrawer.expandedHeight
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                BackButtonType {
                    backButtonFunction: function() { runningAppsDrawer.closeTriggered() }
                }
                Header2Type {
                    Layout.fillWidth: true
                    headerText: qsTr("Running applications")
                }
                TextFieldWithHeaderType {
                    Layout.fillWidth: true
                    textField.placeholderText: qsTr("Search by name or path")
                    textField.text: runningAppsDrawer.searchText
                    textField.onTextChanged: runningAppsDrawer.searchText = textField.text
                }
                BasicButtonType {
                    Layout.fillWidth: true
                    text: AppSplitTunnelingController.runningAppsLoading ? qsTr("Loading...") : qsTr("Refresh list")
                    enabled: !AppSplitTunnelingController.runningAppsLoading
                    clickedFunc: function() { runningAppsDrawer.refresh() }
                }
                ListViewType {
                    id: runningList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    enabled: !AppSplitTunnelingController.runningAppsLoading
                    model: runningAppsDrawer.filteredApps
                    ScrollBar.vertical: ScrollBarType {}
                    delegate: Item {
                        id: runningAppRow
                        required property var modelData
                        width: runningList.width
                        implicitHeight: appCheckBox.implicitHeight
                        Image {
                            anchors.right: parent.right
                            anchors.rightMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            width: 24
                            height: 24
                            source: runningAppRow.modelData.appIcon || "qrc:/images/controls/app.svg"
                            fillMode: Image.PreserveAspectFit
                        }
                        CheckBoxType {
                            id: appCheckBox
                            property var modelData: runningAppRow.modelData
                            width: parent.width - 48
                            text: modelData.appName + (modelData.alreadyAdded ? " — " + qsTr("Already added") : "")
                            descriptionText: modelData.appPath
                            enabled: !modelData.alreadyAdded
                            checked: modelData.alreadyAdded || !!runningAppsDrawer.selectedPaths[modelData.appPath]
                            function handleSwitch(event) {
                                if (!event.isAutoRepeat) {
                                    toggle()
                                    toggled()
                                }
                                event.accepted = true
                            }
                            onToggled: {
                                var selection = Object.assign({}, runningAppsDrawer.selectedPaths)
                                if (checked)
                                    selection[modelData.appPath] = true
                                else
                                    delete selection[modelData.appPath]
                                runningAppsDrawer.selectedPaths = selection
                            }
                        }
                    }
                    Label {
                        anchors.centerIn: parent
                        visible: runningList.count === 0 && !AppSplitTunnelingController.runningAppsLoading
                        text: qsTr("No applications found")
                        color: AmneziaStyle.color.paleGray
                    }
                }
                BasicButtonType {
                    Layout.fillWidth: true
                    text: qsTr("Add selected")
                    enabled: root.pageEnabled && !AppSplitTunnelingController.runningAppsLoading &&
                             Object.keys(runningAppsDrawer.selectedPaths).length > 0
                    clickedFunc: function() {
                        AppSplitTunnelingController.addRunningApps(Object.keys(runningAppsDrawer.selectedPaths))
                        runningAppsDrawer.closeTriggered()
                    }
                }
            }
        }
    }

    InstalledAppsDrawer {
        id: installedAppDrawer

        anchors.fill: parent
    }
}
