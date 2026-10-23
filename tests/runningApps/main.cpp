#include "core/utils/runningApps.h"
#include <QCoreApplication>
#include <QProcess>
#include <QThread>
#include <QTemporaryDir>
#include <QJSEngine>
#include <QDebug>
#include <cstdio>
#include <cstdlib>

static void check(bool ok, const char *message) { if (!ok) { fprintf(stderr, "FAIL: %s\n", message); std::exit(1); } }
static int countPath(const QVariantList &apps, const QString &path) {
    int count = 0;
    for (const auto &value : apps)
        count += RunningApps::pathKey(value.toMap().value("appPath").toString()) == RunningApps::pathKey(path);
    return count;
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (app.arguments().contains("--child")) { QThread::sleep(30); return 0; }
    if (app.arguments().contains("--exit")) return 0;
    check(RunningApps::pathKey("C:\\Apps\\APP.exe") == RunningApps::pathKey("c:/apps/app.EXE"), "case and separators");
    const QString self = QCoreApplication::applicationFilePath();
    QProcess same;
    same.start(self, {"--child"});
    check(same.waitForStarted(), "start duplicate process");
    check(countPath(RunningApps::enumerate(), self) == 1, "deduplicate identical EXE");
    same.kill(); same.waitForFinished();
    QTemporaryDir temp(QDir::currentPath() + "/child-XXXXXX");
    const QString other = temp.filePath("RunningAppsChild.exe");
    check(QFile::copy(self, other), "copy test child");
    QProcess child;
    child.start(other, {"--child"});
    check(child.waitForStarted(), "start distinct process");
    check(countPath(RunningApps::enumerate(), other) == 1, "refresh finds new process");
    child.kill(); child.waitForFinished();
    check(countPath(RunningApps::enumerate(), other) == 0, "refresh removes exited process");
    check(RunningApps::executableIcon(temp.filePath("missing.exe")).isEmpty(), "missing icon fallback");
    const auto icon = RunningApps::executableIcon(qEnvironmentVariable("WINDIR") + "/System32/cmd.exe");
    check(icon.startsWith("data:image/png;base64,"), "extract real executable icon");
    check(!QImage::fromData(QByteArray::fromBase64(icon.mid(icon.indexOf(',') + 1).toLatin1())).isNull(), "valid PNG icon");
    const DWORD guiBefore = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
    DWORD before = 0, after = 0;
    GetProcessHandleCount(GetCurrentProcess(), &before);
    for (int i = 0; i < 30; ++i) {
        QProcess exiting;
        exiting.start(other, {"--exit"});
        check(exiting.waitForStarted(), "start exiting process");
        const auto apps = RunningApps::enumerate();
        QSet<QString> paths;
        for (const auto &value : apps) {
            const auto row = value.toMap();
            const auto path = row.value("appPath").toString();
            check(!path.isEmpty() && QFileInfo(path).isAbsolute(), "full EXE path");
            check(!row.contains("packageName") && !row.contains("pid"), "path-only records");
            check(!paths.contains(RunningApps::pathKey(path)), "unique snapshot");
            paths.insert(RunningApps::pathKey(path));
        }
        exiting.waitForFinished();
    }
    GetProcessHandleCount(GetCurrentProcess(), &after);
    check(GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS) <= guiBefore + 2, "icon handles leaked");
    check(after <= before + 2, "enumeration leaks handles");
    QFile qml(app.arguments().at(1));
    check(qml.open(QIODevice::ReadOnly), "read production QML");
    const QString source = QString::fromUtf8(qml.readAll());
    const auto start = source.indexOf("var query = searchText.toLowerCase()");
    const auto end = source.indexOf("\n        }", start);
    check(start >= 0 && end > start, "locate production search");
    QJSEngine js;
    auto result = js.evaluate("var AppSplitTunnelingController = {runningApps: ["
        "{appName:'Browser.exe',appPath:'C:/Apps/Browser.exe'},"
        "{appName:'Editor.exe',appPath:'D:/Work [1]/Editor.exe'}]};"
        "function search(searchText) {" + source.mid(start, end-start) + "}\n"
        "search('BROWSER').length === 1 && search('d:/work [1]').length === 1 && "
        "search('[').length === 1 && search('missing').length === 0 && search('').length === 2");
    check(!result.isError() && result.toBool(), "literal case-insensitive name/path search");
    const auto toggleStart = source.indexOf("var selection = Object.assign({}, runningAppsDrawer.selectedPaths)");
    const auto toggleEnd = source.indexOf("\n                            }", toggleStart);
    check(toggleStart >= 0 && toggleEnd > toggleStart, "locate production selection handler");
    result = js.evaluate("var runningAppsDrawer = {selectedPaths:{}};"
        "function select(path, checked) { var modelData = {appPath:path};" + source.mid(toggleStart, toggleEnd-toggleStart) + "}"
        "select('a.exe',true); select('b.exe',true); select('a.exe',true);"
        "var multiple = Object.keys(runningAppsDrawer.selectedPaths).length === 2;"
        "select('a.exe',false); multiple && Object.keys(runningAppsDrawer.selectedPaths).length === 1 && runningAppsDrawer.selectedPaths['b.exe']");
    check(!result.isError() && result.toBool(), "multi-select, repeated selection, deselection");
    const auto refreshStart = source.indexOf("function refresh() {") + QString("function refresh() {").size();
    const auto refreshEnd = source.indexOf("\n        }", refreshStart);
    result = js.evaluate("var selectedPaths = {'a.exe':true}; var refreshed = 0;"
        "AppSplitTunnelingController.refreshRunningApps = function(){refreshed++;};"
        "function refresh(){" + source.mid(refreshStart, refreshEnd-refreshStart) + "}"
        "refresh(); Object.keys(selectedPaths).length === 0 && refreshed === 1");
    check(!result.isError() && result.toBool(), "refresh clears selection and requests snapshot");
    qInfo() << "PASS: deduplication, paths, refresh, process exit race, handle lifetime, QML search";
}
