#include "splitTunnelGroupsUiController.h"
#include "core/utils/splitTunnelGroups.h"
#include <QUuid>

SplitTunnelGroupsUiController::SplitTunnelGroupsUiController(SecureAppSettingsRepository *repository,
    bool apps, std::function<int()> mode, QObject *parent)
    : QObject(parent), m_repository(repository), m_apps(apps), m_mode(std::move(mode))
{
    if (apps) {
        connect(repository, &SecureAppSettingsRepository::appsChanged, this, [this](AppsRouteMode mode) {
            if (int(mode) == m_mode()) emit changed();
        });
        connect(repository, &SecureAppSettingsRepository::appsRouteModeChanged, this, [this] { emit changed(); });
    } else {
        connect(repository, &SecureAppSettingsRepository::sitesChanged, this, [this](RouteMode mode) {
            if (int(mode) == m_mode()) emit changed();
        });
        connect(repository, &SecureAppSettingsRepository::routeModeChanged, this, [this] { emit changed(); });
    }
}
QJsonObject SplitTunnelGroupsUiController::state() const { return m_repository->splitTunnelGroups(m_apps, m_mode()); }
QVariantList SplitTunnelGroupsUiController::groups() const {
    QVariantList result { QVariantMap {{"id", ""}, {"name", tr("Ungrouped")}, {"enabled", state().value("ungroupedEnabled").toBool(true)}} };
    for (const auto &entry : state().value("groups").toArray()) result.append(entry.toObject().toVariantMap());
    return result;
}
QString SplitTunnelGroupsUiController::createGroup(const QString &name) {
    const auto trimmed = name.trimmed().left(80);
    if (trimmed.isEmpty()) return {};
    auto data = state();
    auto groups = data.value("groups").toArray();
    for (const auto &entry : groups)
        if (entry.toObject().value("name").toString().compare(trimmed, Qt::CaseInsensitive) == 0) return {};
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    groups.append(QJsonObject {{"id", id}, {"name", trimmed}, {"enabled", true}});
    data.insert("groups", groups);
    m_repository->setSplitTunnelGroups(m_apps, m_mode(), data);
    return id;
}
bool SplitTunnelGroupsUiController::renameGroup(const QString &id, const QString &name) {
    if (id.isEmpty() || name.trimmed().isEmpty()) return false;
    auto data = state(); auto groups = data.value("groups").toArray();
    const auto trimmed = name.trimmed().left(80);
    for (const auto &entry : groups) {
        const auto group = entry.toObject();
        if (group.value("id").toString() != id && group.value("name").toString().compare(trimmed, Qt::CaseInsensitive) == 0) return false;
    }
    for (int i = 0; i < groups.size(); ++i) {
        auto group = groups[i].toObject();
        if (group.value("id").toString() == id) { group.insert("name", trimmed); groups[i] = group; }
    }
    data.insert("groups", groups); m_repository->setSplitTunnelGroups(m_apps, m_mode(), data);
    return true;
}
void SplitTunnelGroupsUiController::removeGroup(const QString &id) {
    if (id.isEmpty()) return;
    auto data = state(); auto groups = data.value("groups").toArray();
    for (int i = groups.size() - 1; i >= 0; --i) if (groups[i].toObject().value("id").toString() == id) groups.removeAt(i);
    auto rules = data.value("rules").toObject();
    for (const auto &key : rules.keys()) {
        auto rule = rules.value(key).toObject();
        if (rule.value("groupId").toString() == id) { rule.insert("groupId", ""); rules.insert(key, rule); }
    }
    data.insert("groups", groups); data.insert("rules", rules);
    m_repository->setSplitTunnelGroups(m_apps, m_mode(), data);
}
void SplitTunnelGroupsUiController::setGroupEnabled(const QString &id, bool enabled) {
    auto data = state(); auto groups = data.value("groups").toArray();
    if (id.isEmpty()) data.insert("ungroupedEnabled", enabled);
    for (int i = 0; i < groups.size(); ++i) {
        auto group = groups[i].toObject();
        if (group.value("id").toString() == id) { group.insert("enabled", enabled); groups[i] = group; }
    }
    data.insert("groups", groups); m_repository->setSplitTunnelGroups(m_apps, m_mode(), data);
}
void SplitTunnelGroupsUiController::updateRule(const QString &key, const QString &field, const QJsonValue &value) {
    auto data = state(); auto rules = data.value("rules").toObject();
    const auto canonical = SplitTunnelGroups::key(key, m_apps);
    auto rule = rules.value(canonical).toObject(); rule.insert(field, value); rules.insert(canonical, rule);
    data.insert("rules", rules); m_repository->setSplitTunnelGroups(m_apps, m_mode(), data);
}
void SplitTunnelGroupsUiController::setRuleEnabled(const QString &key, bool enabled) { updateRule(key, "enabled", enabled); }
void SplitTunnelGroupsUiController::moveRule(const QString &key, const QString &id) {
    bool found = id.isEmpty();
    for (const auto &group : groups()) if (group.toMap().value("id").toString() == id) found = true;
    if (found) updateRule(key, "groupId", id);
}
