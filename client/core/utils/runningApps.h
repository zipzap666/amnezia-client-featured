#pragma once

#include <QDir>
#include <QFileInfo>
#include <QScopeGuard>
#include <QSet>
#include <QVariant>
#include <QBuffer>
#include <QImage>
#include <algorithm>

#ifdef Q_OS_WIN
#include <windows.h>
#include <tlhelp32.h>
#include <shellapi.h>
#pragma comment(lib, "shell32.lib")
#endif

namespace RunningApps {
// Extract resources without executing the application. Called on the enumeration worker.
inline QString executableIcon(const QString &path)
{
#ifdef Q_OS_WIN
    HICON icon = nullptr;
    ExtractIconExW(reinterpret_cast<LPCWSTR>(path.utf16()), 0, nullptr, &icon, 1);
    if (!icon)
        return {};
    const auto destroyIcon = qScopeGuard([icon] { DestroyIcon(icon); });
    const QImage image = QImage::fromHICON(icon);
    QByteArray png;
    QBuffer buffer(&png);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG"))
        return {};
    return QStringLiteral("data:image/png;base64,") + QString::fromLatin1(png.toBase64());
#else
    Q_UNUSED(path)
    return {};
#endif
}

inline QString pathKey(QString path)
{
    path.replace('\\', '/');
    return QDir::cleanPath(path).toCaseFolded();
}

inline QVariantList enumerate()
{
    QVariantList apps;
#ifdef Q_OS_WIN
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return apps;
    const auto closeSnapshot = qScopeGuard([snapshot] { CloseHandle(snapshot); });
    PROCESSENTRY32W entry {};
    entry.dwSize = sizeof(entry);
    QSet<QString> seen;
    if (!Process32FirstW(snapshot, &entry))
        return apps;
    do {
        const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
        if (!process)
            continue;
        const auto closeProcess = qScopeGuard([process] { CloseHandle(process); });
        wchar_t buffer[32768];
        DWORD length = DWORD(std::size(buffer));
        if (!QueryFullProcessImageNameW(process, 0, buffer, &length))
            continue; // Protected processes and processes exiting during enumeration are normal.
        const QString path = QString::fromWCharArray(buffer, int(length));
        const QString key = pathKey(path);
        if (path.isEmpty() || seen.contains(key))
            continue;
        seen.insert(key);
        apps.append(QVariantMap { { "appName", QFileInfo(path).fileName() }, { "appPath", path },
                                 { "appIcon", executableIcon(path) } });
    } while (Process32NextW(snapshot, &entry));
    std::sort(apps.begin(), apps.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value("appPath").toString().compare(b.toMap().value("appPath").toString(), Qt::CaseInsensitive) < 0;
    });
#endif
    return apps;
}
}
