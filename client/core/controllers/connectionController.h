#ifndef CONNECTIONCONTROLLER_H
#define CONNECTIONCONTROLLER_H

#include <QObject>
#include <QJsonObject>
#include <QPair>
#include <memory>

#include "core/utils/containerEnum.h"
#include "core/utils/containers/containerUtils.h"
#include "core/utils/protocolEnum.h"
#include "core/utils/errorCodes.h"
#include "core/utils/routeModes.h"
#include "core/utils/commonStructs.h"
#include "core/repositories/secureServersRepository.h"
#include "core/repositories/secureAppSettingsRepository.h"
#include "core/protocols/vpnProtocol.h"
#include "vpnConnection.h"
#include "core/utils/splitTunnelSession.h"
#include "core/proxy/proxyConnection.h"

using namespace amnezia;

class ConnectionController : public QObject
{
    Q_OBJECT

public:
    enum class ConnectionMode { Vpn, Proxy };
    Q_ENUM(ConnectionMode)
    bool proxyMode() const { return m_mode == ConnectionMode::Proxy; }
    bool proxyAvailable() const;
    QJsonObject proxySettings() const { return m_appSettingsRepository->proxySettings(); }
    void saveProxySettings(const QJsonObject &settings) { m_appSettingsRepository->setProxySettings(settings); }
    void selectProxyMode(bool enabled);
    QString proxyStatus() const { return m_proxy->status(); }
    bool proxyRecoveryPending() const { return m_proxy->recoveryPending(); }
    ProxyConnection *proxyConnection() const { return m_proxy; }
    explicit ConnectionController(SecureServersRepository* serversRepository,
                                 SecureAppSettingsRepository* appSettingsRepository,
                                 VpnConnection* vpnConnection,
                                 QObject* parent = nullptr);
    ~ConnectionController() = default;

    ErrorCode prepareConnection(const QString &serverId,
                               QJsonObject& vpnConfiguration,
                               DockerContainer& container,
                               const QJsonObject &splitSettings = {},
                               DockerContainer forcedContainer = DockerContainer::None);

    ErrorCode isConnectionSupported(const QString &serverId) const;

    ErrorCode openConnection(const QString &serverId);

    void closeConnection();
    void reconnectWithSavedRules();
    bool hasPendingSplitTunnelChanges() const;
    bool isSplitTunnelReconnectInProgress() const { return m_splitSession.reconnecting(); }

#ifdef Q_OS_ANDROID
    void restoreConnection();
#endif

    void onKillSwitchModeChanged(bool enabled);

    ErrorCode lastConnectionError() const;

    bool isConnected() const;
    void setConnectionState(Vpn::ConnectionState state);

    QJsonObject createConnectionConfiguration(const QPair<QString, QString> &dns,
                                             bool isApiConfig,
                                             const QString &hostName,
                                             const QString &description,
                                             int configVersion,
                                             const ContainerConfig &containerConfig,
                                             DockerContainer container,
                                             const QJsonObject &splitSettings = {});

    bool isServiceReady() const;

    bool isContainerSupported(DockerContainer container) const;

signals:
    void proxyChanged();
    void proxyError(const QString &message);
    void splitTunnelStatusChanged();
    void reconnectErrorOccurred(ErrorCode error);
    void connectionStateChanged(Vpn::ConnectionState state);
    void connectionNotificationStateChanged(Vpn::ConnectionState state, bool silent);
    void openConnectionRequested(const QString &serverId, DockerContainer container, const QJsonObject &vpnConfiguration);
    void closeConnectionRequested();
    void killSwitchModeChangedRequested(bool enabled);

#ifdef Q_OS_ANDROID
    void restoreConnectionRequested();
#endif

private:
    QJsonObject effectiveSplitSnapshot() const;
    bool stopActiveBackend();
    ConnectionMode m_mode = ConnectionMode::Vpn;
    ProxyConnection *m_proxy = nullptr;
    QJsonObject m_proxySettingsBaseline;
    bool m_switchPending = false;
    ConnectionMode m_nextMode = ConnectionMode::Vpn;
    void refreshSplitTunnelSettings();
    void handleConnectionState(Vpn::ConnectionState state);
    ErrorCode openConnectionImpl(const QString &serverId, DockerContainer forcedContainer);
    SplitTunnelSession m_splitSession;
    Vpn::ConnectionState m_state = Vpn::Disconnected;
    QString m_sessionServerId;
    DockerContainer m_sessionContainer = DockerContainer::None;
    bool m_opening = false;
    quint64 m_requestGeneration = 0;
    ErrorCode defaultContainerForServer(const QString &serverId, DockerContainer &container) const;

    SecureServersRepository* m_serversRepository;
    SecureAppSettingsRepository* m_appSettingsRepository;
    VpnConnection* m_vpnConnection;
};

#endif
