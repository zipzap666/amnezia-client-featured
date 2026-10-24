#pragma once
#include <QDebug>
// Avoid writing application logs or loading the service in isolated tests.
class Logger {
public:
    explicit Logger(const char *) {}
    QDebug debug() { return qDebug(); }
    QDebug warning() { return qWarning(); }
    QDebug error() { return qWarning(); }
};
