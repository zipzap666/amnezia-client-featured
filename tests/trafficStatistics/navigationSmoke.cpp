#include <QGuiApplication>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QFontDatabase>
#include <QFile>
#include <QImage>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>
#include "ui/utils/pageEnum.h"

static int errors = 0;
static void messages(QtMsgType type, const QMessageLogContext &, const QString &message) {
    fprintf(stderr, "%s\n", qPrintable(message));
    if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) ++errors;
}
int main(int argc, char **argv) {
    qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
    qputenv("QT_QUICK_BACKEND", "software");
    qputenv("QT_QPA_FONTDIR", qgetenv("WINDIR") + "/Fonts");
    qInstallMessageHandler(messages);
    QGuiApplication app(argc, argv);
    QFontDatabase::addApplicationFont(QString(REPO_PATH) + "/client/fonts/pt-root-ui_vf.ttf");
    PageLoader::declareQmlPageEnum();
    const QString sourcePath = QString(REPO_PATH) + "/client/ui/qml/Pages2/PageStart.qml";
    QFile source(sourcePath);
    if (!source.open(QIODevice::ReadOnly)) return 1;
    const QString pageSource = QString::fromUtf8(source.readAll());
    QFile stub(QString(TEST_OUTPUT_PATH) + "/NavigationStub.qml");
    if (!stub.open(QIODevice::WriteOnly)) return 1;
    stub.write("import QtQuick\nItem {}\n"); stub.close();
    const QString stubUrl = QUrl::fromLocalFile(stub.fileName()).toString();
    QString mockSource = R"(
import QtQuick
QtObject {
    property bool proxyMode: false
    property var proxyCounters: ({})
    property int safeAreaTopMargin: 0
    property int safeAreaBottomMargin: 0
    property int imeHeight: 0
    property bool hasServersFromGatewayApi: false
    property bool hasUnread: false
    property bool writeAccess: true
    property bool isLoggingEnabled: false
    property string defaultServerId: "test"
    property bool isConnected: false
    property bool isConnectionInProgress: false
    property string connectionStateText: "Disconnected"
    property var vpn: ({available: false, observed: false, rx: 0, tx: 0, received: 0, sent: 0, history: []})
    property var physical: vpn
    property real now: 0
    function isStartPageVisible() { return false }
    function isOnTv() { return false }
    function isNewsNotificationsEnabled() { return true }
    function hasServerWithWriteAccess() { return writeAccess }
    function setProcessedServerId(value) {}
    function resetRootObject() {}
    function setFocusOnDefaultItem() {}
    function getLineHeightAppend() { return 0 }
)";
    const auto statisticsUrl = QUrl::fromLocalFile(QString(REPO_PATH) + "/client/ui/qml/Pages2/PageStatistics.qml").toString();
    mockSource += "function getPagePath(page) { return (page === " + QString::number(int(PageLoader::PageEnum::PageStatistics))
                  + " ? \"" + statisticsUrl + "\" : \"" + stubUrl + "\") + \"?page=\" + page }\n";
    // Mock only the application's signal surface; render the actual PageStart.
    QRegularExpression expression("function on([A-Z]\\w*)\\(([^)]*)\\)");
    auto matches = expression.globalMatch(pageSource);
    QSet<QString> names;
    while (matches.hasNext()) {
        auto match = matches.next();
        auto name = match.captured(1); name[0] = name[0].toLower();
        if (names.contains(name)) continue;
        names.insert(name);
        QStringList args;
        for (const auto &arg : match.captured(2).split(',', Qt::SkipEmptyParts)) args.append("var " + arg.trimmed());
        mockSource += "signal " + name + "(" + args.join(',') + ")\n";
    }
    mockSource += "}\n";
    QQmlEngine engine;
    engine.addImportPath(QString(REPO_PATH) + "/client/ui/qml/Modules");
    QQmlComponent mocks(&engine);
    mocks.setData(mockSource.toUtf8(), QUrl());
    auto mock = mocks.create();
    if (!mock) { qCritical() << mocks.errors(); return 1; }
    for (const auto *name : {"PageController", "ConnectionController", "ServersUiController", "ServersModel", "NewsModel",
                            "SettingsController", "InstallController", "ImportController", "SubscriptionUiController",
                            "FocusController", "LanguageUiController", "TrafficStatistics"})
        engine.rootContext()->setContextProperty(name, mock);
    QQmlComponent component(&engine, QUrl::fromLocalFile(sourcePath));
    auto object = component.create();
    if (!object) { qCritical() << component.errors(); return 1; }
    auto page = qobject_cast<QQuickItem *>(object);
    QQuickWindow window;
    window.setColor(QColor("#0E0E11")); window.setGeometry(0, 0, 320, 640);
    page->setParentItem(window.contentItem()); page->setWidth(320); page->setHeight(640);
    window.show();
    auto bar = object->findChild<QQuickItem *>("tabBar");
    auto button = object->findChild<QQuickItem *>("statisticsTabButton");
    auto stack = object->findChild<QObject *>("tabBarStackView");
    if (!bar || !button || !stack) return 1;
    const auto check = [](bool condition, const char *message) { if (!condition) qCritical("%s", message); };
    int stage = 0;
    QTimer timer;
    timer.setInterval(350);
    QObject::connect(&timer, &QTimer::timeout, &app, [&] {
        for (const auto *name : {"homeTabButton", "shareTabButton", "settingsTabButton", "plusTabButton", "statisticsTabButton"}) {
            auto item = object->findChild<QQuickItem *>(name);
            if (!item->isVisible()) continue;
            const auto point = item->mapToItem(page, QPointF());
            check(point.x() >= 0 && point.x() + item->width() <= page->width() && item->width() >= 43,
                  "Navigation button exceeds page or is too narrow");
        }
        if (stage == 0) {
            QMetaObject::invokeMethod(button, "clicked");
        } else if (stage == 1) {
            check(bar->property("currentIndex").toInt() == 2 && button->property("isSelected").toBool(), "Statistics tab is not selected");
            const auto share = object->findChild<QQuickItem *>("shareTabButton");
            const auto settings = object->findChild<QQuickItem *>("settingsTabButton");
            check(share->mapToItem(page, QPointF()).x() < button->mapToItem(page, QPointF()).x()
                  && button->mapToItem(page, QPointF()).x() < settings->mapToItem(page, QPointF()).x(),
                  "Statistics must be between Share and Settings");
            auto current = stack->property("currentItem").value<QObject *>();
            check(current && current->objectName().endsWith("page=" + QString::number(int(PageLoader::PageEnum::PageStatistics))),
                  "Statistics tab loaded incorrect page");
            auto back = current ? current->findChild<QQuickItem *>("statisticsBackButton") : nullptr;
            check(back && !back->isVisible() && back->height() == 0, "Root Statistics tab must not show or reserve space for Back");
            check(window.grabWindow().save(QString(TEST_OUTPUT_PATH) + "/navigation-320.png"), "Cannot save screenshot");
            QMetaObject::invokeMethod(object->findChild<QObject *>("settingsTabButton"), "clicked");
            QMetaObject::invokeMethod(mock, "goToPage", Q_ARG(QVariant, int(PageLoader::PageEnum::PageStatistics)), Q_ARG(QVariant, false));
        } else if (stage == 2) {
            check(stack->property("depth").toInt() == 2, "Statistics did not open from Settings");
            auto current = stack->property("currentItem").value<QObject *>();
            auto back = current ? current->findChild<QQuickItem *>("statisticsBackButton") : nullptr;
            check(back && back->isVisible() && back->height() > 0, "Nested Statistics must retain Back");
            QMetaObject::invokeMethod(mock, "escapePressed");
        } else if (stage == 3) {
            check(stack->property("depth").toInt() == 1 && bar->property("currentIndex").toInt() == 3,
                  "Escape from nested Statistics does not return to Settings");
            QMetaObject::invokeMethod(button, "clicked");
            QMetaObject::invokeMethod(mock, "escapePressed");
        } else if (stage == 4) {
            check(bar->property("currentIndex").toInt() == 0, "Escape does not return to Home");
            mock->setProperty("writeAccess", false);
            QMetaObject::invokeMethod(mock, "modelReset");
            QMetaObject::invokeMethod(button, "clicked");
            page->setWidth(360); window.setWidth(360);
        } else {
            check(bar->property("currentIndex").toInt() == 2, "Hidden Share changes Statistics index");
            check(window.grabWindow().save(QString(TEST_OUTPUT_PATH) + "/navigation-360-no-share.png"), "Cannot save screenshot");
            app.exit(errors ? 2 : 0);
        }
        ++stage;
    });
    timer.start();
    const auto result = app.exec();
    delete object; delete mock;
    return result;
}
