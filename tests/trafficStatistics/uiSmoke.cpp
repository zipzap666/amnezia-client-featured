#include <QGuiApplication>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTranslator>
#include <QTimer>
#include <QDir>
#include <QImage>
#include <QJSValue>
#include <QFontDatabase>
#include <cstdio>

static int errors = 0;
static void messages(QtMsgType type, const QMessageLogContext &, const QString &message) {
    std::fprintf(stderr, "%s\n", qPrintable(message));
    if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) ++errors;
}
static bool fits(QQuickItem *item, QQuickItem *page) {
    if (item->isVisible() && item->width() > 0 && item->height() > 0) {
        const auto point = item->mapToItem(page, QPointF(0, 0));
        if (point.x() < -0.5 || point.x() + item->width() > page->width() + 0.5) {
            qCritical() << "Horizontal overflow:" << item << point << item->size();
            return false;
        }
    }
    for (auto child : item->childItems()) if (!fits(child, page)) return false;
    return true;
}
int main(int argc, char **argv) {
    qInstallMessageHandler(messages);
    qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
    qputenv("QT_QUICK_BACKEND", "software");
    qputenv("QT_QPA_FONTDIR", qgetenv("WINDIR") + "/Fonts");
    QGuiApplication app(argc, argv);
    if (QFontDatabase::addApplicationFont(QString(REPO_PATH) + "/client/fonts/pt-root-ui_vf.ttf") < 0) return 1;
    QTranslator translator;
    if (!translator.load(QString(TEST_OUTPUT_PATH) + "/ru.qm")) return 1;
    app.installTranslator(&translator);
    QQmlEngine engine;
    engine.addImportPath(QString(REPO_PATH) + "/client/ui/qml/Modules");
    QQmlComponent mocks(&engine);
    mocks.setData(R"(
import QtQuick
QtObject {
    property int safeAreaTopMargin: 0
    property string connectionStateText: "Подключено"
    property bool isConnected: true
    property bool isConnectionInProgress: false
    property var vpn: ({available: true, observed: true, rx: 2048, tx: 512, received: 1048576, sent: 524288, history: []})
    property var physical: ({available: false, observed: false, rx: 0, tx: 0, received: 0, sent: 0, history: []})
    property real now: 300000
    function getLineHeightAppend() { return 0 }
    function resetRootObject() {}
    function setFocusOnDefaultItem() {}
    function reset() {}
})", QUrl());
    auto mock = mocks.create();
    if (!mock) { qCritical() << mocks.errors(); return 1; }
    for (const auto *name : {"PageController", "ConnectionController", "LanguageUiController", "FocusController", "TrafficStatistics"})
        engine.rootContext()->setContextProperty(name, mock);
    QVariantList history;
    for (int i = 0; i <= 300; ++i)
        history.append(QVariantMap{{"time", i * 1000}, {"valid", i < 140 || i > 150},
                                  {"rx", (i % 20) * 512}, {"tx", (i % 15) * 128}});
    auto vpn = mock->property("vpn").value<QJSValue>().toVariant().toMap();
    vpn["history"] = history;
    mock->setProperty("vpn", vpn);
    QQuickWindow window;
    window.setGeometry(0, 0, 320, 740);
    window.setColor(QColor("#0E0E11"));
    QQmlComponent component(&engine, QUrl::fromLocalFile(QString(REPO_PATH) + "/client/ui/qml/Pages2/PageStatistics.qml"));
    auto object = component.create();
    if (!object) { qCritical() << component.errors(); return 1; }
    auto page = qobject_cast<QQuickItem *>(object);
    auto infoButton = object->findChild<QObject *>("measurementInfoButton");
    auto infoPanel = object->findChild<QQuickItem *>("measurementInfoPanel");
    if (!infoButton || !infoPanel || infoPanel->isVisible()) { qCritical("Measurement details must start collapsed"); return 1; }
    page->setParentItem(window.contentItem());
    page->setWidth(320); page->setHeight(740);
    window.show();
    const auto dir = QString(TEST_OUTPUT_PATH) + "/";
    int stage = 0;
    QTimer timer;
    timer.setInterval(400);
    QObject::connect(&timer, &QTimer::timeout, &app, [&] {
        if (!fits(page, page)) { app.exit(2); return; }
        if (stage == 0) {
            if (!window.grabWindow().save(dir + "statistics-ru-320-60.png")) { app.exit(3); return; }
            page->setProperty("period", 300);
        } else if (stage == 1) {
            if (!window.grabWindow().save(dir + "statistics-ru-320-300.png")) { app.exit(3); return; }
            const auto flickables = page->findChildren<QQuickItem *>();
            for (auto item : flickables) if (item->property("contentY").isValid()) item->setProperty("contentY", 600);
        } else if (stage == 2) {
            if (!window.grabWindow().save(dir + "statistics-ru-320-details.png")) { app.exit(3); return; }
            infoButton->setProperty("checked", true);
        } else if (stage == 3) {
            if (!infoPanel->isVisible()) { qCritical("Measurement details do not expand"); app.exit(4); return; }
            if (!window.grabWindow().save(dir + "statistics-ru-320-info.png")) { app.exit(3); return; }
            infoButton->setProperty("checked", false);
            page->setWidth(360); window.setWidth(360);
        } else {
            if (infoPanel->isVisible()) { qCritical("Measurement details do not collapse"); app.exit(4); return; }
            if (!window.grabWindow().save(dir + "statistics-ru-360-details.png")) { app.exit(3); return; }
            app.exit(errors ? 4 : 0);
        }
        ++stage;
    });
    timer.start();
    const auto result = app.exec();
    delete object;
    delete mock;
    return result;
}
