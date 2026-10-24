#ifndef APPSPLITTUNNELINGUICONTROLLER_H
#define APPSPLITTUNNELINGUICONTROLLER_H

#include <QObject>
#include "splitTunnelGroupsUiController.h"
#include <QVector>
#include <QVariant>

#include "core/controllers/appSplitTunnelingController.h"
#include "core/utils/errorCodes.h"
#include "core/utils/routeModes.h"
#include "core/utils/commonStructs.h"
#include "ui/models/appSplitTunnelingModel.h"

class AppSplitTunnelingUiController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList treeRules READ treeRules NOTIFY treeRulesChanged)
    Q_PROPERTY(QObject* groupsController READ groupsController CONSTANT)

    Q_PROPERTY(QVariantList runningApps READ runningApps NOTIFY runningAppsChanged)
    Q_PROPERTY(bool runningAppsLoading READ runningAppsLoading NOTIFY runningAppsLoadingChanged)

    Q_PROPERTY(int routeMode READ getRouteMode WRITE setRouteMode NOTIFY routeModeChanged)
    Q_PROPERTY(bool isSplitTunnelingEnabled READ isSplitTunnelingEnabled NOTIFY isSplitTunnelingEnabledChanged)

public:
    QVariantList treeRules() const;
    SplitTunnelGroupsUiController *groupsController() const { return m_groupsController; }
    explicit AppSplitTunnelingUiController(AppSplitTunnelingController* appSplitTunnelingController,
                                          AppSplitTunnelingModel* appSplitTunnelingModel,
                                          QObject *parent = nullptr);

    QVariantList runningApps() const { return m_runningApps; }
    bool runningAppsLoading() const { return m_runningAppsLoading; }

public slots:
    void removeRuleByKey(const QString &key);
    void refreshRunningApps();
    void addRunningApps(const QStringList &paths);
    void addApp(const QString &appPath);
    void addApps(QVector<QPair<QString, QString>> apps);
    void removeApp(const int index);
    void toggleSplitTunneling(bool enabled);
    void setRouteMode(int routeMode);

    int getRouteMode() const;
    bool isSplitTunnelingEnabled() const;

    void updateModel();

signals:
    void treeRulesChanged();
    void runningAppsChanged();
    void runningAppsLoadingChanged();
    void routeModeChanged();
    void isSplitTunnelingEnabledChanged();
    void errorOccurred(const QString &errorMessage);
    void finished(const QString &message);

private:
    SplitTunnelGroupsUiController *m_groupsController;
    QVariantList m_runningApps;
    bool m_runningAppsLoading = false;
    AppSplitTunnelingController* m_appSplitTunnelingController;
    AppSplitTunnelingModel* m_appSplitTunnelingModel;
};

#endif // APPSPLITTUNNELINGUICONTROLLER_H
