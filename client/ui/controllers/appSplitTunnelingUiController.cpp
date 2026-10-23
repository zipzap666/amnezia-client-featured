#include "appSplitTunnelingUiController.h"

#include <QFileInfo>
#include <QFutureWatcher>
#include <QtConcurrent>
#include "core/utils/runningApps.h"

#include "core/utils/errorCodes.h"
#include "core/utils/routeModes.h"
#include "core/utils/commonStructs.h"

AppSplitTunnelingUiController::AppSplitTunnelingUiController(AppSplitTunnelingController* appSplitTunnelingController,
                                                              AppSplitTunnelingModel* appSplitTunnelingModel,
                                                              QObject *parent)
    : QObject(parent),
      m_appSplitTunnelingController(appSplitTunnelingController),
      m_appSplitTunnelingModel(appSplitTunnelingModel)
{
    m_appSplitTunnelingModel->updateModel(m_appSplitTunnelingController->getApps());
}

void AppSplitTunnelingUiController::addApp(const QString &appPath)
{
    amnezia::InstalledAppInfo appInfo { "", "", appPath };
    if (!appPath.isEmpty()) {
        QFileInfo fileInfo(appPath);
        appInfo.appName = fileInfo.fileName();
    }

    if (m_appSplitTunnelingController->addApp(appInfo)) {
        emit finished(tr("Application added: %1").arg(appInfo.appName));
    } else {
        emit errorOccurred(tr("The application has already been added"));
    }
}

void AppSplitTunnelingUiController::addApps(QVector<QPair<QString, QString>> apps)
{
    for (const auto &app : apps) {
        amnezia::InstalledAppInfo appInfo { app.first, app.second, "" };
        m_appSplitTunnelingController->addApp(appInfo);
    }
    emit finished(tr("The selected applications have been added"));
}

void AppSplitTunnelingUiController::removeApp(const int index)
{
    auto modelIndex = m_appSplitTunnelingModel->index(index);
    auto appPath = m_appSplitTunnelingModel->data(modelIndex, AppSplitTunnelingModel::Roles::AppPathRole).toString();
    m_appSplitTunnelingController->removeApp(index);

    QFileInfo fileInfo(appPath);
    emit finished(tr("Application removed: %1").arg(fileInfo.fileName()));
}

void AppSplitTunnelingUiController::toggleSplitTunneling(bool enabled)
{
    m_appSplitTunnelingController->toggleSplitTunneling(enabled);
    emit isSplitTunnelingEnabledChanged();
}

void AppSplitTunnelingUiController::setRouteMode(int routeMode)
{
    m_appSplitTunnelingController->setRouteMode(static_cast<amnezia::AppsRouteMode>(routeMode));
    emit routeModeChanged();
}

int AppSplitTunnelingUiController::getRouteMode() const
{
    return static_cast<int>(m_appSplitTunnelingController->getRouteMode());
}

bool AppSplitTunnelingUiController::isSplitTunnelingEnabled() const
{
    return m_appSplitTunnelingController->isSplitTunnelingEnabled();
}

void AppSplitTunnelingUiController::updateModel()
{
    m_appSplitTunnelingModel->updateModel(m_appSplitTunnelingController->getApps());
}


void AppSplitTunnelingUiController::refreshRunningApps()
{
#ifdef Q_OS_WIN
    if (m_runningAppsLoading)
        return;
    m_runningAppsLoading = true;
    emit runningAppsLoadingChanged();
    auto *watcher = new QFutureWatcher<QVariantList>(this);
    connect(watcher, &QFutureWatcher<QVariantList>::finished, this, [this, watcher] {
        m_runningApps = watcher->result();
        const auto added = m_appSplitTunnelingController->getApps();
        for (auto &value : m_runningApps) {
            auto app = value.toMap();
            const auto key = RunningApps::pathKey(app.value("appPath").toString());
            app.insert("alreadyAdded", std::any_of(added.begin(), added.end(), [&key](const auto &item) {
                return RunningApps::pathKey(item.appPath) == key;
            }));
            value = app;
        }
        m_runningAppsLoading = false;
        emit runningAppsChanged();
        emit runningAppsLoadingChanged();
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run(&RunningApps::enumerate));
#endif
}

void AppSplitTunnelingUiController::addRunningApps(const QStringList &paths)
{
#ifdef Q_OS_WIN
    for (const auto &path : paths) {
        if (!path.isEmpty())
            m_appSplitTunnelingController->addApp({ QFileInfo(path).fileName(), "", path });
    }
    emit finished(tr("The selected applications have been added"));
#endif
}
