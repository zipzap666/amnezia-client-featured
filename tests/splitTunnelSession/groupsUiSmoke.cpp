#include <QGuiApplication>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QImage>
#include <QTranslator>
#include <QTimer>
#include <QDir>
#include <QJSValue>
#include <cstdio>

static int uiErrors = 0;
static void reportMessage(QtMsgType type, const QMessageLogContext &, const QString &message) {
    std::fprintf(stderr, "%s\n", qPrintable(message));
    if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) ++uiErrors;
}
static QList<QQuickItem *> visualItems(QQuickItem *root, const QString &name) {
    QList<QQuickItem *> result;
    if (root->objectName() == name) result.append(root);
    for (auto child : root->childItems()) result.append(visualItems(child, name));
    return result;
}
int main(int argc, char **argv) {
    qInstallMessageHandler(reportMessage);
    qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
    qputenv("QT_QUICK_BACKEND", "software");
    qputenv("QT_QPA_FONTDIR", qgetenv("WINDIR") + "/Fonts");
    QGuiApplication app(argc, argv);
    QTranslator translator;
    const auto translations = qEnvironmentVariable("GROUPS_TEST_TRANSLATIONS");
    if (!translations.isEmpty()) {
        if (!translator.load(translations)) { qCritical("Cannot load requested translations"); return 1; }
        app.installTranslator(&translator);
    }
    QQmlEngine engine;
    engine.addImportPath(QString(REPO_PATH) + "/client/ui/qml/Modules");
    QQmlComponent mocks(&engine);
    mocks.setData(R"(
import QtQuick
QtObject {
    property string lastMovedKey: ""
    property string lastMovedGroup: ""
    property int safeAreaTopMargin: 0
    property int safeAreaBottomMargin: 0
    property var groups: [{id: "", name: "Ungrouped", enabled: true}, {id: "games", name: "Games", enabled: true}]
    signal closeTopDrawer()
    signal changed()
    function getLineHeightAppend() { return 0 }
    function getDrawerDepth() { return 0 }
    function getInitialPageNavigationBarColor() { return 0xFF1C1D21 }
    function updateNavigationBarColor(value) {}
    function incrementDrawerDepth() { return 1 }
    function decrementDrawerDepth() {}
    function pushRootObject(value) {}
    function dropRootObject(value) {}
    function setGroupEnabled(id, value) {}
    function setRuleEnabled(key, value) {}
    function moveRule(key, id) { lastMovedKey = key; lastMovedGroup = id }
    function createGroup(name) { return "created" }
    function renameGroup(id, name) { return true }
    function removeGroup(id) {}
})", QUrl());
    auto mock = mocks.create();
    if (!mock) { qCritical() << mocks.errors(); return 1; }
    engine.rootContext()->setContextProperty("PageController", mock);
    engine.rootContext()->setContextProperty("FocusController", mock);
    engine.rootContext()->setContextProperty("LanguageUiController", mock);
    QQuickWindow window;
    window.setGeometry(0, 0, 360, 740);
    window.setColor(QColor("#0E0E11"));
    QQuickItem page;
    page.setParentItem(window.contentItem());
    page.setWidth(360); page.setHeight(740);
    window.show();
    QQmlComponent component(&engine, QUrl::fromLocalFile(QString(REPO_PATH) + "/client/ui/qml/Components/SplitTunnelTree.qml"));
    QVariantList sampleRules {
        QVariantMap{{"key", "C:/Apps/discord.exe"}, {"name", "Discord"}, {"details", "C:/Apps/discord.exe"}, {"groupId", ""}, {"enabled", true}},
        QVariantMap{{"key", "C:/Games/bg3.exe"}, {"name", "Baldur's Gate 3"}, {"details", "C:/Games/bg3.exe"}, {"groupId", "games"}, {"enabled", true}},
        QVariantMap{{"key", "C:/Steam/steam.exe"}, {"name", "Steam"}, {"details", "C:/Steam/steam.exe"}, {"groupId", "games"}, {"enabled", false}}
    };
    auto object = component.createWithInitialProperties({{"controller", QVariant::fromValue(mock)},
        {"pageRoot", QVariant::fromValue(&page)}, {"rules", sampleRules}, {"apps", true}});
    if (!object) { qCritical() << component.errors(); return 1; }
    auto item = qobject_cast<QQuickItem *>(object); item->setParentItem(&page); item->setWidth(360); item->setHeight(600);
    if (page.childItems().first() != item) item->stackBefore(page.childItems().first());
    const auto rows = [object] {
        const auto value = object->property("visibleRows");
        return value.metaType() == QMetaType::fromType<QJSValue>() ? value.value<QJSValue>().toVariant().toList() : value.toList();
    };
    bool ok = rows().size() == 2;
    for (const auto &row : rows()) ok = ok && row.toMap().value("isGroup").toBool() && !row.toMap().value("expanded").toBool();
    object->setProperty("searchText", "BG3.EXE");
    ok = ok && rows().size() == 2 && !rows().at(1).toMap().value("isGroup").toBool();
    object->setProperty("searchText", "");
    ok = ok && rows().size() == 2; // Clearing search restores default collapse.
    QMetaObject::invokeMethod(object, "toggleGroup", Q_ARG(QVariant, "games"));
    ok = ok && rows().size() == 4;
    object->setProperty("searchText", "BG3.EXE");
    ok = ok && rows().size() == 2;
    object->setProperty("searchText", "");
    ok = ok && rows().size() == 4; // A manually expanded group stays expanded.
    QMetaObject::invokeMethod(object, "toggleGroup", Q_ARG(QVariant, "games"));
    ok = ok && rows().size() == 2;
    object->setProperty("searchText", "[not a regex]");
    ok = ok && rows().isEmpty();
    object->setProperty("searchText", "");
    ok = ok && rows().size() == 2;
    QMetaObject::invokeMethod(object, "toggleGroup", Q_ARG(QVariant, ""));
    ok = ok && rows().size() == 3;
    QMetaObject::invokeMethod(object, "toggleGroup", Q_ARG(QVariant, "games"));
    ok = ok && rows().size() == 5;
    auto form = object->findChild<QObject *>("splitTunnelGroupForm");
    auto actions = object->findChild<QObject *>("splitTunnelActionsDrawer");
    auto move = object->findChild<QObject *>("splitTunnelMoveDrawer");
    if (!form || !actions || !move) { qCritical("Missing tree drawers"); return 1; }
    const auto capture = [&window](const QString &name) {
        const auto dir = QString(REPO_PATH) + "/.build-tools/split-tree-ui/";
        QDir().mkpath(dir);
        return window.grabWindow().save(dir + name + ".png");
    };
    int stage = 0;
    QTimer timer;
    timer.setInterval(350);
    QObject::connect(&timer, &QTimer::timeout, &app, [&, ok] {
        if (stage == 0) {
            capture("tree-360");
            for (auto row : visualItems(&page, "splitTreeRowContent")) {
                for (auto control : row->childItems()) {
                    if (control->isVisible() && (control->x() < -0.5 || control->x() + control->width() > row->width() + 0.5)) {
                        qCritical("Tree row control exceeds available width");
                    }
                }
            }
            qreal toggleRight = -1;
            auto toggles = visualItems(&page, "splitGroupSwitch");
            toggles.append(visualItems(&page, "splitRuleSwitch"));
            if (toggles.size() != 5) qCritical("Expected switches for every group and rule");
            for (auto toggle : toggles) {
                const auto right = toggle->mapToItem(&page, QPointF(toggle->width(), 0)).x();
                if (toggleRight < 0) toggleRight = right;
                if (qAbs(right - toggleRight) > 0.5) qCritical("Group and rule switches must align vertically");
            }
            for (auto row : visualItems(&page, "splitTreeRowContent")) {
                QQuickItem *remove = nullptr, *actions = nullptr, *toggle = nullptr;
                for (auto control : row->childItems()) {
                    if (!control->isVisible()) continue;
                    if (control->objectName() == "splitRuleRemove") remove = control;
                    if (control->objectName() == "splitRuleActions") actions = control;
                    if (control->objectName() == "splitGroupSwitch" || control->objectName() == "splitRuleSwitch") toggle = control;
                }
                if (actions && toggle && actions->x() >= toggle->x()) qCritical("Actions must be before the switch");
                if (remove && actions && remove->x() >= actions->x()) qCritical("Trash must be before actions");
            }
            QMetaObject::invokeMethod(object, "createGroup");
            ++stage; return;
        }
        if (stage == 1) {
            capture("create-360");
            QMetaObject::invokeMethod(form, "closeTriggered");
            object->setProperty("actionRow", rows().at(2));
            QMetaObject::invokeMethod(actions, "openTriggered");
            ++stage; return;
        }
        if (stage == 2) {
            capture("actions-360");
            QMetaObject::invokeMethod(actions, "closeTriggered");
            bool clicked = false;
            for (auto button : visualItems(&page, "splitRuleActions")) {
                const auto row = button->parentItem()->parentItem();
                const auto value = row->property("modelData");
                const auto data = value.metaType() == QMetaType::fromType<QJSValue>() ? value.value<QJSValue>().toVariant().toMap() : value.toMap();
                if (data.value("key").toString() == "C:/Games/bg3.exe") {
                    if (!button->property("image").toString().endsWith("folder-input.svg")) qCritical("Rule move button must use folder input icon");
                    clicked = QMetaObject::invokeMethod(button, "clicked");
                    break;
                }
            }
            if (!clicked || !move->property("isOpened").toBool() || !actions->property("isClosed").toBool())
                qCritical("Rule move button must open group picker directly");
            ++stage; return;
        }
        capture("move-360");
        auto moveList = object->findChild<QQuickItem *>("splitTunnelMoveList");
        bool moveFits = moveList && moveList->width() > 0 && moveList->height() > 100;
        auto target = visualItems(&page, "splitMoveTarget_").value(0);
        if (!target || !target->property("clickedFunction").value<QJSValue>().isCallable()) moveFits = false;
        else target->property("clickedFunction").value<QJSValue>().call();
        moveFits = moveFits && mock->property("lastMovedKey").toString() == "C:/Games/bg3.exe";
        moveFits = moveFits && mock->property("lastMovedGroup").toString().isEmpty();
        QMetaObject::invokeMethod(move, "closeTriggered");
        timer.stop();
        object->setProperty("rules", QVariantList{QVariantMap{{"key", "example.org"}, {"name", "example.org"},
            {"details", "192.0.2.1"}, {"groupId", "games"}, {"enabled", true}}});
        object->setProperty("apps", false);
        object->setProperty("searchText", "192.0.2.1");
        bool passed = ok && moveFits && rows().size() == 2;
        // Removing a group keeps its rules visible under Ungrouped, even if the old UI row is stale.
        mock->setProperty("groups", QVariantList{QVariantMap{{"id", ""}, {"name", "Ungrouped"}, {"enabled", true}}});
        passed = passed && rows().size() == 2 && rows().at(0).toMap().value("id").toString().isEmpty();
        if (!passed) app.exit(2);
        else { std::puts("PASS: app/site tree, expand/collapse, literal search, state restore, create/actions/move drawers and deleted-group fallback"); app.quit(); }
    });
    timer.start();
    const int result = app.exec();
    delete object; delete mock;
    return result ? result : (uiErrors ? 3 : 0);
}
