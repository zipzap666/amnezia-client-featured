#pragma once
#include <QByteArray>
#include <QRegularExpression>
#include <QStringList>

// Never retain stderr verbatim: panic values may include application data.
// Keep only runtime markers and function names, excluding arguments and files.
struct HelperDiagnostics {
    bool panic = false, fatal = false;
    QStringList frames;
    QByteArray pending;
    int bytes = 0;
    void feed(const QByteArray &chunk) {
        const int allowed = qMax(0, 65536 - bytes);
        pending += chunk.left(allowed);
        bytes += qMin(chunk.size(), qsizetype(allowed));
        int end;
        while ((end = pending.indexOf('\n')) >= 0) {
            const auto line = QString::fromUtf8(pending.left(end)).trimmed();
            pending.remove(0, end + 1);
            panic |= line.startsWith("panic:");
            fatal |= line.startsWith("fatal error:");
            if (frames.size() >= 8 || !(line.startsWith("runtime.") || line.startsWith("main.")
                || line.startsWith("gvisor.dev/gvisor/") || line.startsWith("amnezia.org/awg-proxy/")
                || line.startsWith("github.com/amnezia-vpn/amneziawg-go/"))) continue;
            const int args = line.lastIndexOf('(');
            if (args <= 0) continue;
            const auto function = line.left(args);
            static const QRegularExpression name("^[A-Za-z0-9_./()*-]{1,240}$");
            if (name.match(function).hasMatch() && !frames.contains(function)) frames.append(function);
        }
        if (pending.size() > 4096 || bytes >= 65536) pending.clear();
    }
};
