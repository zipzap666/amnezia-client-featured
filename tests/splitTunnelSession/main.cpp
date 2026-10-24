#include "core/utils/splitTunnelSession.h"
#include <QCoreApplication>
#include <cstdio>
#include <cstdlib>

static void check(bool ok, const char *name) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", name); std::exit(1); }
}
static QJsonObject rules() {
    return {{"sitesEnabled", true}, {"siteMode", 2}, {"appsEnabled", true}, {"appMode", 2},
        {"sites", QJsonObject{{"0", QJsonObject{}}, {"1", QJsonObject{}},
            {"2", QJsonObject{{"example.org", QJsonArray{"192.0.2.1"}}, {"198.51.100.0/24", QJsonArray{}}}}}},
        {"apps", QJsonObject{{"0", QJsonArray{}}, {"1", QJsonArray{}}, {"2", QJsonArray{"C:/Apps/browser.exe"}}}}};
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    auto original = rules();
    SplitTunnelSession session;
    session.save(original);
    check(!session.pending(), "no session does not require reconnect");
    session.begin(original);
    auto attempt = session.attempt();
    auto edited = original;
    edited["siteMode"] = 1;
    session.save(edited);
    check(session.pending(), "edits during connection retained");
    check(session.attempt() == attempt, "saved edits cannot mutate attempt");
    session.connected(attempt);
    check(session.pending(), "old attempt completing cannot clear newer edits");
    edited["appsEnabled"] = false;
    session.save(edited);
    check(session.pending(), "multiple edits");
    session.save(original);
    check(!session.pending(), "revert clears without reconnect");
    auto dns = original;
    auto siteLists = dns["sites"].toObject();
    auto sites = siteLists["2"].toObject();
    sites["example.org"] = QJsonArray{"203.0.113.10", "203.0.113.11"};
    siteLists["2"] = sites; dns["sites"] = siteLists;
    session.save(dns);
    check(!session.pending(), "DNS refresh does not change user rules");
    sites.remove("example.org"); sites["EXAMPLE.ORG."] = QJsonArray{};
    siteLists["2"] = sites; dns["sites"] = siteLists; session.save(dns);
    check(!session.pending(), "domain case/trailing-dot normalization");
    sites["203.0.113.0/24"] = QJsonArray{};
    siteLists["2"] = sites; dns["sites"] = siteLists; session.save(dns);
    check(session.pending(), "explicit IP import is a rule change");
    auto clear = original; clear["sites"] = QJsonObject{}; session.save(clear);
    check(session.pending(), "clear sites");
    clear = original; clear["apps"] = QJsonObject{}; session.save(clear);
    check(session.pending(), "clear apps");
#ifdef Q_OS_WIN
    auto paths = original;
    auto apps = paths["apps"].toObject(); apps["2"] = QJsonArray{"c:\\APPS\\BROWSER.EXE", "C:/Apps/browser.exe"};
    paths["apps"] = apps; session.save(paths);
    check(!session.pending(), "Windows paths compare case-insensitively and as a set");
#endif
    session.save(edited);
    check(!session.requestReconnect(false), "cannot reconnect while disconnected/connecting");
    check(session.requestReconnect(true), "accept reconnect from connected");
    check(!session.requestReconnect(true), "repeated click ignored");
    check(session.reconnecting(), "waiting for disconnect remains busy across pages");
    check(session.attempt() == attempt, "no new attempt before disconnect");
    check(session.disconnected(), "completed disconnect starts next attempt");
    check(!session.disconnected(), "duplicate disconnect never starts another attempt");
    session.begin(edited);
    auto newer = edited; newer["sitesEnabled"] = false;
    session.save(newer);
    session.connected(edited); session.finishReconnect();
    check(session.pending(), "edits during reconnect survive success");
    session.begin(newer); session.finishReconnect();
    check(session.pending(), "failure/cancel does not commit attempted settings");
    session.begin(newer); session.connected(newer);
    check(!session.pending(), "manual reconnect applies current settings");
    session.save(original); check(session.requestReconnect(true), "second reconnect accepted");
    session.finishReconnect();
    check(!session.disconnected(), "cancel prevents queued restart");
    check(session.pending(), "cancel keeps unapplied changes");
    // Automatic recovery has no begin(): it reuses the exact protocol snapshot.
    session.connected(newer);
    check(session.pending(), "automatic recovery does not apply saved edits");
    std::puts("PASS: immutable snapshots, semantic comparison, pending changes, reconnect sequencing/cancellation");
}
