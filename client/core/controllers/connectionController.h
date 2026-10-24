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

using namespace amnezia;

class ConnectionController : public QObject
{
    Q_OBJECT

public:
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
    bool hasPendingSplitTunnelChanges() const { return m_splitSession.pending(); }
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
