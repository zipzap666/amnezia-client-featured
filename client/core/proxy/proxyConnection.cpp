#include "proxyConnection.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkInformation>
#include <QUuid>
#include "core/utils/constants/configKeys.h"
#include "core/utils/splitTunnelSession.h"
#ifdef Q_OS_WIN
#include <windows.h>
#endif

ProxyConnection::ProxyConnection(QObject *parent, QString helperPath, QString journalPath)
    : QObject(parent), m_system(journalPath),
      m_helperPath(helperPath.isEmpty() ? QCoreApplication::applicationDirPath() + "/amnezia-awg-proxy.exe" : helperPath) {
    m_lastTick.start(); m_resumeTimer.setInterval(5000);
    connect(&m_resumeTimer, &QTimer::timeout, this, [this] {
        const bool resumed = m_lastTick.restart() > 20000;
        if (resumed && running() && !m_stopping) m_networkDebounce.start();
    });
    m_resumeTimer.start();
    m_timeout.setSingleShot(true); m_timeout.setInterval(30000);
    connect(&m_timeout, &QTimer::timeout, this, [this] { fail(tr("AWG proxy connection timed out.")); });
    m_networkDebounce.setSingleShot(true); m_networkDebounce.setInterval(1000);
    connect(&m_networkDebounce, &QTimer::timeout, this, &ProxyConnection::recover);
    QNetworkInformation::loadDefaultBackend();
    if (auto *network = QNetworkInformation::instance()) {
        connect(network, &QNetworkInformation::reachabilityChanged, this, [this] { if (running() && !m_stopping) m_networkDebounce.start(); });
        connect(network, &QNetworkInformation::transportMediumChanged, this, [this] { if (running() && !m_stopping) m_networkDebounce.start(); });
    }
    connect(&m_process, &QProcess::readyReadStandardOutput, this, &ProxyConnection::consume);
    connect(&m_process, &QProcess::readyReadStandardError, this, [this] { m_process.readAllStandardError(); });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (!m_stopping && !m_recovering && error == QProcess::FailedToStart) fail(tr("Cannot start the AWG proxy helper."));
    });
    connect(&m_process, &QProcess::finished, this, [this] {
        m_timeout.stop(); m_ready = false;
        if (!m_stopping && !m_recovering) fail(tr("AWG proxy stopped unexpectedly. System proxy settings are retained."));
    });
    connect(&m_process, &QProcess::started, this, [this] {
#ifdef Q_OS_WIN
        m_job = CreateJobObjectW(nullptr, nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        HANDLE process = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, DWORD(m_process.processId()));
        const bool ok = m_job && process && SetInformationJobObject(m_job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))
                        && AssignProcessToJobObject(m_job, process);
        if (process) CloseHandle(process);
        if (!ok) { fail(tr("Cannot supervise the AWG proxy helper.")); return; }
#endif
        m_process.write(QJsonDocument(m_command).toJson(QJsonDocument::Compact) + '\n');
    });
    // Explicit application exit restores Windows settings. A GUI crash retains them.
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this] { stop(); }, Qt::DirectConnection);
}
ProxyConnection::~ProxyConnection() {
    disconnect(this, nullptr, nullptr, nullptr);
    stop();
    disconnect(&m_process, nullptr, this, nullptr);
    if (running()) { m_process.kill(); m_process.waitForFinished(3000); }
#ifdef Q_OS_WIN
    if (m_job) CloseHandle(m_job);
#endif
}
void ProxyConnection::start(const QJsonObject &configuration, const QJsonObject &settings) {
    if (running()) { fail(tr("The previous proxy helper is still running.")); return; }
    const auto proto = configuration.value(amnezia::configKey::vpnProto).toString();
    const auto data = configuration.value(proto + "_config_data").toObject();
    auto native = data.value(amnezia::configKey::config).toString();
    if (native.isEmpty()) { fail(tr("The AWG profile has no native client configuration.")); return; }
    // Client protocol editors update structured fields. Overlay their effective
    // values on the original INI, while retaining unknown lines for strict rejection.
    QMap<QString, QString> overrides;
    const QMap<QString, QString> names{{"client_priv_key","PrivateKey"},{"client_ip","Address"},
        {"server_pub_key","PublicKey"},{"psk_key","PresharedKey"},{"persistent_keep_alive","PersistentKeepalive"},{"mtu","MTU"}};
    for (auto it = names.begin(); it != names.end(); ++it) {
        const auto v = data.value(it.key()).toString();
        if (!v.isEmpty()) overrides[it.value()] = v;
    }
    // AWG parameters use native field names in the existing protocol model.
    for (const auto &key : amnezia::configKey::awgProtocolKeys()) {
        if (data.contains(key)) overrides[key] = data.value(key).toString();
    }
    const auto allowed = data.value(amnezia::configKey::allowedIps);
    if (allowed.isArray()) {
        QStringList ips; for (const auto &ip : allowed.toArray()) ips.append(ip.toString());
        if (!ips.isEmpty()) overrides["AllowedIPs"] = ips.join(", ");
    }
    if (overrides.contains("Address") && !overrides["Address"].contains('/')) {
        overrides["Address"] += overrides["Address"].contains(':') ? "/128" : "/32";
    }
    QStringList lines = native.split('\n');
    QSet<QString> replaced;
    for (auto &line : lines) {
        const auto equal = line.indexOf('=');
        if (equal < 0) continue;
        const auto key = line.left(equal).trimmed();
        if (overrides.contains(key)) {
            // Preserve all native addresses, including IPv6; the legacy model
            // only has one clientIp and cannot represent the complete list.
            if (key != "Address") line = key + " = " + overrides[key];
            replaced.insert(key);
        }
    }
    // Insert missing interface options before [Peer], peer options at the end.
    const QSet<QString> peerFields{"PublicKey","PresharedKey","AllowedIPs","PersistentKeepalive"};
    int peerIndex = -1;
    for (int i = 0; i < lines.size(); ++i) if (lines[i].trimmed() == "[Peer]") { peerIndex = i; break; }
    if (peerIndex < 0) { fail(tr("The AWG profile has no peer.")); return; }
    for (auto it = overrides.begin(); it != overrides.end(); ++it) {
        if (replaced.contains(it.key()) || it.value().isEmpty()) continue;
        const auto line = it.key() + " = " + it.value();
        if (peerFields.contains(it.key())) lines.append(line); else lines.insert(peerIndex++, line);
    }
    native = lines.join('\n');
    m_settings = settings;
    const auto split = configuration.value(SplitTunnelSettings::configKey).toObject();
    QJsonArray sites;
    const auto entries = SplitTunnelSettings::sites(split, split.value("siteMode").toInt());
    for (auto it = entries.begin(); it != entries.end(); ++it) sites.append(it.key());
    m_command = {{"version", 1}, {"type", "start"}, {"native", native},
        {"dns", QJsonArray{configuration.value(amnezia::configKey::dns1), configuration.value(amnezia::configKey::dns2)}},
        {"httpPort", settings.value("httpPort")}, {"socksPort", settings.value("socksPort")},
        {"rules", QJsonObject{{"enabled", split.value("sitesEnabled")}, {"mode", split.value("siteMode")}, {"sites", sites}}}};
    launch();
}
void ProxyConnection::launch() {
    m_stopping = false; m_ready = false; m_error.clear(); m_counters.clear(); m_buffer.clear();
    m_session = QUuid::createUuid().toString(QUuid::WithoutBraces); m_command["session"] = m_session;
    emit stateChanged(Vpn::Connecting); emit changed();
#ifdef Q_OS_WIN
    if (m_job) { CloseHandle(m_job); m_job = nullptr; }
    m_process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) { args->flags |= CREATE_NO_WINDOW; });
#endif
    m_timeout.start();
    m_process.setProgram(m_helperPath);
    m_process.setArguments({}); m_process.start();
}
void ProxyConnection::consume() {
    m_buffer += m_process.readAllStandardOutput();
    if (m_buffer.size() > 1024*1024) { fail(tr("Invalid proxy helper response.")); return; }
    int pos;
    while ((pos = m_buffer.indexOf('\n')) >= 0) {
        const auto line = m_buffer.left(pos); m_buffer.remove(0, pos+1);
        const auto message = QJsonDocument::fromJson(line).object();
        if (message.value("session").toString() != m_session || m_stopping) continue;
        if (message.value("version").toInt() != 1) { fail(tr("Incompatible proxy helper.")); return; }
        const auto type = message.value("type").toString();
        if (type == "unavailable") {
            m_ready = false;
            emit stateChanged(Vpn::Reconnecting); emit changed();
        } else if (type == "ready" || type == "available") {
            m_timeout.stop();
            const int port = m_settings.value("httpPort").toInt();
            const bool automatic = m_settings.value("systemProxy").toBool();
            if (automatic && m_system.pending() && !m_system.ownsInstalledSettings()) {
                fail(tr("Windows proxy settings changed externally. Disconnect before applying them again.")); return;
            }
            if (!automatic || !m_system.owns(port)) {
                if (!m_system.restore() || (automatic && !m_system.apply(port))) { fail(m_system.error()); return; }
            }
            m_ready = true; emit stateChanged(Vpn::Connected); emit changed();
        } else if (type == "error") {
            fail(tr("AWG proxy: %1").arg(message.value("message").toString())); return;
        } else if (type == "counters" && m_ready) {
            m_counters = message.toVariantMap(); emit changed();
            emit trafficCounters(message["rx"].toInteger(), message["tx"].toInteger(), message["directRx"].toInteger(), message["directTx"].toInteger());
        }
    }
}
void ProxyConnection::fail(const QString &message) {
    m_error = message; m_ready = false; m_timeout.stop(); m_networkDebounce.stop();
    m_stopping = true;
    if (running()) { m_process.kill(); m_process.waitForFinished(3000); }
    emit stateChanged(Vpn::Error); emit changed(); emit failed(message);
}
bool ProxyConnection::stop() {
    m_networkDebounce.stop(); m_timeout.stop();
    if (!m_system.restore()) { m_error = m_system.error(); emit changed(); emit failed(m_error); return false; }
    m_stopping = true; m_ready = false; emit stateChanged(Vpn::Disconnecting);
    if (running()) {
        m_process.write(QJsonDocument(QJsonObject{{"version",1},{"session",m_session},{"type","stop"}}).toJson(QJsonDocument::Compact)+'\n');
        m_process.closeWriteChannel();
        if (!m_process.waitForFinished(3000)) { m_process.kill(); if (!m_process.waitForFinished(3000)) { fail(tr("Cannot stop the previous proxy helper.")); return false; } }
    }
    m_command = {}; m_counters.clear(); m_error.clear(); emit stateChanged(Vpn::Disconnected); emit changed(); return true;
}
void ProxyConnection::recover() {
    if (!running() || m_stopping) return;
    m_recovering = true; m_ready = false;
    emit stateChanged(Vpn::Reconnecting);
    m_process.kill();
    if (!m_process.waitForFinished(3000)) { m_recovering = false; fail(tr("Cannot restart the AWG proxy helper.")); return; }
    m_recovering = false; launch();
}
