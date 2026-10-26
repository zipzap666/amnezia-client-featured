#pragma once
#include <QJsonObject>
#include <QString>

// No network effects during construction. A journal survives a GUI crash.
class WindowsSystemProxyManager {
public:
    explicit WindowsSystemProxyManager(QString journalPath = {});
    virtual ~WindowsSystemProxyManager() = default;
    bool apply(int port);
    bool restore();
    bool pending() const;
    bool owns(int port) const;
    bool ownsInstalledSettings() const;
    QString status() const;
    QString error() const { return m_error; }
protected:
    virtual bool read(QJsonObject &value) const;
    virtual bool write(const QJsonObject &value);
private:
    bool journal(const QJsonObject &value);
    QJsonObject load() const;
    QString m_path, m_error;
};
