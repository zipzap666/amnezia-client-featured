#include <QGuiApplication>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTimer>
#include <QJSValue>
#include <QFontDatabase>
#include <QTranslator>
#include <cstdio>

static int warnings = 0;
static void messages(QtMsgType type, const QMessageLogContext &, const QString &text) {
    std::fprintf(stderr, "%s\n", qPrintable(text));
    if (type == QtWarningMsg || type == QtCriticalMsg) ++warnings;
}
int main(int argc, char **argv) {
    qInstallMessageHandler(messages);
    qputenv("QT_QUICK_CONTROLS_STYLE", "Basic"); qputenv("QT_QUICK_BACKEND", "software");
    qputenv("QT_QPA_FONTDIR", qgetenv("WINDIR") + "/Fonts");
    QGuiApplication app(argc, argv);
    QFontDatabase::addApplicationFont(QString(REPO_PATH)+"/client/fonts/pt-root-ui_vf.ttf");
    QTranslator translator;
    if (!translator.load(QString(TEST_OUTPUT_PATH)+"/ru.qm")) return 6;
    app.installTranslator(&translator);
    QQmlEngine engine; engine.addImportPath(QString(REPO_PATH) + "/client/ui/qml/Modules");
    QQmlComponent mocks(&engine);
    mocks.setData(R"(
import QtQuick
QtObject {
 property int safeAreaTopMargin: 0
 property bool proxyMode: true
 property bool proxyRecoveryPending: true
 property bool isConnected: true
 property bool isConnectionInProgress: false
 property bool hasPendingSplitTunnelChanges: false
 property var proxySettings: ({systemProxy:true,httpPort:8080,socksPort:1080})
 property string proxyStatus: "System proxy enabled"
 function getLineHeightAppend() { return 0 }
 function resetRootObject() {}
 function setFocusOnDefaultItem() {}
 function saveProxySettings(automatic,http,socks) {
   if (http<1 || http>65535 || socks<1 || socks>65535 || http===socks) return false
   proxySettings={systemProxy:automatic,httpPort:http,socksPort:socks}
   hasPendingSplitTunnelChanges=true
   return true
 }
 function closeConnection() { isConnected=false; proxyRecoveryPending=false }
 function reconnectWithSavedRules() { hasPendingSplitTunnelChanges=false }
})", QUrl());
    auto mock = mocks.create(); if (!mock) { qCritical() << mocks.errors(); return 1; }
    for (const auto *name : {"PageController","ConnectionController","FocusController","LanguageUiController"}) engine.rootContext()->setContextProperty(name,mock);
    QQmlComponent component(&engine, QUrl::fromLocalFile(QString(REPO_PATH) + "/client/ui/qml/Pages2/PageSettingsSystemProxy.qml"));
    auto object = component.create(); if (!object) { qCritical() << component.errors(); return 1; }
    auto page = qobject_cast<QQuickItem *>(object);
    QQuickWindow window; window.setColor(QColor("#0E0E11")); window.setGeometry(0,0,360,640); page->setParentItem(window.contentItem()); page->setSize(QSizeF(360,640)); window.show();
    QTimer::singleShot(400,&app,[&] {
        if (page->width()!=360 || page->height()!=640) { app.exit(2); return; }
        if (!window.grabWindow().save(QString(TEST_OUTPUT_PATH)+"/proxy-settings.png")) { app.exit(3); return; }
        auto settings = mock->property("proxySettings").value<QJSValue>().toVariant().toMap();
        if (settings["httpPort"].toInt()!=8080) { app.exit(4); return; }
        auto field = object->findChild<QObject *>("proxySocksPort")->property("textField").value<QObject *>();
        field->setProperty("text","8080"); QMetaObject::invokeMethod(object,"save");
        if (object->findChild<QObject *>("proxySettingsError")->property("text").toString().isEmpty()) {app.exit(7);return;}
        field->setProperty("text","1081"); QMetaObject::invokeMethod(object,"save");
        if (!mock->property("hasPendingSplitTunnelChanges").toBool()) {app.exit(8);return;}
        auto automatic=object->findChild<QObject *>("proxyAutomatic");
        automatic->setProperty("checked",false);QMetaObject::invokeMethod(automatic,"toggled");
        auto map=mock->property("proxySettings").value<QJSValue>().toVariant().toMap();
        if (map["systemProxy"].toBool() || map["socksPort"].toInt()!=1081) {app.exit(9);return;}
        auto scroll=object->findChild<QQuickItem *>("proxySettingsScroll");
        scroll->setProperty("contentY",scroll->property("contentHeight").toReal()-scroll->height());
        QTimer::singleShot(200,&app,[&] {
            if (!window.grabWindow().save(QString(TEST_OUTPUT_PATH)+"/proxy-settings-bottom.png")) {app.exit(3);return;}
            auto button=object->findChild<QQuickItem *>("proxySettingsReconnect");
            if (!button->isVisible()) {app.exit(10);return;}
            app.exit(warnings ? 5 : 0);
        });
    });
    const int result = app.exec(); delete object; delete mock; return result;
}
