#include "connectionController.h"

#include <QJsonDocument>

#include "core/configurators/configuratorBase.h"
#include "core/utils/protocolEnum.h"
#include "core/protocols/protocolUtils.h"
#include "core/utils/constants/configKeys.h"
#include "core/utils/payloadSender.h"
#include "core/utils/utilities.h"
#include "core/utils/serverConfigUtils.h"
#include "version.h"
#include "core/utils/containerEnum.h"
#include "core/utils/containers/containerUtils.h"
#include "core/utils/protocolEnum.h"
#include "core/models/containerConfig.h"
#include "core/models/protocolConfig.h"

using namespace amnezia;
using namespace ProtocolUtils;

ConnectionController::ConnectionController(SecureServersRepository* serversRepository,
                                         SecureAppSettingsRepository* appSettingsRepository,
                                         VpnConnection* vpnConnection,
                                         QObject* parent)
    : QObject(parent),
      m_serversRepository(serversRepository),
      m_appSettingsRepository(appSettingsRepository),
      m_vpnConnection(vpnConnection)
{
    connect(m_vpnConnection, &VpnConnection::connectionStateChanged, this, &ConnectionController::handleConnectionState);
    refreshSplitTunnelSettings();
    connect(m_appSettingsRepository, &SecureAppSettingsRepository::sitesChanged, this, &ConnectionController::refreshSplitTunnelSettings);
    connect(m_appSettingsRepository, &SecureAppSettingsRepository::appsChanged, this, &ConnectionController::refreshSplitTunnelSettings);
    connect(m_appSettingsRepository, &SecureAppSettingsRepository::routeModeChanged, this, &ConnectionController::refreshSplitTunnelSettings);
    connect(m_appSettingsRepository, &SecureAppSettingsRepository::appsRouteModeChanged, this, &ConnectionController::refreshSplitTunnelSettings);
    connect(m_appSettingsRepository, &SecureAppSettingsRepository::sitesSplitTunnelingEnabledChanged, this, &ConnectionController::refreshSplitTunnelSettings);
    connect(m_appSettingsRepository, &SecureAppSettingsRepository::appsSplitTunnelingEnabledChanged, this, &ConnectionController::refreshSplitTunnelSettings);
    connect(m_vpnConnection, &VpnConnection::splitTunnelSnapshotConnected, this, [this](const QJsonObject &snapshot) {
        m_splitSession.connected(snapshot);
        refreshSplitTunnelSettings();
    });
    connect(this, &ConnectionController::openConnectionRequested, m_vpnConnection, &VpnConnection::connectToVpn, Qt::QueuedConnection);
    connect(this, &ConnectionController::closeConnectionRequested, m_vpnConnection, &VpnConnection::disconnectFromVpn, Qt::QueuedConnection);
    connect(this, &ConnectionController::killSwitchModeChangedRequested, m_vpnConnection, &VpnConnection::onKillSwitchModeChanged, Qt::QueuedConnection);
#ifdef Q_OS_ANDROID
    connect(this, &ConnectionController::restoreConnectionRequested, m_vpnConnection, &VpnConnection::restoreConnection, Qt::QueuedConnection);
#endif
}

bool ConnectionController::isConnected() const
{
    return m_vpnConnection && m_vpnConnection->connectionState() == Vpn::ConnectionState::Connected;
}

void ConnectionController::setConnectionState(Vpn::ConnectionState state)
{
    handleConnectionState(state);
}

ErrorCode ConnectionController::defaultContainerForServer(const QString &serverId, DockerContainer &container) const
{
    const auto kind = m_serversRepository->serverKind(serverId);
    switch (kind) {
    case serverConfigUtils::ConfigType::SelfHostedAdmin: {
        const auto cfg = m_serversRepository->selfHostedAdminConfig(serverId);
        if (!cfg.has_value()) {
            return ErrorCode::InternalError;
        }
        container = cfg->defaultContainer;
        return ErrorCode::NoError;
    }
    case serverConfigUtils::ConfigType::SelfHostedUser: {
        const auto cfg = m_serversRepository->selfHostedUserConfig(serverId);
        if (!cfg.has_value()) {
            return ErrorCode::InternalError;
        }
        container = cfg->defaultContainer;
        return ErrorCode::NoError;
    }
    case serverConfigUtils::ConfigType::Native: {
        const auto cfg = m_serversRepository->nativeConfig(serverId);
        if (!cfg.has_value()) {
            return ErrorCode::InternalError;
        }
        container = cfg->defaultContainer;
        return ErrorCode::NoError;
    }
    case serverConfigUtils::ConfigType::AmneziaPremiumV2:
    case serverConfigUtils::ConfigType::AmneziaFreeV3:
    case serverConfigUtils::ConfigType::ExternalPremium: {
        const auto cfg = m_serversRepository->apiV2Config(serverId);
        if (!cfg.has_value()) {
            return ErrorCode::InternalError;
        }
        container = cfg->defaultContainer;
        return ErrorCode::NoError;
    }
    case serverConfigUtils::ConfigType::AmneziaPremiumV1:
    case serverConfigUtils::ConfigType::AmneziaFreeV2:
        return ErrorCode::LegacyApiV1NotSupportedError;
    case serverConfigUtils::ConfigType::Invalid:
    default:
        return ErrorCode::InternalError;
    }
}

ErrorCode ConnectionController::isConnectionSupported(const QString &serverId) const
{
    if (serverId.isEmpty()) {
        return ErrorCode::InternalError;
    }

    if (!isServiceReady()) {
        return ErrorCode::AmneziaServiceNotRunning;
    }

    const serverConfigUtils::ConfigType kind = m_serversRepository->serverKind(serverId);
    if (serverConfigUtils::isLegacyApiSubscription(kind)) {
        return ErrorCode::LegacyApiV1NotSupportedError;
    }

    DockerContainer container = DockerContainer::None;
    const ErrorCode errorCode = defaultContainerForServer(serverId, container);
    if (errorCode != ErrorCode::NoError) {
        return errorCode;
    }

    if (container == DockerContainer::None) {
        if (serverConfigUtils::isApiV2Subscription(kind)) {
            return ErrorCode::NoError;
        }
        return ErrorCode::NoInstalledContainersError;
    }

    if (ContainerUtils::isUnsupportedContainer(container)) {
        return ErrorCode::LegacyContainerNotSupportedError;
    }

    if (!isContainerSupported(container)) {
        return ErrorCode::NotSupportedOnThisPlatform;
    }

    return ErrorCode::NoError;
}

ErrorCode ConnectionController::prepareConnection(const QString &serverId,
                                                 QJsonObject& vpnConfiguration,
                                                 DockerContainer& container,
                                                 const QJsonObject &splitSettings,
                                                 DockerContainer forcedContainer)
{
    ContainerConfig containerConfigModel;
    QPair<QString, QString> dns;
    QString hostName;
    QString description;
    int configVersion = 0;
    bool isApiConfig = false;

    const auto kind = m_serversRepository->serverKind(serverId);
    const QString primaryDns = m_appSettingsRepository->primaryDns();
    const QString secondaryDns = m_appSettingsRepository->secondaryDns();
    switch (kind) {
    case serverConfigUtils::ConfigType::SelfHostedAdmin: {
        const auto cfg = m_serversRepository->selfHostedAdminConfig(serverId);
        if (!cfg.has_value()) return ErrorCode::InternalError;
        container = forcedContainer == DockerContainer::None ? cfg->defaultContainer : forcedContainer;
        containerConfigModel = cfg->containerConfig(container);
        dns = cfg->getDnsPair(m_appSettingsRepository->useAmneziaDns(), primaryDns, secondaryDns);
        hostName = cfg->hostName;
        description = cfg->description;
        break;
    }
    case serverConfigUtils::ConfigType::SelfHostedUser: {
        const auto cfg = m_serversRepository->selfHostedUserConfig(serverId);
        if (!cfg.has_value()) return ErrorCode::InternalError;
        container = forcedContainer == DockerContainer::None ? cfg->defaultContainer : forcedContainer;
        containerConfigModel = cfg->containerConfig(container);
        dns = cfg->getDnsPair(primaryDns, secondaryDns);
        hostName = cfg->hostName;
        description = cfg->description;
        break;
    }
    case serverConfigUtils::ConfigType::Native: {
        const auto cfg = m_serversRepository->nativeConfig(serverId);
        if (!cfg.has_value()) return ErrorCode::InternalError;
        container = forcedContainer == DockerContainer::None ? cfg->defaultContainer : forcedContainer;
        containerConfigModel = cfg->containerConfig(container);
        dns = cfg->getDnsPair(primaryDns, secondaryDns);
        hostName = cfg->hostName;
        description = cfg->description;
        break;
    }
    case serverConfigUtils::ConfigType::AmneziaPremiumV2:
    case serverConfigUtils::ConfigType::AmneziaFreeV3:
    case serverConfigUtils::ConfigType::ExternalPremium: {
        const auto cfg = m_serversRepository->apiV2Config(serverId);
        if (!cfg.has_value()) return ErrorCode::InternalError;
        container = forcedContainer == DockerContainer::None ? cfg->defaultContainer : forcedContainer;
        containerConfigModel = cfg->containerConfig(container);
        dns = cfg->getDnsPair(primaryDns, secondaryDns);
        hostName = cfg->hostName;
        description = cfg->description;
        configVersion = serverConfigUtils::ConfigSource::AmneziaGateway;
        isApiConfig = true;
        break;
    }
    case serverConfigUtils::ConfigType::AmneziaPremiumV1:
    case serverConfigUtils::ConfigType::AmneziaFreeV2:
        return ErrorCode::InternalError;
    case serverConfigUtils::ConfigType::Invalid:
    default:
        return ErrorCode::InternalError;
    }

    vpnConfiguration = createConnectionConfiguration(dns, isApiConfig, hostName, description, configVersion,
                                                     containerConfigModel, container, splitSettings);

    return ErrorCode::NoError;
}

ErrorCode ConnectionController::openConnection(const QString &serverId)
{
    if (m_opening || m_splitSession.reconnecting() || m_state == Vpn::Connected
        || m_state == Vpn::Connecting || m_state == Vpn::Reconnecting || m_state == Vpn::Disconnecting)
        return ErrorCode::NoError;
    return openConnectionImpl(serverId, DockerContainer::None);
}

ErrorCode ConnectionController::openConnectionImpl(const QString &serverId, DockerContainer forcedContainer)
{
    const auto request = ++m_requestGeneration;
    m_opening = true;
    const auto snapshot = m_appSettingsRepository->splitTunnelSnapshot();
    QJsonObject vpnConfiguration;
    DockerContainer container;
    ErrorCode errorCode = prepareConnection(serverId, vpnConfiguration, container, snapshot, forcedContainer);
    if (errorCode != ErrorCode::NoError) {
        m_opening = false;
        return errorCode;
    }
    m_sessionServerId = serverId;
    m_sessionContainer = container;
    m_splitSession.begin(snapshot);
    refreshSplitTunnelSettings();

    const auto apiV2 = m_serversRepository->apiV2Config(serverId);
    if (apiV2.has_value() && !apiV2->sendPayload.isEmpty()) {
        PayloadSender::sendAll(apiV2->sendPayload);
    }

    if (request != m_requestGeneration) {
        m_opening = false;
        return ErrorCode::NoError;
    }
    emit openConnectionRequested(serverId, container, vpnConfiguration);
    return ErrorCode::NoError;
}

void ConnectionController::closeConnection()
{
    ++m_requestGeneration;
    m_splitSession.finishReconnect();
    emit splitTunnelStatusChanged();
    if (m_vpnConnection) {
        emit closeConnectionRequested();
    }
}

#ifdef Q_OS_ANDROID
void ConnectionController::restoreConnection()
{
    if (m_vpnConnection) {
        emit restoreConnectionRequested();
    }
}
#endif

void ConnectionController::onKillSwitchModeChanged(bool enabled)
{
    if (m_vpnConnection) {
        emit killSwitchModeChangedRequested(enabled);
    }
}

ErrorCode ConnectionController::lastConnectionError() const
{
    return m_vpnConnection->lastError();
}

QJsonObject ConnectionController::createConnectionConfiguration(const QPair<QString, QString> &dns,
                                                              bool isApiConfig,
                                                              const QString &hostName,
                                                              const QString &description,
                                                              int configVersion,
                                                              const ContainerConfig &containerConfig,
                                                              DockerContainer container,
                                                              const QJsonObject &splitSettings)
{
    const auto snapshot = splitSettings.isEmpty() ? m_appSettingsRepository->splitTunnelSnapshot() : splitSettings;
    QJsonObject vpnConfiguration {};

    if (ContainerUtils::containerService(container) == ServiceType::Other) {
        return vpnConfiguration;
    }

    Proto proto = ContainerUtils::defaultProtocol(container);

    ConnectionSettings connectionSettings = {
        { dns.first, dns.second },
        isApiConfig,
        {
            snapshot.value("sitesEnabled").toBool(),
            static_cast<RouteMode>(snapshot.value("siteMode").toInt())
        }
    };

    auto configurator = ConfiguratorBase::create(proto, nullptr);
    ProtocolConfig processedConfig = configurator->processConfigWithLocalSettings(connectionSettings,
                                                                                  containerConfig.protocolConfig);

    QJsonObject vpnConfigData = processedConfig.getClientConfigJson();
    if (ContainerUtils::isAwgContainer(container) || container == DockerContainer::WireGuard) {
        if (vpnConfigData[configKey::mtu].toString().isEmpty()) {
            vpnConfigData[configKey::mtu] =
                    ContainerUtils::isAwgContainer(container) ? protocols::awg::defaultMtu :
                    protocols::wireguard::defaultMtu;
        }
    }

    vpnConfiguration.insert(SplitTunnelSettings::configKey, snapshot);
    vpnConfiguration.insert(ProtocolUtils::key_proto_config_data(proto), vpnConfigData);
    vpnConfiguration[configKey::vpnProto] = ProtocolUtils::protoToString(proto);

    vpnConfiguration[configKey::dns1] = dns.first;
    vpnConfiguration[configKey::dns2] = dns.second;

    vpnConfiguration[configKey::hostName] = hostName;
    vpnConfiguration[configKey::description] = description;
    vpnConfiguration[configKey::configVersion] = configVersion;

    return vpnConfiguration;
}

bool ConnectionController::isServiceReady() const
{
#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS) && !defined(MACOS_NE)
    return Utils::processIsRunning(Utils::executable(SERVICE_NAME, false), true);
#else
    return true;
#endif
}

bool ConnectionController::isContainerSupported(DockerContainer container) const
{
    return ContainerUtils::isSupportedByCurrentPlatform(container);
}

void ConnectionController::refreshSplitTunnelSettings()
{
    m_splitSession.save(m_appSettingsRepository->splitTunnelSnapshot());
    emit splitTunnelStatusChanged();
}

void ConnectionController::reconnectWithSavedRules()
{
    if (!m_splitSession.requestReconnect(m_state == Vpn::Connected && !m_opening)) return;
    emit splitTunnelStatusChanged();
    emit closeConnectionRequested();
}

void ConnectionController::handleConnectionState(Vpn::ConnectionState state)
{
    const bool silent = m_splitSession.reconnecting();
    const bool wasOpening = m_opening;
    m_state = state;
    if (state == Vpn::Connected || state == Vpn::Disconnected || state == Vpn::Error || state == Vpn::Unknown)
        m_opening = false;
    if (state == Vpn::Disconnected && m_splitSession.disconnected()) {
        // Queue after the worker's completed teardown; never start by timeout.
        QMetaObject::invokeMethod(this, [this] {
            if (!m_splitSession.reconnecting() || m_state != Vpn::Disconnected) return;
            const auto error = openConnectionImpl(m_sessionServerId, m_sessionContainer);
            if (error != ErrorCode::NoError) {
                m_splitSession.finishReconnect();
                emit splitTunnelStatusChanged();
                emit reconnectErrorOccurred(error);
            }
        }, Qt::QueuedConnection);
    }
    if (state == Vpn::Connected || state == Vpn::Error || state == Vpn::Unknown
        || (state == Vpn::Disconnected && wasOpening))
        m_splitSession.finishReconnect();
    emit splitTunnelStatusChanged();
    emit connectionStateChanged(state);
    emit connectionNotificationStateChanged(state, silent);
}
