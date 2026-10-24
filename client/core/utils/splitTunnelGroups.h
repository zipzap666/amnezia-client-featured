#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QDir>
#include "splitTunnelSession.h"

namespace SplitTunnelGroups {
inline QString key(QString value, bool apps) {
    if (!apps) return SplitTunnelSettings::siteKey(value);
#ifdef Q_OS_WIN
    value.replace('\\', '/');
    return QDir::cleanPath(value).toCaseFolded();
#else
    return value;
#endif
}
inline QJsonObject rule(const QJsonObject &state, const QString &value, bool apps) {
    auto result = state.value("rules").toObject().value(key(value, apps)).toObject();
    const auto groupId = result.value("groupId").toString();
    bool found = groupId.isEmpty();
    bool groupEnabled = groupId.isEmpty() ? state.value("ungroupedEnabled").toBool(true) : true;
    for (const auto &entry : state.value("groups").toArray()) {
        const auto group = entry.toObject();
        if (group.value("id").toString() == groupId) {
            found = true;
            groupEnabled = group.value("enabled").toBool(true);
            break;
        }
    }
    if (!found) {
        result.insert("groupId", "");
        groupEnabled = state.value("ungroupedEnabled").toBool(true);
    }
    result.insert("enabled", result.value("enabled").toBool(true));
    result.insert("groupEnabled", groupEnabled);
    return result;
}
inline bool active(const QJsonObject &state, const QString &value, bool apps) {
    const auto entry = rule(state, value, apps);
    return entry.value("enabled").toBool() && entry.value("groupEnabled").toBool();
}
inline QJsonObject prune(QJsonObject state, const QStringList &values, bool apps) {
    QSet<QString> keys;
    for (const auto &value : values) keys.insert(key(value, apps));
    auto rules = state.value("rules").toObject();
    for (const auto &name : rules.keys()) if (!keys.contains(name)) rules.remove(name);
    state.insert("rules", rules);
    return state;
}
}
