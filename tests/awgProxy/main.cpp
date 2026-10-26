#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include "core/proxy/windowsSystemProxyManager.h"

// Override every Windows API operation: tests never touch real proxy settings.
class FakeProxy : public WindowsSystemProxyManager {
public:
    explicit FakeProxy(const QString &path) : WindowsSystemProxyManager(path) {}
    mutable QJsonObject current{{"flags", 13}, {"server", "old:3128"}, {"bypass", "<local>"}, {"pac", "http://old/proxy.pac"}};
    bool rejectWrites = false;
protected:
    bool read(QJsonObject &value) const override { value = current; return true; }
    bool write(const QJsonObject &value) override { if (rejectWrites) return false; current = value; return true; }
};
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir directory;
    auto check = [](bool ok) { if (!ok) qFatal("System proxy recovery invariant failed"); };
    const auto path = directory.filePath("journal.json");
    FakeProxy proxy(path);
    const auto original = proxy.current;
    check(proxy.apply(8080)); check(proxy.pending()); check(proxy.current["flags"].toInt() == 2);
    check(proxy.current["pac"].toString().isEmpty()); check(proxy.restore()); check(proxy.current == original); check(!proxy.pending());
    check(proxy.apply(8080));
    const QJsonObject external{{"flags",1},{"server","external"}};
    proxy.current = external; check(proxy.restore()); check(proxy.current == external);
    proxy.current = original; proxy.rejectWrites = true; check(!proxy.apply(8080));
    proxy.rejectWrites = false; check(proxy.restore()); check(proxy.current == original);
    check(proxy.apply(8080)); proxy.rejectWrites = true; check(!proxy.restore()); check(proxy.pending());
    proxy.rejectWrites = false; check(proxy.restore()); check(proxy.current == original);
    // A replacement process recovers a persisted journal, not in-memory state.
    check(proxy.apply(8080)); FakeProxy replacement(path); replacement.current = proxy.current;
    check(replacement.restore()); check(replacement.current == original);
    QFile corrupt(path); check(corrupt.open(QIODevice::WriteOnly)); corrupt.write("invalid"); corrupt.close();
    check(!replacement.restore()); check(replacement.pending());
    return 0;
}
