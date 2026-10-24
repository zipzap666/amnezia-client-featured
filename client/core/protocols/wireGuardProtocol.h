#ifndef WIREGUARDPROTOCOL_H
#define WIREGUARDPROTOCOL_H

#include <QObject>
#include <QProcess>
#include <QString>
#include <QTemporaryFile>
#include <QTimer>
#include <QElapsedTimer>

#include "vpnProtocol.h"

#include "mozilla/controllerimpl.h"

class WireguardProtocol : public VpnProtocol
{
    Q_OBJECT

public:
    explicit WireguardProtocol(const QJsonObject& configuration, QObject* parent = nullptr);
    virtual ~WireguardProtocol() override;

    ErrorCode start() override;
    void stop() override;

    ErrorCode startMzImpl();
    ErrorCode stopMzImpl();

private:

    QScopedPointer<ControllerImpl> m_impl;
#ifdef Q_OS_WIN
    bool m_statisticsPending = false;
    quint64 m_statisticsEpoch = 0, m_statisticsRequestEpoch = 0;
    QElapsedTimer m_statisticsRequest;
#endif
};

#endif // WIREGUARDPROTOCOL_H
