#pragma once
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QJsonObject>
#include <QVariantMap>
#include <QElapsedTimer>
#include "core/protocols/vpnProtocol.h"
#include "windowsSystemProxyManager.h"

class ProxyConnection : public QObject {
    Q_OBJECT
public:
    explicit ProxyConnection(QObject *parent = nullptr, QString helperPath = {}, QString journalPath = {});
    ~ProxyConnection() override;
    void start(const QJsonObject &configuration, const QJsonObject &settings);
    bool stop();
    bool recoveryPending() const { return m_system.pending(); }
    bool running() const { return m_process.state() != QProcess::NotRunning; }
    QString status() const {
        if (!m_error.isEmpty()) return m_error + "\n" + m_system.status();
        if (m_system.pending() && !m_ready) return tr("System proxy is enabled; AWG is unavailable or reconnecting.");
        return m_system.status();
    }
    QString error() const { return m_error; }
    QVariantMap counters() const { return m_counters; }
signals:
    void stateChanged(Vpn::ConnectionState state);
    void changed();
    void failed(const QString &message);
    void trafficCounters(quint64 rx, quint64 tx, quint64 directRx, quint64 directTx);
private:
    void fail(const QString &message);
    void consume();
    void launch();
    void recover();
    QProcess m_process;
    QTimer m_timeout, m_networkDebounce;
    QTimer m_resumeTimer;
    QElapsedTimer m_lastTick;
    WindowsSystemProxyManager m_system;
    QJsonObject m_command, m_settings;
    QByteArray m_buffer;
    QString m_session, m_error;
    QString m_helperPath;
    QVariantMap m_counters;
    bool m_stopping = false, m_recovering = false, m_ready = false;
    void *m_job = nullptr;
};
