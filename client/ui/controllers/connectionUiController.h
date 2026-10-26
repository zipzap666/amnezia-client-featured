#ifndef CONNECTIONUICONTROLLER_H
#define CONNECTIONUICONTROLLER_H

#include <QObject>

#include "core/controllers/connectionController.h"
#include "core/utils/errorCodes.h"
#include "core/utils/routeModes.h"
#include "core/utils/commonStructs.h"
#include "core/protocols/vpnProtocol.h"
#include "core/controllers/serversController.h"

class ConnectionUiController : public QObject
{
    Q_OBJECT

public:
    Q_PROPERTY(bool proxyMode READ proxyMode NOTIFY proxyChanged)
    Q_PROPERTY(bool proxyAvailable READ proxyAvailable NOTIFY proxyChanged)
    Q_PROPERTY(QVariantMap proxySettings READ proxySettings NOTIFY proxyChanged)
    Q_PROPERTY(QString proxyStatus READ proxyStatus NOTIFY proxyChanged)
    Q_PROPERTY(bool proxyRecoveryPending READ proxyRecoveryPending NOTIFY proxyChanged)
    Q_PROPERTY(QVariantMap proxyCounters READ proxyCounters NOTIFY proxyChanged)
    bool proxyMode() const { return m_connectionController->proxyMode(); }
    bool proxyAvailable() const { return m_connectionController->proxyAvailable(); }
    QVariantMap proxySettings() const { return m_connectionController->proxySettings().toVariantMap(); }
    QString proxyStatus() const { return m_connectionController->proxyStatus(); }
    bool proxyRecoveryPending() const { return m_connectionController->proxyRecoveryPending(); }
    QVariantMap proxyCounters() const { return m_connectionController->proxyConnection()->counters(); }
    Q_INVOKABLE void selectProxyMode(bool enabled) { m_connectionController->selectProxyMode(enabled); }
    Q_INVOKABLE bool saveProxySettings(bool systemProxy, int httpPort, int socksPort) {
        if (httpPort < 1 || httpPort > 65535 || socksPort < 1 || socksPort > 65535 || httpPort == socksPort) return false;
        m_connectionController->saveProxySettings({{"systemProxy",systemProxy},{"httpPort",httpPort},{"socksPort",socksPort}}); return true;
    }
    Q_PROPERTY(bool hasPendingSplitTunnelChanges READ hasPendingSplitTunnelChanges NOTIFY splitTunnelStatusChanged)
    Q_PROPERTY(bool isSplitTunnelReconnectInProgress READ isSplitTunnelReconnectInProgress NOTIFY splitTunnelStatusChanged)
    Q_PROPERTY(bool isConnected READ isConnected NOTIFY connectionStateChanged)
    Q_PROPERTY(bool isConnectionInProgress READ isConnectionInProgress NOTIFY connectionStateChanged)
    Q_PROPERTY(QString connectionStateText READ connectionStateText NOTIFY connectionStateChanged)

    explicit ConnectionUiController(ConnectionController* connectionController,
                                    ServersController* serversController,
                                    QObject *parent = nullptr);

    ~ConnectionUiController() = default;

    bool hasPendingSplitTunnelChanges() const { return m_connectionController->hasPendingSplitTunnelChanges(); }
    bool isSplitTunnelReconnectInProgress() const { return m_connectionController->isSplitTunnelReconnectInProgress(); }
    bool isConnected() const;
    bool isConnectionInProgress() const;
    QString connectionStateText() const;

public slots:
    void reconnectWithSavedRules();
    void toggleConnection();

    void openConnection();
    void closeConnection();

    bool isRevokeBlockedDuringActiveConnection(const QString &serverId, int containerIndex, const QString &clientId) const;

    ErrorCode getLastConnectionError();
    void onConnectionStateChanged(Vpn::ConnectionState state);

    void onTranslationsUpdated();

signals:
    void proxyChanged();
    void splitTunnelStatusChanged();
    void connectionStateChanged();

    void connectionErrorOccurred(ErrorCode errorCode);

    void connectButtonClicked();
    void preparingConfig();
    void prepareConfig();
    void unsupportedConnectDrawerRequested();
    void noInstalledContainers();

private:
    Vpn::ConnectionState getCurrentConnectionState();
    void notifyConnectionBlocked(ErrorCode errorCode);

    ConnectionController* m_connectionController;
    ServersController* m_serversController;

    bool m_isConnected = false;
    bool m_isConnectionInProgress = false;
    QString m_connectionStateText = tr("Connect");

    Vpn::ConnectionState m_state;
};

#endif
