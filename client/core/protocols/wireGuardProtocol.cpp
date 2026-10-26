#include <QCoreApplication>
#include <QFileInfo>
#include <QProcess>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>

#include "wireGuardProtocol.h"
#include "core/utils/networkUtilities.h"

#include "mozilla/localsocketcontroller.h"
#include "daemon/daemonerrors.h"

WireguardProtocol::WireguardProtocol(const QJsonObject &configuration, QObject *parent)
    : VpnProtocol(configuration, parent)
{
    m_impl.reset(new LocalSocketController());
#ifdef Q_OS_WIN
    auto *statisticsTimer = new QTimer(this);
    statisticsTimer->setInterval(1000);
    connect(this, &VpnProtocol::connectionStateChanged, this, [this] { ++m_statisticsEpoch; });
    connect(statisticsTimer, &QTimer::timeout, this, [this] {
        if (m_connectionState == Vpn::Connected && !m_statisticsPending) {
            m_statisticsPending = true;
            m_statisticsRequestEpoch = m_statisticsEpoch;
            m_statisticsRequest.start();
            m_impl->checkStatus();
        }
    });
    statisticsTimer->start();
#endif
    connect(m_impl.get(), &ControllerImpl::connected, this,
            [this](const QString &pubkey, const QDateTime &connectionTimestamp) {
                setConnectionState(Vpn::ConnectionState::Connected);
            });
    connect(m_impl.get(), &ControllerImpl::statusUpdated, this,
            [this](const QString& serverIpv4Gateway,
                   const QString& deviceIpv4Address, uint64_t txBytes,
                   uint64_t rxBytes) {
#ifdef Q_OS_WIN
                // The Windows daemon returns cumulative UAPI peer counters.
                // Ignore LocalSocketController's synthetic address-only update.
                if (m_statisticsPending && !serverIpv4Gateway.isEmpty()) {
                    m_statisticsPending = false;
                    if (m_connectionState == Vpn::Connected && m_statisticsRequestEpoch == m_statisticsEpoch
                        && m_statisticsRequest.elapsed() <= 3000)
                        emit trafficCounters(rxBytes, txBytes);
                }
#endif
                const QString previousGateway = m_vpnGateway;
                const QString previousLocal = m_vpnLocalAddress;

                if (!serverIpv4Gateway.isEmpty()) {
                    m_vpnGateway = serverIpv4Gateway;
                }
                if (!deviceIpv4Address.isEmpty()) {
                    m_vpnLocalAddress = deviceIpv4Address;
                }

                if ((!m_vpnGateway.isEmpty() && m_vpnGateway != previousGateway) ||
                    (!m_vpnLocalAddress.isEmpty() && m_vpnLocalAddress != previousLocal)) {
                    emit tunnelAddressesUpdated(m_vpnGateway, m_vpnLocalAddress);
                }
            });

    connect(m_impl.get(), &ControllerImpl::disconnected, this,
            [this]() {
#ifdef Q_OS_WIN
                // The disconnect acknowledgement drains earlier socket replies.
                m_statisticsPending = false;
#endif
                if (m_connectionState == Vpn::Connecting || m_connectionState == Vpn::Reconnecting)
                    setLastError(ErrorCode::AmneziaServiceConnectionFailed);
                else
                    setConnectionState(Vpn::ConnectionState::Disconnected);
            });
    connect(static_cast<LocalSocketController *>(m_impl.get()), &LocalSocketController::backendError, this, [this](int code) {
        switch (static_cast<DaemonError>(code)) {
        case DaemonError::ERROR_SPLIT_TUNNEL_INIT_FAILURE: setLastError(ErrorCode::AppSplitTunnelInitializationFailed); break;
        case DaemonError::ERROR_SPLIT_TUNNEL_START_FAILURE: setLastError(ErrorCode::AppSplitTunnelStartFailed); break;
        case DaemonError::ERROR_SPLIT_TUNNEL_EXCLUDE_FAILURE: setLastError(ErrorCode::AppSplitTunnelRulesFailed); break;
        default: setLastError(ErrorCode::AmneziaServiceConnectionFailed); break;
        }
    });
    m_impl->initialize(nullptr, nullptr);
}

WireguardProtocol::~WireguardProtocol()
{
    WireguardProtocol::stop();
    QThread::msleep(200);
}

void WireguardProtocol::stop()
{
    if (m_connectionState != Vpn::Disconnected) setConnectionState(Vpn::Disconnecting);
    stopMzImpl();
    return;
}

ErrorCode WireguardProtocol::startMzImpl()
{
    QString protocolName = m_rawConfig.value("protocol").toString();
    QJsonObject vpnConfigData = m_rawConfig.value(protocolName + "_config_data").toObject();
    vpnConfigData[configKey::hostName] = NetworkUtilities::getIPAddress(vpnConfigData.value(configKey::hostName).toString());
    m_rawConfig.insert(protocolName + "_config_data", vpnConfigData);
    m_rawConfig[configKey::hostName] = NetworkUtilities::getIPAddress(m_rawConfig[configKey::hostName].toString());

    m_impl->activate(m_rawConfig);
    return ErrorCode::NoError;
}

ErrorCode WireguardProtocol::stopMzImpl()
{
    m_impl->deactivate();
    return ErrorCode::NoError;
}


ErrorCode WireguardProtocol::start()
{
    setConnectionState(Vpn::Connecting);
    return startMzImpl();
}
