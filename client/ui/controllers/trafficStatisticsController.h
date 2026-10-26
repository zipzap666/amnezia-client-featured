#pragma once

#include <QObject>
#include <QElapsedTimer>
#include <QTimer>
#include <QFutureWatcher>
#include "trafficSeries.h"

class VpnConnection;
class ProxyConnection;
class TrafficStatisticsController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap vpn READ vpn NOTIFY changed)
    Q_PROPERTY(QVariantMap physical READ physical NOTIFY changed)
    Q_PROPERTY(QVariantMap proxy READ proxy NOTIFY changed)
    Q_PROPERTY(QVariantMap proxyDirect READ proxyDirect NOTIFY changed)
    Q_PROPERTY(qint64 now READ now NOTIFY changed)
public:
    explicit TrafficStatisticsController(VpnConnection *connection, QObject *parent = nullptr);
    QVariantMap vpn() const { return m_vpn.data(now()); }
    QVariantMap physical() const { return m_physical.data(now()); }
    QVariantMap proxy() const { return m_proxy.data(now()); }
    QVariantMap proxyDirect() const { return m_proxyDirect.data(now()); }
    void attachProxy(ProxyConnection *connection);
    qint64 now() const { return m_clock.elapsed(); }
    Q_INVOKABLE void reset();
signals:
    void changed();
private:
    struct Snapshot {
        bool valid = false;
        quint64 rx = 0, tx = 0;
        qint64 time = 0;
        QString identity;
        QMap<quint64, QPair<quint64, quint64>> adapters;
    };
    static Snapshot readPhysical();
    void poll();
    TrafficSeries m_vpn, m_physical, m_proxy, m_proxyDirect;
    quint64 m_proxyGeneration = 0;
    QElapsedTimer m_clock;
    QTimer m_timer;
    QFutureWatcher<Snapshot> m_watcher;
    QMetaObject::Connection m_bytesConnection;
    QMap<quint64, QPair<quint64, quint64>> m_adapters;
    quint64 m_generation = 0, m_resetGeneration = 0;
};
