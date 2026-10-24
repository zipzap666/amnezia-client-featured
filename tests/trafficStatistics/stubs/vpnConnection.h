#pragma once
#include <QObject>
#include <QSharedPointer>
namespace Vpn { enum ConnectionState { Disconnected, Connecting, Connected, Reconnecting }; }
class VpnProtocol : public QObject {
    Q_OBJECT
signals:
    void trafficCounters(quint64, quint64);
};
class VpnConnection : public QObject {
    Q_OBJECT
public:
    QSharedPointer<VpnProtocol> protocol {new VpnProtocol};
    QSharedPointer<VpnProtocol> vpnProtocol() const { return protocol; }
signals:
    void connectionStateChanged(Vpn::ConnectionState);
};
