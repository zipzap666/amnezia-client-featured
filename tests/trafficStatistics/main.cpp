#include <QCoreApplication>
#include <QEventLoop>
#include <QThread>
#include "vpnConnection.h"
#include "trafficStatisticsController.h"

static int failures = 0;
static void check(bool condition, const char *message) {
    if (!condition) { qCritical("FAIL: %s", message); ++failures; }
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    TrafficSeries s;
    check(!s.data(0)["available"].toBool() && !s.data(0)["observed"].toBool(), "no data is not zero");
    s.update(1000, 2000, 0, "a");
    check(!s.available && s.received == 0, "first sample establishes baseline");
    s.update(2024, 4048, 1000, "a");
    check(s.available && s.inRate == 1024 && s.outRate == 2048, "one second rates and directions");
    s.update(4072, 8144, 3000, "a");
    check(s.inRate == 1024 && s.outRate == 2048 && s.received == 3072, "two second interval");
    s.update(4584, 9168, 3500, "a");
    check(s.inRate == 1024 && s.outRate == 2048, "half second interval");
    s.update(4584, 9168, 4500, "a");
    check(s.available && s.inRate == 0 && s.outRate == 0, "real zero traffic");
    const auto volume = s.received;
    s.update(1, 1, 5500, "a");
    check(!s.available && s.received == volume, "counter reset has no spike");
    s.update(100, 200, 6500, "b");
    check(!s.available && s.received == volume, "adapter identity change");
    s.update(200, 400, 16500, "b");
    check(!s.available && s.received == volume, "sleep gap excluded");
    s.update(300, 600, 17500, "b");
    check(s.available && s.inRate == 100, "recovery after gap");
    check(!s.data(22000)["available"].toBool(), "stale source expires");
    s.update(400, 800, 17500, "b");
    check(!s.available, "nonpositive interval excluded");
    s = {};
    for (int i = 0; i < 1000; ++i) s.update(i * 100, i * 200, i * 1000, "a");
    check(s.history.size() == 301 && s.history.first().toMap()["time"].toInt() == 699000, "five minute bounded history");
    s = {};
    for (int i = 0; i < 1000; ++i) s.update(i, i, i, "a");
    check(s.history.size() == 301, "history bounded under rapid responses");

    VpnConnection connection;
    TrafficStatisticsController controller(&connection);
    emit connection.connectionStateChanged(Vpn::Connected);
    emit connection.protocol->trafficCounters(100, 200);
    check(!controller.vpn()["available"].toBool(), "controller first baseline");
    QThread::msleep(10);
    emit connection.protocol->trafficCounters(200, 400);
    check(controller.vpn()["available"].toBool() && controller.vpn()["received"].toDouble() == 100, "controller receives protocol counters");
    controller.reset();
    check(!controller.vpn()["observed"].toBool() && controller.vpn()["history"].toList().isEmpty(), "reset clears measurements only");
    emit connection.protocol->trafficCounters(300, 600);
    check(!controller.vpn()["available"].toBool(), "reset requires baseline");
    emit connection.connectionStateChanged(Vpn::Reconnecting);
    emit connection.protocol->trafficCounters(100000, 200000);
    check(controller.vpn()["history"].toList().isEmpty(), "late disconnected samples ignored");
    auto oldProtocol = connection.protocol;
    connection.protocol.reset(new VpnProtocol);
    emit connection.connectionStateChanged(Vpn::Connected);
    emit oldProtocol->trafficCounters(300000, 400000);
    check(controller.vpn()["history"].toList().isEmpty(), "old protocol ignored after replacement");
    emit connection.protocol->trafficCounters(500, 500);
    check(!controller.vpn()["observed"].toBool(), "new session baseline");
    emit connection.connectionStateChanged(Vpn::Disconnected);
    QEventLoop loop;
    QTimer::singleShot(1200, &loop, &QEventLoop::quit);
    loop.exec(); // Exercise read-only asynchronous WinAPI and callback lifetime.
    check(!controller.physical()["history"].toList().isEmpty(), "asynchronous physical sample");
    qInfo("Traffic statistics checks: %s", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
