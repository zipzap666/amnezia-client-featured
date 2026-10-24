import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Style 1.0
import "../Controls2"
import "../Controls2/TextTypes"

Item {
    id: root
    required property var controller
    required property Item pageRoot
    property var rules: []
    property string searchText: ""
    property bool apps: false
    property var expandedGroups: ({})
    property var actionRow: ({
            isGroup: false,
            name: "",
            groupId: ""
        })
    property string editingGroupId: ""
    property string editingGroupName: ""
    property string groupNameError: ""
    signal removeRuleRequested(string key, string name)
    signal removeGroupRequested(string id, string name)

    // Search expands matching groups without modifying the user's expansion state.
    readonly property var visibleRows: buildRows(rules, controller.groups, searchText, expandedGroups)
    function buildRows(entries, groups, search, expandedGroups) {
        var query = search.trim().toLocaleLowerCase();
        var buckets = ({});
        for (var i = 0; i < groups.length; ++i)
            buckets[groups[i].id] = [];
        for (var j = 0; j < entries.length; ++j) {
            var rule = entries[j];
            var id = Object.prototype.hasOwnProperty.call(buckets, rule.groupId) ? rule.groupId : "";
            buckets[id].push(rule);
        }
        var result = [];
        for (var g = 0; g < groups.length; ++g) {
            var group = groups[g];
            var all = buckets[group.id];
            var groupMatches = query !== "" && group.name.toLocaleLowerCase().indexOf(query) !== -1;
            var matches = all.filter(function (entry) {
                return query === "" || groupMatches || entry.name.toLocaleLowerCase().indexOf(query) !== -1 || entry.key.toLocaleLowerCase().indexOf(query) !== -1 || entry.details.toLocaleLowerCase().indexOf(query) !== -1;
            });
            if (query !== "" && matches.length === 0 && !groupMatches)
                continue;
            var expanded = query !== "" || !!expandedGroups[group.id];
            result.push({
                isGroup: true,
                id: group.id,
                name: group.name,
                enabled: group.enabled,
                count: matches.length,
                expanded: expanded
            });
            if (expanded) {
                matches.sort(function (a, b) {
                    return a.name.localeCompare(b.name);
                });
                for (var r = 0; r < matches.length; ++r) {
                    var entry = matches[r];
                    result.push({
                        isGroup: false,
                        key: entry.key,
                        name: entry.name,
                        details: entry.details,
                        groupId: group.id,
                        enabled: entry.enabled,
                        groupEnabled: group.enabled
                    });
                }
            }
        }
        return result;
    }
    function toggleGroup(id) {
        var next = Object.assign({}, expandedGroups);
        next[id] = !next[id];
        expandedGroups = next;
    }
    function createGroup() {
        editingGroupId = "";
        root.editingGroupName = "";
        root.groupNameError = "";
        groupForm.openTriggered();
    }
    function showActions(row) {
        actionRow = row;
        if (row.isGroup)
            actions.openTriggered();
        else
            moveDrawer.openTriggered();
    }
    function beginRename() {
        var row = actionRow;
        actions.closeTriggered();
        Qt.callLater(function () {
            editingGroupId = row.id;
            root.editingGroupName = row.name;
            root.groupNameError = "";
            groupForm.openTriggered();
        });
    }
    function deleteActionGroup() {
        var row = actionRow;
        actions.closeTriggered();
        Qt.callLater(function () {
            root.removeGroupRequested(row.id, row.name);
        });
    }

    ListViewType {
        id: tree
        objectName: "splitTunnelTreeList"
        anchors.fill: parent
        clip: true
        model: root.visibleRows
        ScrollBar.vertical: ScrollBarType {
            policy: ScrollBar.AsNeeded
        }
        delegate: Item {
            id: treeRow
            required property var modelData
            width: tree.width
            height: modelData.isGroup ? 56 : 60
            RowLayout {
                id: rowContent
                objectName: "splitTreeRowContent"
                anchors.fill: parent
                anchors.leftMargin: treeRow.modelData.isGroup ? 12 : 40
                anchors.rightMargin: 12
                spacing: 8
                ImageButtonType {
                    visible: treeRow.modelData.isGroup
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 40
                    image: treeRow.modelData.expanded ? "qrc:/images/controls/chevron-down.svg" : "qrc:/images/controls/chevron-right.svg"
                    onClicked: root.toggleGroup(treeRow.modelData.id)
                    Accessible.name: treeRow.modelData.expanded ? qsTr("Collapse group") : qsTr("Expand group")
                }
                Image {
                    Layout.preferredWidth: 20
                    Layout.preferredHeight: 20
                    source: treeRow.modelData.isGroup ? (treeRow.modelData.expanded ? "qrc:/images/controls/folder-open.svg" : "qrc:/images/controls/folder.svg") : (root.apps ? "qrc:/images/controls/app.svg" : "qrc:/images/controls/globe-2.svg")
                    opacity: treeRow.modelData.enabled && (treeRow.modelData.isGroup || treeRow.modelData.groupEnabled) ? 1 : 0.45
                    fillMode: Image.PreserveAspectFit
                }
                Label {
                    id: rowLabel
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    text: treeRow.modelData.name + (treeRow.modelData.isGroup ? " (" + treeRow.modelData.count + ")" : "")
                    elide: Text.ElideRight
                    maximumLineCount: 1
                    textFormat: Text.PlainText
                    font.pixelSize: treeRow.modelData.isGroup ? 16 : 15
                    color: treeRow.modelData.enabled && (treeRow.modelData.isGroup || treeRow.modelData.groupEnabled) ? AmneziaStyle.color.paleGray : AmneziaStyle.color.mutedGray
                    HoverHandler {
                        id: rowHover
                    }
                    ToolTip.visible: rowHover.hovered
                    ToolTip.delay: 500
                    ToolTip.text: treeRow.modelData.isGroup ? treeRow.modelData.name : treeRow.modelData.name + "\n" + treeRow.modelData.key + (treeRow.modelData.details !== treeRow.modelData.key ? "\n" + treeRow.modelData.details : "")
                    TapHandler {
                        enabled: treeRow.modelData.isGroup
                        onTapped: root.toggleGroup(treeRow.modelData.id)
                    }
                }
                ImageButtonType {
                    objectName: "splitRuleRemove"
                    Layout.preferredWidth: 32
                    Layout.preferredHeight: 40
                    visible: !treeRow.modelData.isGroup
                    image: "qrc:/images/controls/trash.svg"
                    Accessible.name: qsTr("Remove") + " " + treeRow.modelData.name
                    onClicked: root.removeRuleRequested(treeRow.modelData.key, treeRow.modelData.name)
                }
                ImageButtonType {
                    objectName: "splitRuleActions"
                    Layout.preferredWidth: 32
                    Layout.preferredHeight: 40
                    visible: !treeRow.modelData.isGroup || treeRow.modelData.id !== ""
                    image: treeRow.modelData.isGroup ? "qrc:/images/controls/more-vertical.svg" : "qrc:/images/controls/folder-input.svg"
                    Accessible.name: (treeRow.modelData.isGroup ? qsTr("Actions") : qsTr("Move to group")) + " " + treeRow.modelData.name
                    ToolTip.visible: hovered
                    ToolTip.delay: 500
                    ToolTip.text: treeRow.modelData.isGroup ? qsTr("Actions") : qsTr("Move to group")
                    onClicked: root.showActions(treeRow.modelData)
                }
                SwitcherType {
                    objectName: treeRow.modelData.isGroup ? "splitGroupSwitch" : "splitRuleSwitch"
                    Layout.alignment: Qt.AlignVCenter
                    Layout.preferredWidth: 52
                    Layout.preferredHeight: 40
                    checked: treeRow.modelData.enabled
                    Accessible.name: qsTr("Enable") + " " + treeRow.modelData.name
                    onToggled: {
                        if (treeRow.modelData.isGroup)
                            root.controller.setGroupEnabled(treeRow.modelData.id, checked);
                        else
                            root.controller.setRuleEnabled(treeRow.modelData.key, checked);
                    }
                }
            }
            DividerType {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
            }
        }
        Label {
            anchors.centerIn: parent
            visible: tree.count === 0
            text: qsTr("No matches")
            color: AmneziaStyle.color.mutedGray
        }
    }

    DrawerType2 {
        id: actions
        objectName: "splitTunnelActionsDrawer"
        parent: root.pageRoot
        anchors.fill: parent
        expandedHeight: Math.min(parent.height, 256 + PageController.safeAreaBottomMargin)
        expandedStateContent: Flickable {
            implicitHeight: actions.expandedHeight
            clip: true
            contentHeight: actionContent.implicitHeight + 48 + PageController.safeAreaBottomMargin
            ColumnLayout {
                id: actionContent
                width: Math.min(parent.width - 32, 520)
                anchors.horizontalCenter: parent.horizontalCenter
                y: 24
                spacing: 8
                Label {
                    Layout.fillWidth: true
                    text: root.actionRow.name || ""
                    font.pixelSize: 18
                    color: AmneziaStyle.color.paleGray
                    elide: Text.ElideRight
                }
                LabelWithButtonType {
                    Layout.fillWidth: true
                    visible: root.actionRow.isGroup
                    text: qsTr("Rename group")
                    clickedFunction: root.beginRename
                }
                LabelWithButtonType {
                    Layout.fillWidth: true
                    visible: root.actionRow.isGroup
                    text: qsTr("Delete group")
                    clickedFunction: root.deleteActionGroup
                }
            }
        }
    }

    DrawerType2 {
        id: moveDrawer
        objectName: "splitTunnelMoveDrawer"
        parent: root.pageRoot
        anchors.fill: parent
        expandedHeight: parent.height * 0.65
        expandedStateContent: Item {
            implicitHeight: moveDrawer.expandedHeight
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                Header2Type {
                    Layout.fillWidth: true
                    headerText: qsTr("Move to group")
                }
                ListViewType {
                    objectName: "splitTunnelMoveList"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: root.controller.groups
                    ScrollBar.vertical: ScrollBarType {}
                    delegate: LabelWithButtonType {
                        objectName: "splitMoveTarget_" + modelData.id
                        width: ListView.view.width
                        text: modelData.name
                        rightImageSource: root.actionRow.groupId === modelData.id ? "qrc:/images/controls/check.svg" : ""
                        clickedFunction: function () {
                            var key = root.actionRow.key;
                            var id = modelData.id;
                            moveDrawer.closeTriggered();
                            root.controller.moveRule(key, id);
                        }
                    }
                }
            }
        }
    }

    DrawerType2 {
        id: groupForm
        objectName: "splitTunnelGroupForm"
        parent: root.pageRoot
        anchors.fill: parent
        expandedHeight: Math.min(parent.height, 392 + PageController.safeAreaBottomMargin)
        expandedStateContent: Flickable {
            implicitHeight: groupForm.expandedHeight
            clip: true
            contentHeight: formContent.implicitHeight + 48 + PageController.safeAreaBottomMargin
            ColumnLayout {
                id: formContent
                width: Math.min(parent.width - 32, 520)
                anchors.horizontalCenter: parent.horizontalCenter
                y: 24
                spacing: 16
                Header2Type {
                    Layout.fillWidth: true
                    headerText: root.editingGroupId === "" ? qsTr("Create group") : qsTr("Rename group")
                }
                TextFieldWithHeaderType {
                    id: groupName
                    objectName: "splitTunnelGroupName"
                    Layout.fillWidth: true
                    textField.placeholderText: qsTr("Group name")
                    textField.maximumLength: 80
                    textField.text: root.editingGroupName
                    textField.onTextChanged: root.editingGroupName = textField.text
                    errorText: root.groupNameError
                }
                BasicButtonType {
                    Layout.fillWidth: true
                    text: qsTr("Save")
                    enabled: root.editingGroupName.trim().length > 0
                    clickedFunc: function () {
                        var name = root.editingGroupName;
                        var success = root.editingGroupId === "" ? root.controller.createGroup(name) !== "" : root.controller.renameGroup(root.editingGroupId, name);
                        if (!success) {
                            root.groupNameError = qsTr("Enter a unique group name");
                            return;
                        }
                        groupForm.closeTriggered();
                    }
                }
                BasicButtonType {
                    Layout.fillWidth: true
                    text: qsTr("Cancel")
                    clickedFunc: function () {
                        groupForm.closeTriggered();
                    }
                }
            }
        }
    }
}
