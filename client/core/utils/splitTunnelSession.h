#pragma once

#include <QDir>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <optional>

namespace SplitTunnelSettings {
inline constexpr auto configKey = "_splitTunnelSettings";
inline QJsonObject forProxy(QJsonObject snapshot)
{
    snapshot.remove("apps"); snapshot.remove("appsEnabled"); snapshot.remove("appMode");
    return snapshot;
}
inline QJsonObject sites(const QJsonObject &snapshot, int mode)
{
    return snapshot.value("sites").toObject().value(QString::number(mode)).toObject();
}
inline QJsonArray apps(const QJsonObject &snapshot, int mode)
{
    return snapshot.value("apps").toObject().value(QString::number(mode)).toArray();
}
inline QString siteKey(QString key)
{
    key = key.trimmed();
    if (key.contains('/')) {
        const auto subnet = QHostAddress::parseSubnet(key);
        if (subnet.second >= 0)
            return subnet.first.toString() + '/' + QString::number(subnet.second);
    }
    const QHostAddress address(key);
    if (!address.isNull())
        return address.toString();
    while (key.endsWith('.')) key.chop(1);
    return key.toCaseFolded();
}
inline QJsonObject canonical(QJsonObject snapshot)
{
    // Domain values are a DNS cache, not user rules. Explicit IP/CIDR keys
    // remain rules. Compare every stored list, irrespective of its active mode.
    QJsonObject siteLists, appLists;
    const auto rawSites = snapshot.value("sites").toObject();
    for (auto i = rawSites.begin(); i != rawSites.end(); ++i) {
        QSet<QString> keys;
        const auto entries = i.value().toObject();
        for (auto j = entries.begin(); j != entries.end(); ++j) keys.insert(siteKey(j.key()));
        auto sorted = keys.values(); sorted.sort();
        siteLists.insert(i.key(), QJsonArray::fromStringList(sorted));
    }
    const auto rawApps = snapshot.value("apps").toObject();
    for (auto i = rawApps.begin(); i != rawApps.end(); ++i) {
        QSet<QString> keys;
        for (const auto &value : i.value().toArray()) {
            QString key = value.toString();
#ifdef Q_OS_WIN
            key.replace('\\', '/');
            key = QDir::cleanPath(key).toCaseFolded();
#endif
            keys.insert(key);
        }
        auto sorted = keys.values(); sorted.sort();
        appLists.insert(i.key(), QJsonArray::fromStringList(sorted));
    }
    snapshot.insert("sites", siteLists);
    snapshot.insert("apps", appLists);
    return snapshot;
}
}

// Shared across pages; success commits the attempt's snapshot, never the latest
// settings. Errors/cancellation retain the last applied comparison baseline.
class SplitTunnelSession
{
public:
    void save(const QJsonObject &value) { m_saved = value; }
    void begin(const QJsonObject &value) {
        m_attempt = value;
        if (!m_baseline) m_baseline = value;
    }
    void connected(const QJsonObject &value) { m_baseline = value; }
    const QJsonObject &attempt() const { return m_attempt; }
    bool pending() const {
        return m_baseline && SplitTunnelSettings::canonical(m_saved) != SplitTunnelSettings::canonical(*m_baseline);
    }
    bool pendingForAttempt() const {
        return SplitTunnelSettings::canonical(m_saved) != SplitTunnelSettings::canonical(m_attempt);
    }
    bool requestReconnect(bool connected) {
        if (!connected || m_reconnect != Idle) return false;
        m_reconnect = WaitingForDisconnect;
        return true;
    }
    bool disconnected() {
        if (m_reconnect != WaitingForDisconnect) return false;
        m_reconnect = Starting;
        return true;
    }
    bool reconnecting() const { return m_reconnect != Idle; }
    void finishReconnect() { m_reconnect = Idle; }
private:
    QJsonObject m_saved, m_attempt;
    std::optional<QJsonObject> m_baseline;
    enum { Idle, WaitingForDisconnect, Starting } m_reconnect = Idle;
};
