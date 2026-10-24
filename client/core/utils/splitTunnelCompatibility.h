#pragma once

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QUuid>
#include <QVariant>
#include <functional>
#include "splitTunnelGroups.h"

// The official client reads only legacyKey(). The full editable list, group
// metadata and last published projection stay under Conf for backup/restore.
// Reconciliation runs at startup/restore, never while editing or connecting.
class SplitTunnelCompatibility
{
public:
    using Read = std::function<QVariant(const QString &)>;
    using Write = std::function<void(const QString &, const QVariant &)>;
    SplitTunnelCompatibility(Read read, Write write) : m_read(std::move(read)), m_write(std::move(write)) {}

    static QString legacyKey(bool apps, int mode) {
        if (mode < 0 || mode > 2) return QStringLiteral("Conf/");
        static const char *sites[] = {"AllSites", "ForwardSites", "ExceptSites"};
        static const char *applications[] = {"AllApps", "ForwardApps", "ExceptApps"};
        return "Conf/" + QString::fromLatin1((apps ? applications : sites)[mode]);
    }
    static QString fullKey(bool apps, int mode) { return key("lists/v1", apps, mode); }
    static QString mirrorKey(bool apps, int mode) { return key("published/v1", apps, mode); }
    static QString pendingKey(bool apps, int mode) { return key("publishing/v1", apps, mode); }
    static QString groupsKey(bool apps, int mode) { return key("", apps, mode); }

    QVariant full(bool apps, int mode) const {
        auto stored = m_read(fullKey(apps, mode));
        return normalized(apps, stored.isValid() ? stored : m_read(legacyKey(apps, mode)));
    }
    QJsonObject groups(bool apps, int mode) const { return m_read(groupsKey(apps, mode)).toJsonObject(); }

    void writeList(bool apps, int mode, const QVariant &list) const {
        const auto data = normalized(apps, list);
        auto state = groups(apps, mode);
        QStringList names;
        if (apps) {
            for (const auto &entry : data.toJsonArray()) names.append(appPath(entry.toObject()));
        } else names = data.toMap().keys();
        state = SplitTunnelGroups::prune(state, names, apps);
        commit(apps, mode, data, state);
    }
    void writeGroups(bool apps, int mode, const QJsonObject &state) const {
        const auto data = full(apps, mode);
        // Save a full copy before a first projection can hide disabled records.
        commit(apps, mode, data, state);
    }
    void initialize() const {
        for (bool apps : {false, true}) {
            for (int mode = 0; mode <= 2; ++mode) {
                const auto stored = m_read(fullKey(apps, mode));
                auto data = full(apps, mode);
                auto state = groups(apps, mode);
                if (stored.isValid() && !m_read(pendingKey(apps, mode)).toBool()) {
                    const auto previous = m_read(mirrorKey(apps, mode));
                    const auto baseline = previous.isValid() ? normalized(apps, previous) : project(apps, data, state);
                    const auto current = normalized(apps, m_read(legacyKey(apps, mode)));
                    if (entries(apps, current) != entries(apps, baseline))
                        data = reconcile(apps, data, baseline, current, state);
                }
                commit(apps, mode, data, state);
            }
        }
    }

    static QVariant project(bool apps, const QVariant &data, const QJsonObject &state) {
        if (apps) {
            QJsonArray result;
            QSet<QString> seen;
            for (const auto &entry : normalized(true, data).toJsonArray()) {
                const auto path = appPath(entry.toObject());
                const auto canonical = SplitTunnelGroups::key(path, true);
                if (!path.isEmpty() && !seen.contains(canonical) && SplitTunnelGroups::active(state, path, true)) {
                    result.append(entry);
                    seen.insert(canonical);
                }
            }
            return result;
        }
        auto sites = data.toMap();
        for (const auto &site : sites.keys()) if (!SplitTunnelGroups::active(state, site, false)) sites.remove(site);
        return sites;
    }

private:
    using Entries = QMap<QString, QJsonObject>;
    static QString key(const QString &part, bool apps, int mode) {
        return QString("Conf/splitGroups/%1%2/%3").arg(part.isEmpty() ? "" : part + "/")
            .arg(apps ? "apps" : "sites").arg(mode);
    }
    static QVariant normalized(bool apps, const QVariant &value) {
        if (!apps) return value.toMap();
        if (value.metaType() == QMetaType::fromType<QJsonArray>()) return value;
        return QJsonArray::fromVariantList(value.toList());
    }
    static QString appPath(const QJsonObject &entry) {
        const auto path = entry.value("appPath").toString();
        return path.isEmpty() ? entry.value("packageName").toString() : path;
    }
    static Entries entries(bool apps, const QVariant &data) {
        Entries result;
        if (apps) {
            for (const auto &entry : normalized(true, data).toJsonArray()) {
                const auto item = entry.toObject();
                result.insert(SplitTunnelGroups::key(appPath(item), true), item);
            }
        } else {
            const auto sites = data.toMap();
            for (auto it = sites.begin(); it != sites.end(); ++it)
                result.insert(SplitTunnelGroups::key(it.key(), false), QJsonObject{
                    {"site", it.key()}, {"ips", QJsonValue::fromVariant(it.value())}});
        }
        return result;
    }
    static QVariant list(bool apps, const Entries &entries) {
        if (apps) {
            QJsonArray result;
            for (const auto &entry : entries) result.append(entry);
            return result;
        }
        QVariantMap result;
        for (const auto &entry : entries) result.insert(entry.value("site").toString(), entry.value("ips").toVariant());
        return result;
    }
    static QString enabledDestination(QJsonObject &state) {
        if (state.value("ungroupedEnabled").toBool(true)) return {};
        auto groups = state.value("groups").toArray();
        QSet<QString> names;
        for (const auto &entry : groups) {
            const auto group = entry.toObject();
            if (group.value("compatibilityImport").toBool() && group.value("enabled").toBool(true))
                return group.value("id").toString();
            names.insert(group.value("name").toString().toCaseFolded());
        }
        const auto base = QCoreApplication::translate("SplitTunnelCompatibility", "Imported rules");
        QString name = base;
        for (int n = 2; names.contains(name.toCaseFolded()); ++n) name = base + " (" + QString::number(n) + ")";
        const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        groups.append(QJsonObject{{"id", id}, {"name", name}, {"enabled", true}, {"compatibilityImport", true}});
        state.insert("groups", groups);
        return id;
    }
    static QVariant reconcile(bool apps, const QVariant &data, const QVariant &previous,
                              const QVariant &current, QJsonObject &state) {
        auto all = entries(apps, data);
        const auto before = entries(apps, previous);
        const auto after = entries(apps, current);
        auto rules = state.value("rules").toObject();
        for (auto it = before.begin(); it != before.end(); ++it) {
            if (!after.contains(it.key())) { all.remove(it.key()); rules.remove(it.key()); }
        }
        state.insert("rules", rules);
        for (auto it = after.begin(); it != after.end(); ++it) {
            all.insert(it.key(), it.value());
            if (!before.contains(it.key())) {
                // A record explicitly added in the official client must be active
                // on return, without re-enabling unrelated disabled groups/rules.
                const auto resolved = SplitTunnelGroups::rule(state, it.key(), apps);
                auto rule = rules.value(it.key()).toObject();
                rule.insert("enabled", true);
                rule.insert("groupId", resolved.value("groupEnabled").toBool()
                    ? resolved.value("groupId").toString() : enabledDestination(state));
                rules.insert(it.key(), rule);
                state.insert("rules", rules);
            }
        }
        return list(apps, all);
    }
    void commit(bool apps, int mode, const QVariant &data, const QJsonObject &state) const {
        // An interrupted projection is repaired from the full copy, rather than
        // mistaken for deletions made in the official client.
        m_write(pendingKey(apps, mode), true);
        m_write(fullKey(apps, mode), data);
        m_write(groupsKey(apps, mode), state);
        const auto flat = project(apps, data, state);
        m_write(legacyKey(apps, mode), flat);
        m_write(mirrorKey(apps, mode), flat);
        m_write(pendingKey(apps, mode), false);
    }
    Read m_read;
    Write m_write;
};
