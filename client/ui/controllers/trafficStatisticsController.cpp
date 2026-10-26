#include "trafficStatisticsController.h"
#include "vpnConnection.h"
#include <QtConcurrent>
#ifdef Q_OS_WIN
#include <winsock2.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>
#include <memory>
#endif

TrafficStatisticsController::TrafficStatisticsController(VpnConnection *connection, QObject *parent)
    : QObject(parent)
{
    m_clock.start();
#ifdef Q_OS_WIN
    connect(connection, &VpnConnection::connectionStateChanged, this, [this, connection](Vpn::ConnectionState state) {
        ++m_generation;
        disconnect(m_bytesConnection);
        m_vpn = {};
        if (state == Vpn::Connected && connection->vpnProtocol()) {
            const auto generation = m_generation;
            m_bytesConnection = connect(connection->vpnProtocol().get(), &VpnProtocol::trafficCounters,
                                         this, [this, generation](quint64 rx, quint64 tx) {
                if (generation != m_generation) return;
                m_vpn.update(rx, tx, now(), QString::number(generation));
                emit changed();
            });
        }
        emit changed();
    });
    m_timer.setInterval(1000);
    connect(&m_timer, &QTimer::timeout, this, &TrafficStatisticsController::poll);
    m_timer.start();
    poll();
#else
    Q_UNUSED(connection)
#endif
}

void TrafficStatisticsController::reset()
{
    ++m_resetGeneration;
    m_vpn = {};
    m_physical = {};
    m_proxy = {}; m_proxyDirect = {};
    m_adapters.clear();
    emit changed();
}


TrafficStatisticsController::Snapshot TrafficStatisticsController::readPhysical()
{
    Snapshot snapshot;
#ifdef Q_OS_WIN
    MIB_IF_TABLE2 *table = nullptr;
    if (GetIfTable2(&table) != NO_ERROR) return snapshot;
    const std::unique_ptr<MIB_IF_TABLE2, decltype(&FreeMibTable)> guard(table, FreeMibTable);
    QStringList identities;
    for (ULONG i = 0; i < table->NumEntries; ++i) {
        const auto &row = table->Table[i];
        if (!row.InterfaceAndOperStatusFlags.HardwareInterface || row.InterfaceAndOperStatusFlags.FilterInterface
            || row.OperStatus != IfOperStatusUp || row.Type == IF_TYPE_SOFTWARE_LOOPBACK
            || row.TunnelType != TUNNEL_TYPE_NONE) continue;
        identities.append(QString::number(row.InterfaceLuid.Value));
        snapshot.adapters.insert(row.InterfaceLuid.Value, {row.InOctets, row.OutOctets});
        snapshot.rx += row.InOctets;
        snapshot.tx += row.OutOctets;
    }
    identities.sort();
    snapshot.identity = identities.join(',');
    snapshot.valid = !identities.isEmpty();
#endif
    return snapshot;
}

void TrafficStatisticsController::poll()
{
    emit changed(); // Expire stale VPN values even when its source stops replying.
    if (m_watcher.isRunning()) return;
    disconnect(&m_watcher, nullptr, this, nullptr);
    const auto resetGeneration = m_resetGeneration;
    const auto started = now();
    connect(&m_watcher, &QFutureWatcher<Snapshot>::finished, this, [this, resetGeneration, started] {
        if (resetGeneration != m_resetGeneration) return;
        const auto result = m_watcher.result();
        if (!result.valid || now() - started > 3000) m_physical.invalidate();
        else {
            // A reset on one adapter can be hidden by growth on another.
            for (auto it = result.adapters.cbegin(); it != result.adapters.cend(); ++it) {
                const auto previous = m_adapters.constFind(it.key());
                if (previous != m_adapters.cend()
                    && (it.value().first < previous.value().first || it.value().second < previous.value().second))
                    m_physical.invalidate();
            }
            m_adapters = result.adapters;
            m_physical.update(result.rx, result.tx, result.time, result.identity);
        }
        emit changed();
    });
    // Copy the monotonic clock: the task never dereferences the controller.
    const auto clock = m_clock;
    m_watcher.setFuture(QtConcurrent::run([clock] {
        auto result = readPhysical();
        result.time = clock.elapsed();
        return result;
    }));
}
