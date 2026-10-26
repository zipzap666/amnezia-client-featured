#include "windowsSystemProxyManager.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>
#ifdef Q_OS_WIN
#include <windows.h>
#include <wininet.h>
#endif

WindowsSystemProxyManager::WindowsSystemProxyManager(QString path)
    : m_path(path.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                              + "/system-proxy-recovery.json" : path) {}
QJsonObject WindowsSystemProxyManager::load() const {
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}
bool WindowsSystemProxyManager::pending() const { return QFile::exists(m_path); }
bool WindowsSystemProxyManager::ownsInstalledSettings() const {
    QJsonObject current;
    const auto record=load();
    return record["version"].toInt()==1 && read(current) && current==record["installed"].toObject();
}
bool WindowsSystemProxyManager::owns(int port) const {
    QJsonObject current;
    const auto record = load();
    const auto expected = QString("http=127.0.0.1:%1;https=127.0.0.1:%1").arg(port);
    return record["version"].toInt() == 1 && read(current) && current == record["installed"].toObject()
           && current["server"].toString() == expected;
}
bool WindowsSystemProxyManager::journal(const QJsonObject &value) {
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    const auto bytes = QJsonDocument(value).toJson(QJsonDocument::Compact);
    return file.write(bytes) == bytes.size() && file.commit();
}
bool WindowsSystemProxyManager::read(QJsonObject &value) const {
#ifdef Q_OS_WIN
    INTERNET_PER_CONN_OPTIONW options[4] = {};
    options[0].dwOption = INTERNET_PER_CONN_FLAGS_UI;
    options[1].dwOption = INTERNET_PER_CONN_PROXY_SERVER;
    options[2].dwOption = INTERNET_PER_CONN_PROXY_BYPASS;
    options[3].dwOption = INTERNET_PER_CONN_AUTOCONFIG_URL;
    INTERNET_PER_CONN_OPTION_LISTW list = {};
    list.dwSize = sizeof(list); list.dwOptionCount = 4; list.pOptions = options;
    DWORD size = sizeof(list);
    if (!InternetQueryOptionW(nullptr, INTERNET_OPTION_PER_CONNECTION_OPTION, &list, &size)) return false;
    value = {{"flags", int(options[0].Value.dwValue)}};
    const char *names[] = {"server", "bypass", "pac"};
    for (int i = 1; i < 4; ++i) {
        value[names[i-1]] = QString::fromWCharArray(options[i].Value.pszValue ? options[i].Value.pszValue : L"");
        if (options[i].Value.pszValue) GlobalFree(options[i].Value.pszValue);
    }
    return true;
#else
    Q_UNUSED(value)
    return false;
#endif
}
bool WindowsSystemProxyManager::write(const QJsonObject &value) {
#ifdef Q_OS_WIN
    QString server = value["server"].toString(), bypass = value["bypass"].toString(), pac = value["pac"].toString();
    INTERNET_PER_CONN_OPTIONW options[4] = {};
    options[0].dwOption = INTERNET_PER_CONN_FLAGS; options[0].Value.dwValue = value["flags"].toInt();
    options[1].dwOption = INTERNET_PER_CONN_PROXY_SERVER; options[1].Value.pszValue = reinterpret_cast<wchar_t *>(server.data());
    options[2].dwOption = INTERNET_PER_CONN_PROXY_BYPASS; options[2].Value.pszValue = reinterpret_cast<wchar_t *>(bypass.data());
    options[3].dwOption = INTERNET_PER_CONN_AUTOCONFIG_URL; options[3].Value.pszValue = reinterpret_cast<wchar_t *>(pac.data());
    INTERNET_PER_CONN_OPTION_LISTW list = {};
    list.dwSize = sizeof(list); list.dwOptionCount = 4; list.pOptions = options;
    if (!InternetSetOptionW(nullptr, INTERNET_OPTION_PER_CONNECTION_OPTION, &list, sizeof(list))) return false;
    const bool notified = InternetSetOptionW(nullptr, INTERNET_OPTION_SETTINGS_CHANGED, nullptr, 0)
                          && InternetSetOptionW(nullptr, INTERNET_OPTION_REFRESH, nullptr, 0);
    QJsonObject actual;
    return notified && read(actual) && actual == value;
#else
    Q_UNUSED(value)
    return false;
#endif
}
bool WindowsSystemProxyManager::apply(int port) {
    m_error.clear();
    if (pending()) { m_error = QObject::tr("Restore the previous system proxy session first."); return false; }
    QJsonObject original;
    if (!read(original)) { m_error = QObject::tr("Cannot read Windows proxy settings."); return false; }
    const QJsonObject installed{{"flags", 2}, {"server", QString("http=127.0.0.1:%1;https=127.0.0.1:%1").arg(port)},
                                {"bypass", ""}, {"pac", ""}};
    QJsonObject record{{"version", 1}, {"original", original}, {"installed", installed}, {"stage", "prepared"}};
    if (!journal(record)) { m_error = QObject::tr("Cannot save system proxy recovery data."); return false; }
    if (!write(installed)) {
        m_error = QObject::tr("Cannot apply Windows proxy settings. Recovery may be required.");
        // Retain the journal if rollback cannot be verified.
        QJsonObject current;
        if (read(current) && (current == installed || current == original) && write(original)) QFile::remove(m_path);
        return false;
    }
    record["stage"] = "applied";
    if (!journal(record)) {
        m_error = QObject::tr("Cannot commit system proxy recovery data.");
        if (write(original)) QFile::remove(m_path);
        return false;
    }
    return true;
}
bool WindowsSystemProxyManager::restore() {
    m_error.clear();
    if (!pending()) return true;
    const auto record = load();
    if (record["version"].toInt() != 1 || !record["original"].isObject() || !record["installed"].isObject()) {
        m_error = QObject::tr("Invalid system proxy recovery data."); return false;
    }
    QJsonObject current;
    if (!read(current)) { m_error = QObject::tr("Cannot read Windows proxy settings."); return false; }
    const auto original = record["original"].toObject(), installed = record["installed"].toObject();
    if (current == installed && !write(original)) { m_error = QObject::tr("Cannot restore Windows proxy settings. Try again."); return false; }
    // A third party owns any different settings. Never overwrite them.
    if (!QFile::remove(m_path)) { m_error = QObject::tr("Cannot remove system proxy recovery data."); return false; }
    return true;
}
QString WindowsSystemProxyManager::status() const {
    if (!pending()) return QObject::tr("System proxy is not managed by AmneziaVPN");
    QJsonObject current;
    if (!read(current)) return QObject::tr("Cannot read Windows proxy settings");
    const auto record = load();
    if (record.isEmpty()) return QObject::tr("System proxy recovery data is invalid");
    return current == record["installed"].toObject() ? QObject::tr("System proxy is enabled; disconnect to restore previous settings")
        : QObject::tr("Windows proxy settings were changed externally; they will not be overwritten");
}
