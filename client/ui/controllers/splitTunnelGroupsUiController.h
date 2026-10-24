#pragma once

#include <QObject>
#include <QVariantList>
#include <functional>
#include "core/repositories/secureAppSettingsRepository.h"

class SplitTunnelGroupsUiController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList groups READ groups NOTIFY changed)
public:
    SplitTunnelGroupsUiController(SecureAppSettingsRepository *repository, bool apps,
                                 std::function<int()> mode, QObject *parent);
    QVariantList groups() const;
    QJsonObject state() const;
    Q_INVOKABLE QString createGroup(const QString &name);
    Q_INVOKABLE bool renameGroup(const QString &id, const QString &name);
    Q_INVOKABLE void removeGroup(const QString &id);
    Q_INVOKABLE void setGroupEnabled(const QString &id, bool enabled);
    Q_INVOKABLE void setRuleEnabled(const QString &key, bool enabled);
    Q_INVOKABLE void moveRule(const QString &key, const QString &id);
signals:
    void changed();
private:
    void updateRule(const QString &key, const QString &field, const QJsonValue &value);
    SecureAppSettingsRepository *m_repository;
    bool m_apps;
    std::function<int()> m_mode;
};
