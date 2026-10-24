#include "core/utils/splitTunnelCompatibility.h"
#include <QCoreApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <cstdio>
#include <cstdlib>

static void check(bool ok, const char *message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
static QJsonObject application(const QString &path, const QString &name = "App") {
    return {{"appName", name}, {"packageName", ""}, {"appPath", path}};
}
static SplitTunnelCompatibility repository(QSettings &settings) {
    return {[&settings](const QString &key) { return settings.value(key); },
            [&settings](const QString &key, const QVariant &value) { settings.setValue(key, value); }};
}
static QJsonObject disabledGroup(const QString &key) {
    return {{"groups", QJsonArray{QJsonObject{{"id", "games"}, {"name", "Games"}, {"enabled", false}}}},
            {"rules", QJsonObject{{key, QJsonObject{{"groupId", "games"}, {"enabled", true}}}}}};
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir temporary;
    check(temporary.isValid(), "temporary isolated settings directory");
    const auto file = temporary.filePath("settings.ini");
    QSettings settings(file, QSettings::IniFormat);
    const QString visible = "C:/Apps/browser.exe", hidden = "C:/Apps/game.exe";
    QJsonArray applications {application(visible), application(hidden)};
    QVariantMap sites {{"example.org", QStringList{"192.0.2.1", "192.0.2.2"}},
                       {"off.org", QString("198.51.100.1")}, {"203.0.113.0/24", QStringList{}}};
    settings.setValue("Conf/ExceptApps", applications);
    settings.setValue("Conf/ForwardSites", sites);
    settings.setValue("Conf/appsRouteMode", 2);
    settings.setValue("Conf/routeMode", 1);
    settings.setValue("Conf/appsSplitTunnelingEnabled", true);
    settings.setValue("Conf/sitesSplitTunnelingEnabled", true);
    settings.setValue("Servers/sentinel", "leave unchanged");
    settings.setValue("Conf/splitGroups/apps/2", disabledGroup(SplitTunnelGroups::key(hidden, true)));
    settings.setValue("Conf/splitGroups/sites/1", disabledGroup("off.org"));
    auto repo = repository(settings);
    repo.initialize();
    check(repo.full(true, 2).toJsonArray() == applications, "migration retains complete app list");
    check(repo.full(false, 1).toMap() == sites, "migration retains all sites and DNS cache");
    // These are the exact reads performed by official 5.0.3.0; no custom keys are used.
    auto officialApps = settings.value("Conf/ExceptApps").toJsonArray();
    auto officialSites = settings.value("Conf/ForwardSites").toMap();
    check(officialApps.size() == 1 && officialApps[0].toObject().value("appPath").toString() == visible,
          "official client sees enabled EXE paths, never PIDs/packageName");
    check(!officialSites.contains("off.org") && officialSites.contains("203.0.113.0/24"), "official sees active site/IP rules only");
    check(officialSites["example.org"].toStringList().size() == 2, "official DNS cache format preserved");
    auto groups = repo.groups(true, 2);
    groups["groups"] = QJsonArray{QJsonObject{{"id", "games"}, {"name", "Renamed"}, {"enabled", true}}};
    repo.writeGroups(true, 2, groups);
    check(settings.value("Conf/ExceptApps").toJsonArray().size() == 2, "group enable publishes without VPN connection");
    auto rules = groups["rules"].toObject(); rules[SplitTunnelGroups::key(hidden, true)] = QJsonObject{{"groupId", "games"}, {"enabled", false}};
    groups["rules"] = rules;
    repo.writeGroups(true, 2, groups);
    check(settings.value("Conf/ExceptApps").toJsonArray().size() == 1, "individual switch publishes immediately");
    auto renamed = groups; renamed["groups"] = QJsonArray{QJsonObject{{"id", "games"}, {"name", "Another name"}, {"enabled", true}}};
    const auto beforeRename = settings.value("Conf/ExceptApps");
    repo.writeGroups(true, 2, renamed);
    check(settings.value("Conf/ExceptApps") == beforeRename, "renaming does not change compatibility rules");
    // Normal restart / downgrade without edits / return retains inactive rules and grouping.
    repo.initialize();
    check(repo.full(true, 2).toJsonArray().size() == 2 && repo.groups(true, 2) == renamed, "restart retains disabled records and group states");
    settings.sync();
    check(settings.status() == QSettings::NoError, "QSettings serializes real JSON settings types");
    {
        QSettings reopened(file, QSettings::IniFormat);
        auto reopenedRepo = repository(reopened); reopenedRepo.initialize();
        check(reopened.value("Conf/ExceptApps").toJsonArray().size() == 1, "official reads after settings reopen");
        check(reopenedRepo.full(true, 2).toJsonArray().size() == 2, "full copy survives disk reopen");
    }
    // Simulate official removals, additions and DNS updates. Existing disabled rules survive.
    settings.setValue("Conf/ExceptApps", QJsonArray{application("C:/Apps/new.exe", "New")});
    auto editedSites = officialSites; editedSites.remove("203.0.113.0/24");
    editedSites["example.org"] = QStringList{"192.0.2.9"};
    editedSites.insert("new.org", QStringList{"203.0.113.7"});
    settings.setValue("Conf/ForwardSites", editedSites);
    repo.initialize();
    check(repo.full(true, 2).toJsonArray().size() == 2, "official removal does not delete hidden rules");
    check(settings.value("Conf/ExceptApps").toJsonArray()[0].toObject()["appPath"].toString() == "C:/Apps/new.exe", "official additions/removals carried back");
    check(repo.full(false, 1).toMap().contains("off.org"), "disabled site survives official edits");
    check(!repo.full(false, 1).toMap().contains("203.0.113.0/24"), "official removal respected");
    check(repo.full(false, 1).toMap()["example.org"].toStringList() == QStringList{"192.0.2.9"}, "official DNS updates retained");
    // An explicitly re-added inactive rule must be active, without waking its peers.
    auto readded = settings.value("Conf/ForwardSites").toMap(); readded["off.org"] = QStringList{"198.51.100.1"};
    settings.setValue("Conf/ForwardSites", readded);
    repo.initialize();
    check(settings.value("Conf/ForwardSites").toMap().contains("off.org"), "official re-added disabled site becomes active");
    check(!repo.groups(false, 1)["groups"].toArray()[0].toObject()["enabled"].toBool(), "reactivating one rule does not enable its old group");
    // When Ungrouped is off, place explicit official additions into an enabled import group.
    auto allOff = repo.groups(true, 2); allOff["ungroupedEnabled"] = false;
    repo.writeGroups(true, 2, allOff);
    check(settings.value("Conf/ExceptApps").toJsonArray().isEmpty(), "disabled Ungrouped exported empty");
    settings.setValue("Conf/ExceptApps", QJsonArray{application("C:/Apps/official.exe")});
    repo.initialize();
    check(settings.value("Conf/ExceptApps").toJsonArray().size() == 1, "official addition remains active with Ungrouped off");
    check(!repo.groups(true, 2)["ungroupedEnabled"].toBool(), "import never enables unrelated Ungrouped rules");
    // Clearing in official removes only previously published rules.
    settings.setValue("Conf/ExceptApps", QJsonArray{});
    repo.initialize();
    check(settings.value("Conf/ExceptApps").toJsonArray().isEmpty(), "official clear retained");
    check(repo.full(true, 2).toJsonArray().size() == 2, "official clear preserves two inactive records");
    // Interrupted writes must not be interpreted as official deletions.
    settings.setValue(SplitTunnelCompatibility::pendingKey(false, 1), true);
    settings.setValue("Conf/ForwardSites", QVariantMap{});
    repo.initialize();
    check(!settings.value("Conf/ForwardSites").toMap().isEmpty(), "interrupted projection repaired from full copy");
    check(!settings.value(SplitTunnelCompatibility::pendingKey(false, 1)).toBool(), "publication marker cleared");
    // The existing JSON backup path changes JSON types into QVariantList/QVariantMap.
    QJsonObject backup;
    for (const auto &key : settings.allKeys()) backup.insert(key, QJsonValue::fromVariant(settings.value(key)));
    QSettings restored(temporary.filePath("restored.ini"), QSettings::IniFormat);
    const auto decoded = QJsonDocument::fromJson(QJsonDocument(backup).toJson()).object();
    for (const auto &key : decoded.keys()) restored.setValue(key, decoded[key].toVariant());
    auto restoredRepo = repository(restored); restoredRepo.initialize();
    check(restoredRepo.full(true, 2).toJsonArray().size() == 2, "backup round trip retains full app list");
    check(restored.value("Conf/ExceptApps").toJsonArray().isEmpty(), "backup round trip retains active flat list");
    check(restored.value("Conf/appsRouteMode").toInt() == 2 && restored.value("Conf/routeMode").toInt() == 1, "modes preserved");
    check(restored.value("Servers/sentinel").toString() == "leave unchanged", "unrelated settings untouched");
    check(restored.value("Conf/appsSplitTunnelingEnabled").toBool() && restored.value("Conf/sitesSplitTunnelingEnabled").toBool(), "global enable flags preserved");
#ifdef Q_OS_WIN
    QSettings aliases(temporary.filePath("case.ini"), QSettings::IniFormat);
    auto aliasRepo = repository(aliases); aliasRepo.initialize();
    aliasRepo.writeList(true, 1, QJsonArray{application("C:/APPS/GAME.EXE"), application("c:\\apps\\game.exe")});
    check(aliases.value("Conf/ForwardApps").toJsonArray().size() == 1, "official snapshot deduplicates Windows path case/separators");
    auto membership = disabledGroup("c:/apps/game.exe");
    membership["groups"] = QJsonArray{QJsonObject{{"id", "games"}, {"name", "Games"}, {"enabled", true}}};
    aliasRepo.writeGroups(true, 1, membership);
    aliases.setValue("Conf/ForwardApps", QJsonArray{application("c:\\apps\\game.exe", "Renamed in official")});
    aliasRepo.initialize();
    check(aliasRepo.groups(true, 1)["rules"].toObject()["c:/apps/game.exe"].toObject()["groupId"].toString() == "games", "official path case changes retain group membership");
    aliasRepo.writeList(true, 1, QJsonArray{});
    check(aliasRepo.groups(true, 1)["rules"].toObject().isEmpty(), "deleting complete list prunes rule metadata");
    check(aliasRepo.groups(true, 1)["groups"].toArray().size() == 1, "clearing list retains empty groups");
    aliasRepo.writeList(true, 1, QJsonArray{application("C:/Apps/game.exe")});
    check(SplitTunnelGroups::rule(aliasRepo.groups(true, 1), "C:/Apps/game.exe", true)["groupId"].toString().isEmpty(), "readding removed app does not resurrect old membership");
#endif
    QSettings disabledImports(temporary.filePath("disabled-imports.ini"), QSettings::IniFormat);
    auto importsRepo = repository(disabledImports); importsRepo.initialize();
    importsRepo.writeList(false, 2, QVariantMap{{"old.org", QStringList{"192.0.2.1"}}});
    importsRepo.writeGroups(false, 2, QJsonObject{{"ungroupedEnabled", false}});
    disabledImports.setValue("Conf/ExceptSites", QVariantMap{{"first.org", QStringList{"192.0.2.2"}}});
    importsRepo.initialize();
    auto importState = importsRepo.groups(false, 2);
    auto importGroups = importState["groups"].toArray();
    auto firstImport = importGroups[0].toObject(); firstImport["enabled"] = false; importGroups[0] = firstImport;
    importState["groups"] = importGroups; importsRepo.writeGroups(false, 2, importState);
    disabledImports.setValue("Conf/ExceptSites", QVariantMap{{"second.org", QStringList{"192.0.2.3"}}});
    importsRepo.initialize();
    check(disabledImports.value("Conf/ExceptSites").toMap().keys() == QStringList{"second.org"}, "official addition never wakes disabled imported peers");
    check(importsRepo.full(false, 2).toMap().size() == 3, "repeated official round trips retain all inactive records");
    check(importsRepo.groups(false, 2)["groups"].toArray().size() == 2, "disabled import group is preserved and a new enabled group is created");
    // All six list keys use the official schema, independent of currently selected mode.
    for (bool apps : {false, true}) for (int mode = 0; mode <= 2; ++mode) {
        check(restored.contains(SplitTunnelCompatibility::legacyKey(apps, mode)), "all mode projections exist");
        check(restored.contains(SplitTunnelCompatibility::fullKey(apps, mode)), "all full lists backed up");
    }
    std::puts("PASS: migration, official reads, disk restart, groups/toggles, downgrade edits, inactive rules, interrupted writes, JSON backup");
}
