#include "vpnConnection.h"

#include <QDebug>
#include <QEventLoop>
#include <QFile>
#include <QHostInfo>
#include <QJsonObject>
#include <QObject>
#include <QSharedPointer>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <core/configurators/openVpnConfigurator.h>
#include <core/configurators/wireguardConfigurator.h>

#ifdef AMNEZIA_DESKTOP
    #include "core/utils/ipcClient.h"
    #include <core/protocols/wireGuardProtocol.h>
    #include <QRemoteObjectPendingCallWatcher>
#endif

#ifdef Q_OS_ANDROID
    #include "platforms/android/android_controller.h"
    #include <QThread>

#endif

#if defined(Q_OS_IOS) || defined(MACOS_NE)
    #include "platforms/ios/ios_controller.h"
#endif

#include "core/utils/networkUtilities.h"
#include "core/utils/serverConfigUtils.h"
#include "vpnConnection.h"

using namespace ProtocolUtils;

VpnConnection::VpnConnection(SecureServersRepository* serversRepository, SecureAppSettingsRepository* appSettingsRepository, QObject *parent)
    : QObject(parent),
      m_serversRepository(serversRepository),
      m_appSettingsRepository(appSettingsRepository),
      m_checkTimer(this),
      m_connectionState(Vpn::ConnectionState::Disconnected)
{
#if defined(Q_OS_IOS) || defined(MACOS_NE)
    m_checkTimer.setInterval(1000);
    connect(IosController::Instance(), &IosController::connectionStateChanged, this, &VpnConnection::setConnectionState);
    connect(IosController::Instance(), &IosController::bytesChanged, this, &VpnConnection::onBytesChanged);
#endif
}

VpnConnection::~VpnConnection()
{
}

void VpnConnection::onBytesChanged(quint64 receivedBytes, quint64 sentBytes)
{
    emit bytesChanged(receivedBytes, sentBytes);
}

void VpnConnection::onKillSwitchModeChanged(bool enabled)
{
#ifdef AMNEZIA_DESKTOP
    IpcClient::withInterface([enabled](QSharedPointer<IpcInterfaceReplica> iface){
        QRemoteObjectPendingReply<bool> reply = iface->refreshKillSwitch(enabled);
        if (reply.waitForFinished() && reply.returnValue())
            qDebug() << "VpnConnection::onKillSwitchModeChanged: Killswitch refreshed";
        else
            qWarning() << "VpnConnection::onKillSwitchModeChanged: Failed to execute remote refreshKillSwitch call";
    });
#endif
}

void VpnConnection::onConnectionStateChanged(Vpn::ConnectionState state)
{
#ifdef AMNEZIA_DESKTOP
    if (!m_serversRepository || !m_appSettingsRepository) {
        qCritical() << "VpnConnection::onConnectionStateChanged: repositories not initialized";
        return;
    }

    const auto container = m_sessionContainer;
    const auto generation = m_routeGeneration;
    const auto protocol = m_vpnProtocol;
    const auto snapshot = m_splitSnapshot;
    const auto configuration = m_vpnConfiguration;
    IpcClient::withInterface([&, generation, protocol](QSharedPointer<IpcInterfaceReplica> iface) {
        if (generation != m_routeGeneration) return;
        switch (state) {
            case Vpn::ConnectionState::Connected: {
                if (!protocol) return;
                iface->resetIpStack();

                auto flushDns = iface->flushDns();
                if (flushDns.waitForFinished() && flushDns.returnValue())
                    qDebug() << "VpnConnection::onConnectionStateChanged: Successfully flushed DNS";
                else
                    qWarning() << "VpnConnection::onConnectionStateChanged: Failed to flush DNS";

                if (generation != m_routeGeneration) return;
                if (!ContainerUtils::isAwgContainer(container) && container != DockerContainer::WireGuard) {
                    QString dns1 = configuration.value(configKey::dns1).toString();
                    QString dns2 = configuration.value(configKey::dns2).toString();

#ifdef Q_OS_MACOS
                    if (!snapshot.value("sitesEnabled").toBool() || static_cast<RouteMode>(snapshot.value("siteMode").toInt()) != amnezia::RouteMode::VpnAllExceptSites) {
                        iface->routeAddList(protocol->vpnGateway(), QStringList() << dns1 << dns2);
                    }
#else
                    iface->routeAddList(protocol->vpnGateway(), QStringList() << dns1 << dns2);
#endif

                    if (snapshot.value("sitesEnabled").toBool()) {
                        iface->routeDeleteList(protocol->vpnGateway(), QStringList() << "0.0.0.0");
                        RouteMode routeMode = static_cast<RouteMode>(snapshot.value("siteMode").toInt());
                        if (routeMode == amnezia::RouteMode::VpnOnlyForwardSites) {
                            QTimer::singleShot(1000, protocol.data(),
                                               [this, routeMode, generation, protocol]() {
                                                   if (generation == m_routeGeneration && m_connectionState == Vpn::Connected)
                                                       addSitesRoutes(protocol->vpnGateway(), routeMode);
                                               });
                        } else if (routeMode == amnezia::RouteMode::VpnAllExceptSites) {
                            iface->routeAddList(protocol->vpnGateway(), QStringList() << "0.0.0.0/1");
                            iface->routeAddList(protocol->vpnGateway(), QStringList() << "128.0.0.0/1");

                            iface->routeAddList(protocol->routeGateway(), QStringList() << remoteAddress());
#ifdef Q_OS_MACOS
                            iface->routeAddList(protocol->routeGateway(), QStringList() << dns1 << dns2);
#endif
                            addSitesRoutes(protocol->routeGateway(), routeMode);
                        }
                    }
                }
            } break;
            case Vpn::ConnectionState::Disconnected:
            case Vpn::ConnectionState::Error: {
                auto flushDns = iface->flushDns();
                if (flushDns.waitForFinished() && flushDns.returnValue())
                    qDebug() << "VpnConnection::onConnectionStateChanged: Successfully flushed DNS";
                else
                    qWarning() << "VpnConnection::onConnectionStateChanged: Failed to flush DNS";

                if (generation != m_routeGeneration) return;
                auto clearSavedRoutes = iface->clearSavedRoutes();
                if (clearSavedRoutes.waitForFinished() && clearSavedRoutes.returnValue())
                    qDebug() << "VpnConnection::onConnectionStateChanged: Successfully cleared saved routes";
                else
                    qWarning() << "VpnConnection::onConnectionStateChanged: Failed to clear saved routes";
            } break;
            default:
                break;
        }
    });
#endif

#if defined(Q_OS_IOS) || defined(MACOS_NE)
    if (state == Vpn::ConnectionState::Connected ||
        state == Vpn::ConnectionState::Connecting ||
        state == Vpn::ConnectionState::Reconnecting) {
        m_checkTimer.start();
    } else {
        m_checkTimer.stop();
    }
#endif
}

const QString &VpnConnection::remoteAddress() const
{
    return m_remoteAddress;
}

void VpnConnection::setRepositories(SecureServersRepository* serversRepository, SecureAppSettingsRepository* appSettingsRepository)
{
    m_serversRepository = serversRepository;
    m_appSettingsRepository = appSettingsRepository;
}

void VpnConnection::addSitesRoutes(const QString &gw, amnezia::RouteMode mode)
{
#ifdef AMNEZIA_DESKTOP
    if (!m_appSettingsRepository) {
        qCritical() << "VpnConnection::addSitesRoutes: repositories not initialized";
        return;
    }

    const auto generation = m_routeGeneration;
    QStringList ips;
    QStringList sites;
    const QVariantMap m = SplitTunnelSettings::sites(m_splitSnapshot, mode).toVariantMap();
    for (auto i = m.constBegin(); i != m.constEnd(); ++i) {
        if (NetworkUtilities::checkIpSubnetFormat(i.key())) {
            ips.append(i.key());
        } else {
            const QStringList siteIps = SecureAppSettingsRepository::siteIpList(i.value());
            for (const QString &ip : siteIps) {
                if (NetworkUtilities::checkIpSubnetFormat(ip)) {
                    ips.append(ip);
                }
            }
            sites.append(i.key());
        }
    }
    ips.removeDuplicates();

    IpcClient::withInterface([&, generation](QSharedPointer<IpcInterfaceReplica> iface) {
        if (generation != m_routeGeneration) return;
        iface->routeAddList(gw, ips);
    });

    auto remainingLookups = QSharedPointer<int>::create(sites.size());
    auto needFlush = QSharedPointer<bool>::create(false);

    // re-resolve domains
    for (const QString &site : sites) {
        const auto &cbResolv = [this, site, gw, mode, ips, remainingLookups, needFlush, generation](const QHostInfo &hostInfo) {
            if (generation != m_routeGeneration || m_connectionState != Vpn::Connected) return;
            QStringList resolvedIps;
            for (const QHostAddress &addr : hostInfo.addresses()) {
                if (addr.protocol() == QAbstractSocket::NetworkLayerProtocol::IPv4Protocol) {
                    resolvedIps.append(addr.toString());
                }
            }
            resolvedIps.removeDuplicates();
            qDebug() << "[SplitTunneling] addSitesRoutes resolved" << site << "->" << resolvedIps;

            QStringList newIps;
            for (const QString &ip : resolvedIps) {
                if (!ips.contains(ip)) {
                    IpcClient::withInterface([this, generation, gw, ip](QSharedPointer<IpcInterfaceReplica> iface) {
                        if (generation != m_routeGeneration) return;
                        iface->routeAddList(gw, QStringList() << ip);
                    });
                    newIps.append(ip);
                }
            }

            if (!newIps.isEmpty()) {
                *needFlush = true;
            }

            if (--(*remainingLookups) > 0)
                return;

            if (!*needFlush)
                return;

            // Async flush: never waitForFinished() here — that re-enters the event loop and
            // can re-enter this QHostInfo callback until the stack overflows (0xc00000fd).
            IpcClient::withInterface([this, generation](QSharedPointer<IpcInterfaceReplica> iface) {
                if (generation != m_routeGeneration) return;
                QRemoteObjectPendingReply<bool> reply = iface->flushDns();
                auto *watcher = new QRemoteObjectPendingCallWatcher(reply, this);
                QObject::connect(watcher, &QRemoteObjectPendingCallWatcher::finished, this,
                        [](QRemoteObjectPendingCallWatcher *call) {
                            if (call->error() != QRemoteObjectPendingCall::NoError
                                || !call->returnValue().toBool()) {
                                qWarning() << "VpnConnection::addSitesRoutes: Failed to flush DNS";
                            }
                            call->deleteLater();
                        });
            });
        };
        QHostInfo::lookupHost(site, this, cbResolv);
    }
#endif
}

QSharedPointer<VpnProtocol> VpnConnection::vpnProtocol() const
{
    return m_vpnProtocol;
}

void VpnConnection::disconnectSlots()
{
    if (m_vpnProtocol) {
        m_vpnProtocol->disconnect();
    }
}

ErrorCode VpnConnection::lastError() const
{
    if (m_startError != ErrorCode::NoError) return m_startError;
#ifdef Q_OS_ANDROID
    return ErrorCode::AndroidError;
#endif

    if (m_vpnProtocol.isNull()) {
        return ErrorCode::InternalError;
    }

    return m_vpnProtocol.data()->lastError();
}

Vpn::ConnectionState VpnConnection::connectionState() const
{
    return m_connectionState;
}

void VpnConnection::connectToVpn(const QString &serverId, DockerContainer container, const QJsonObject &vpnConfiguration)
{
    if (!m_appSettingsRepository || !m_serversRepository) {
        qCritical() << "VpnConnection::connectToVpn: repositories not initialized";
        setConnectionState(Vpn::ConnectionState::Error);
        return;
    }

    ++m_routeGeneration;
    m_restartAfterDisconnect = false;
    m_startError = ErrorCode::NoError;
    m_splitSnapshot = vpnConfiguration.value(SplitTunnelSettings::configKey).toObject();
    m_sessionContainer = container;
    qDebug() << QString("Trying to connect to VPN, server id is %1, container is %2, route mode is")
                        .arg(serverId)
                        .arg(ContainerUtils::containerToString(container))
             << static_cast<RouteMode>(m_splitSnapshot.value("siteMode").toInt());

    m_remoteAddress = NetworkUtilities::getIPAddress(vpnConfiguration.value(configKey::hostName).toString());
    setConnectionState(Vpn::ConnectionState::Connecting);

    m_vpnConfiguration = vpnConfiguration;

    // A newly added domain may not have a cached address yet. Resolve only
    // missing addresses into this attempt, before generating protocol rules.
    const auto generation = m_routeGeneration;
    const int mode = m_splitSnapshot.value("siteMode").toInt();
    const auto sites = SplitTunnelSettings::sites(m_splitSnapshot, mode);
    QStringList unresolved;
    if (m_splitSnapshot.value("sitesEnabled").toBool()) {
        for (auto i = sites.begin(); i != sites.end(); ++i)
            if (!NetworkUtilities::checkIpSubnetFormat(i.key())
                && SecureAppSettingsRepository::siteIpList(i.value().toVariant()).isEmpty())
                unresolved.append(i.key());
    }
    if (unresolved.isEmpty()) {
        startConfiguredProtocol(container, generation);
        return;
    }
    auto remaining = QSharedPointer<int>::create(unresolved.size());
    for (const auto &site : unresolved) {
        QHostInfo::lookupHost(site, this, [this, generation, container, mode, site, remaining](const QHostInfo &info) {
            if (generation != m_routeGeneration || m_connectionState != Vpn::Connecting) return;
            QJsonArray addresses;
            for (const auto &address : info.addresses())
                if (address.protocol() == QAbstractSocket::IPv4Protocol) addresses.append(address.toString());
            if (addresses.isEmpty()) {
                m_startError = ErrorCode::InternalError;
                setConnectionState(Vpn::Error);
                emit vpnProtocolError(ErrorCode::InternalError);
                return;
            }
            auto lists = m_splitSnapshot.value("sites").toObject();
            auto entries = lists.value(QString::number(mode)).toObject();
            entries.insert(site, addresses);
            lists.insert(QString::number(mode), entries);
            m_splitSnapshot.insert("sites", lists);
            if (--*remaining == 0) startConfiguredProtocol(container, generation);
        });
    }
}

void VpnConnection::startConfiguredProtocol(DockerContainer container, quint64 generation)
{
    if (generation != m_routeGeneration || m_connectionState != Vpn::Connecting) return;
#ifdef AMNEZIA_DESKTOP
    if (m_vpnProtocol) {
        disconnect(m_vpnProtocol.data(), nullptr, this, nullptr);
        m_vpnProtocol->stop();
        m_vpnProtocol.reset();
    }
    appendKillSwitchConfig();
#endif

    if (generation != m_routeGeneration) return;
    appendSplitTunnelingConfig();

#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS) && !defined(MACOS_NE)
    m_vpnProtocol.reset(VpnProtocol::factory(container, m_vpnConfiguration));
    if (!m_vpnProtocol) {
        m_startError = ErrorCode::InternalError;
        setConnectionState(Vpn::ConnectionState::Error);
        return;
    }
    const auto prepareError = m_vpnProtocol->prepare();
    if (generation != m_routeGeneration) return;
    if (prepareError != ErrorCode::NoError) {
        m_startError = prepareError;
        setConnectionState(Vpn::Error);
        emit vpnProtocolError(prepareError);
        return;
    }
#elif defined Q_OS_ANDROID
    androidVpnProtocol = createDefaultAndroidVpnProtocol();
    createAndroidConnections();

    m_vpnProtocol.reset(androidVpnProtocol);
#elif defined Q_OS_IOS || defined(MACOS_NE)
    Proto proto = ContainerUtils::defaultProtocol(container);
    IosController::Instance()->connectVpn(proto, m_vpnConfiguration);
    connect(&m_checkTimer, &QTimer::timeout, IosController::Instance(), &IosController::checkStatus);
    return;
#endif

    createProtocolConnections();
    if (generation != m_routeGeneration || m_connectionState != Vpn::Connecting) return;

    const auto startError = m_vpnProtocol->start();
    if (generation != m_routeGeneration) return;
    if (startError != ErrorCode::NoError) {
        m_startError = startError;
        setConnectionState(Vpn::ConnectionState::Error);
        emit vpnProtocolError(startError);
    }
}

void VpnConnection::createProtocolConnections()
{
    connect(m_vpnProtocol.data(), &VpnProtocol::protocolError, this, &VpnConnection::vpnProtocolError);
    const auto protocol = m_vpnProtocol.data();
    connect(protocol, &VpnProtocol::connectionStateChanged, this, [this, protocol](Vpn::ConnectionState state) {
        if (m_vpnProtocol.data() == protocol) setConnectionState(state);
    });
    connect(m_vpnProtocol.data(), SIGNAL(bytesChanged(quint64, quint64)), this, SLOT(onBytesChanged(quint64, quint64)));

#ifdef AMNEZIA_DESKTOP
    IpcClient::withInterface([this](QSharedPointer<IpcInterfaceReplica> rep) {
        connect(rep.data(), &IpcInterfaceReplica::networkChanged, this, &VpnConnection::reconnectToVpn, static_cast<Qt::ConnectionType>(Qt::QueuedConnection | Qt::UniqueConnection));
        connect(rep.data(), &IpcInterfaceReplica::wakeup, this, &VpnConnection::reconnectToVpn, static_cast<Qt::ConnectionType>(Qt::QueuedConnection | Qt::UniqueConnection));
    });
#endif
}

void VpnConnection::appendKillSwitchConfig()
{
    if (!m_appSettingsRepository) {
        qCritical() << "VpnConnection::appendKillSwitchConfig: repositories not initialized";
        return;
    }

    m_vpnConfiguration.insert(configKey::killSwitchOption, QVariant(m_appSettingsRepository->isKillSwitchEnabled()).toString());
    m_vpnConfiguration.insert(configKey::allowedDnsServers, QVariant(m_appSettingsRepository->getAllowedDnsServers()).toJsonValue());
}

void VpnConnection::appendSplitTunnelingConfig()
{
    if (!m_appSettingsRepository) {
        qCritical() << "VpnConnection::appendSplitTunnelingConfig: repositories not initialized";
        return;
    }

    bool allowSiteBasedSplitTunneling = true;

    // this block is for old native configs and for old self-hosted configs
    auto protocolName = m_vpnConfiguration.value(configKey::vpnProto).toString();
    if (protocolName == ProtocolUtils::protoToString(Proto::Awg) || protocolName == ProtocolUtils::protoToString(Proto::WireGuard)) {
        allowSiteBasedSplitTunneling = false;
        auto configData = m_vpnConfiguration.value(protocolName + "_config_data").toObject();
        if (configData.value(configKey::allowedIps).isString()) {
            QJsonArray allowedIpsJsonArray = QJsonArray::fromStringList(configData.value(configKey::allowedIps).toString().split(", "));
            configData.insert(configKey::allowedIps, allowedIpsJsonArray);
            m_vpnConfiguration.insert(protocolName + "_config_data", configData);
        } else if (configData.value(configKey::allowedIps).isUndefined()) {
            auto nativeConfig = configData.value(configKey::config).toString();
            auto nativeConfigLines = nativeConfig.split("\n");
            for (auto &line : nativeConfigLines) {
                auto allowedIpsString = line.split("=", Qt::KeepEmptyParts);
                if (allowedIpsString.size() >= 2 && allowedIpsString.first().trimmed() == QStringLiteral("AllowedIPs")) {
                    QJsonArray allowedIpsJsonArray;
                    const QString allowedIps = allowedIpsString.mid(1).join(QStringLiteral("=")).trimmed();
                    for (const QString &allowedIp : allowedIps.split(",", Qt::SkipEmptyParts)) {
                        allowedIpsJsonArray.append(allowedIp.trimmed());
                    }
                    configData.insert(configKey::allowedIps, allowedIpsJsonArray);
                    m_vpnConfiguration.insert(protocolName + "_config_data", configData);
                    break;
                }
            }
        }

        if (configData.value(configKey::persistentKeepAlive).isUndefined()) {
            auto nativeConfig = configData.value(configKey::config).toString();
            auto nativeConfigLines = nativeConfig.split("\n");
            for (auto &line : nativeConfigLines) {
                auto persistentKeepaliveString = line.split("=", Qt::KeepEmptyParts);
                if (persistentKeepaliveString.size() >= 2
                        && persistentKeepaliveString.first().trimmed() == QStringLiteral("PersistentKeepalive")) {
                    configData.insert(configKey::persistentKeepAlive,
                                      persistentKeepaliveString.mid(1).join(QStringLiteral("=")).trimmed());
                    m_vpnConfiguration.insert(protocolName + "_config_data", configData);
                    break;
                }
            }
        }

        QJsonArray allowedIpsJsonArray = configData.value(configKey::allowedIps).toArray();
        if (allowedIpsJsonArray.contains("0.0.0.0/0") && allowedIpsJsonArray.contains("::/0")) {
            allowSiteBasedSplitTunneling = true;
        }
    }

    amnezia::RouteMode routeMode = amnezia::RouteMode::VpnAllSites;
    QJsonArray sitesJsonArray;
    if (m_splitSnapshot.value("sitesEnabled").toBool()) {
        routeMode = static_cast<RouteMode>(m_splitSnapshot.value("siteMode").toInt());

        if (allowSiteBasedSplitTunneling) {
            QStringList sites;
            const QVariantMap m = SplitTunnelSettings::sites(m_splitSnapshot, routeMode).toVariantMap();
            for (auto i = m.constBegin(); i != m.constEnd(); ++i) {
                if (NetworkUtilities::checkIpSubnetFormat(i.key())) {
                    sites.append(i.key());
                } else {
                    const QStringList siteIps = SecureAppSettingsRepository::siteIpList(i.value());
                    for (const QString &ip : siteIps) {
                        if (NetworkUtilities::checkIpSubnetFormat(ip)) {
                            sites.append(ip);
                        }
                    }
                }
            }
            sites.removeDuplicates();
            for (const auto &site : sites) {
                sitesJsonArray.append(site);
            }

            if (sitesJsonArray.isEmpty()) {
                routeMode = amnezia::RouteMode::VpnAllSites;
            } else if (routeMode == amnezia::RouteMode::VpnOnlyForwardSites) {
                // Allow traffic to Amnezia DNS
                sitesJsonArray.append(m_vpnConfiguration.value(configKey::dns1).toString());
                sitesJsonArray.append(m_vpnConfiguration.value(configKey::dns2).toString());
            }
        }
    }

    m_vpnConfiguration.insert(configKey::splitTunnelType, routeMode);
    m_vpnConfiguration.insert(configKey::splitTunnelSites, sitesJsonArray);

    amnezia::AppsRouteMode appsRouteMode = amnezia::AppsRouteMode::VpnAllApps;
    QJsonArray appsJsonArray;
    if (m_splitSnapshot.value("appsEnabled").toBool()) {
        appsRouteMode = static_cast<AppsRouteMode>(m_splitSnapshot.value("appMode").toInt());

        auto apps = SplitTunnelSettings::apps(m_splitSnapshot, appsRouteMode);
        for (const auto &app : apps) {
            appsJsonArray.append(app);
        }

        if (appsJsonArray.isEmpty()) {
            appsRouteMode = amnezia::AppsRouteMode::VpnAllApps;
        }
    }

    m_vpnConfiguration.insert(configKey::appSplitTunnelType, appsRouteMode);
    m_vpnConfiguration.insert(configKey::splitTunnelApps, appsJsonArray);

    qDebug() << QString("Site split tunneling is %1, route mode is %2")
                        .arg(m_splitSnapshot.value("sitesEnabled").toBool() ? "enabled" : "disabled")
                        .arg(routeMode);
    qDebug() << QString("App split tunneling is %1, route mode is %2")
                        .arg(m_splitSnapshot.value("appsEnabled").toBool() ? "enabled" : "disabled")
                        .arg(appsRouteMode);
}

#ifdef Q_OS_ANDROID
void VpnConnection::restoreConnection()
{
    createAndroidConnections();

    m_vpnProtocol.reset(androidVpnProtocol);

    createProtocolConnections();
}

void VpnConnection::createAndroidConnections()
{
    androidVpnProtocol = createDefaultAndroidVpnProtocol();

    connect(AndroidController::instance(), &AndroidController::connectionStateChanged, androidVpnProtocol,
            &AndroidVpnProtocol::setConnectionState);
    connect(AndroidController::instance(), &AndroidController::statisticsUpdated, androidVpnProtocol, &AndroidVpnProtocol::setBytesChanged);
}

AndroidVpnProtocol *VpnConnection::createDefaultAndroidVpnProtocol()
{
    return new AndroidVpnProtocol(m_vpnConfiguration);
}
#endif

QString VpnConnection::bytesPerSecToText(quint64 bytes)
{
    double mbps = bytes * 8 / 1e6;
    return QString("%1 %2").arg(QString::number(mbps, 'f', 2)).arg(tr("Mbps")); // Mbit/s
}

void VpnConnection::reconnectToVpn() {
    if (m_vpnProtocol.isNull())
        return;

    if (m_connectionState != Vpn::ConnectionState::Connected) {
        qWarning() << QString("Reconnect triggered on %1 during inappropriate state: %2; ignoring slot")
                              .arg(QMetaEnum::fromType<Vpn::ConnectionState>().valueToKey(m_connectionState));
        return;
    }

    qDebug() << "Reconnect triggered. Reconnecting to the server";

    // Automatic recovery retains the existing protocol config and rule snapshot.
    ++m_routeGeneration;
    m_restartAfterDisconnect = true;
    setConnectionState(Vpn::ConnectionState::Reconnecting);
    m_vpnProtocol->stop();
}

void VpnConnection::disconnectFromVpn()
{
    ++m_routeGeneration;
    m_restartAfterDisconnect = false;
#if defined(Q_OS_IOS) || defined(MACOS_NE)
    // iOS/macOS NE use IosController directly; m_vpnProtocol is not set there.
    IosController::Instance()->disconnectVpn();
    disconnect(&m_checkTimer, &QTimer::timeout, IosController::Instance(), &IosController::checkStatus);
    return;
#endif

    if (m_vpnProtocol.isNull() || m_vpnProtocol->isDisconnected()) {
        setConnectionState(Vpn::ConnectionState::Disconnected);
        return;
    }

    setConnectionState(Vpn::ConnectionState::Disconnecting);

#ifdef Q_OS_ANDROID
    auto *const connection = new QMetaObject::Connection;
    *connection = connect(AndroidController::instance(), &AndroidController::vpnStateChanged, this,
                          [this, connection](AndroidController::ConnectionState state) {
                              if (state == AndroidController::ConnectionState::DISCONNECTED) {
                                  setConnectionState(Vpn::ConnectionState::Disconnected);
                                  disconnect(*connection);
                                  delete connection;
                              }
                          });
#endif

    m_vpnProtocol->stop();

    // Keep the protocol alive until its asynchronous Disconnected signal.
    // The next explicit connection replaces it after teardown has completed.
}

void VpnConnection::setConnectionState(Vpn::ConnectionState state) {
    if (state == Vpn::Disconnected || state == Vpn::Error || state == Vpn::Disconnecting)
        ++m_routeGeneration;
    if (state == Vpn::Disconnecting && m_restartAfterDisconnect) return;
    const auto generationAtEntry = m_routeGeneration;
    if (!(state == Vpn::Disconnected && m_restartAfterDisconnect)) m_connectionState = state;
    onConnectionStateChanged(state);
    if (generationAtEntry != m_routeGeneration) return;

    if (state == Vpn::Disconnected && m_restartAfterDisconnect) {
        m_restartAfterDisconnect = false;
        const auto generation = m_routeGeneration;
        QMetaObject::invokeMethod(this, [this, generation] {
            if (generation != m_routeGeneration || !m_vpnProtocol || m_connectionState != Vpn::Reconnecting) return;
            if (const auto error = m_vpnProtocol->start(); error != ErrorCode::NoError) {
                m_startError = error;
                setConnectionState(Vpn::Error);
                emit vpnProtocolError(error);
            }
        }, Qt::QueuedConnection);
        return;
    }
    if (state == Vpn::Error || state == Vpn::Unknown) m_restartAfterDisconnect = false;
    m_connectionState = state;
    if (state == Vpn::Connected && !m_splitSnapshot.isEmpty()) emit splitTunnelSnapshotConnected(m_splitSnapshot);
    emit connectionStateChanged(state);
}
