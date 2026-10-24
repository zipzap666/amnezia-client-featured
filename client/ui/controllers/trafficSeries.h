#pragma once

#include <QVariantList>

// Cumulative counters are comparable only within one source identity.
struct TrafficSeries {
    quint64 rx = 0, tx = 0, received = 0, sent = 0;
    qint64 last = -1;
    double inRate = 0, outRate = 0;
    bool available = false;
    bool observed = false;
    QString identity;
    QVariantList history;

    void invalidate() { last = -1; available = false; }
    void update(quint64 r, quint64 t, qint64 now, const QString &id) {
        const auto elapsed = now - last;
        available = last >= 0 && id == identity && elapsed > 0 && elapsed <= 5000 && r >= rx && t >= tx;
        if (available) {
            observed = true;
            received += r - rx;
            sent += t - tx;
            inRate = double(r - rx) * 1000 / elapsed;
            outRate = double(t - tx) * 1000 / elapsed;
        }
        history.append(QVariantMap{{"time", now}, {"valid", available},
                                   {"rx", available ? inRate : 0}, {"tx", available ? outRate : 0}});
        while (!history.isEmpty() && (history.size() > 301 || now - history.first().toMap()["time"].toLongLong() > 300000))
            history.removeFirst();
        rx = r; tx = t; last = now; identity = id;
    }
    QVariantMap data(qint64 now) const {
        return {{"available", available && now - last <= 3500}, {"observed", observed}, {"rx", inRate}, {"tx", outRate},
                {"received", double(received)}, {"sent", double(sent)}, {"history", history}};
    }
};
