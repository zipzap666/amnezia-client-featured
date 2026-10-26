#include "proxyConnection.h"
#include "ui/controllers/trafficStatisticsController.h"

void TrafficStatisticsController::attachProxy(ProxyConnection *connection)
{
    connect(connection, &ProxyConnection::stateChanged, this, [this](Vpn::ConnectionState state) {
        if (state == Vpn::Connected || state == Vpn::Disconnected || state == Vpn::Error) {
            ++m_proxyGeneration; m_proxy = {}; m_proxyDirect = {}; emit changed();
        }
    });
    connect(connection, &ProxyConnection::trafficCounters, this, [this](quint64 rx, quint64 tx, quint64 directRx, quint64 directTx) {
        const auto id = QString::number(m_proxyGeneration);
        m_proxy.update(rx, tx, now(), id); m_proxyDirect.update(directRx, directTx, now(), id); emit changed();
    });
}
