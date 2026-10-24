#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QFileInfo>
#include <QDebug>
#include <QUuid>
#include <QSignalSpy>
#include <QTest>

#include "utils/testCoreController.h"
#include "utils/testUtils.h"
#include "vpnConnection.h"
#include "secureQSettings.h"
#include "core/utils/splitTunnelCompatibility.h"

using namespace amnezia::test;

class TestBackupExportImport : public QObject
{
    Q_OBJECT

private:
    TestCoreController* m_coreController;
    SecureQSettings* m_settings;


private slots:
    void initTestCase() {
        QString testOrg = "AmneziaVPN-Test-" + QUuid::createUuid().toString();
        m_settings = new SecureQSettings(testOrg, "amnezia-client", nullptr, false);

        auto vpnConnection = QSharedPointer<VpnConnection>::create(nullptr, nullptr);

        m_coreController = new TestCoreController(vpnConnection, m_settings, nullptr, this);
    }

    void cleanupTestCase() {
        m_settings->clearSettings();
        delete m_coreController;
        delete m_settings;
    }

    void init() {
        m_settings->clearSettings();
    }

    void testBackupExportImport() {
        QString backup = getEnvValue("BACKUP_PATH");

        logEnvValueState("BACKUP_PATH");
        qInfo() << "BACKUP_PATH file exists:" << QFile::exists(backup)
                << "size:" << QFileInfo(backup).size();

        if (!isEnvValueConfigured(backup)) {
            QSKIP("Set BACKUP_PATH");
        }
        QVERIFY2(QFileInfo(backup).size() > 0,
                 "BACKUP_PATH file is empty - is the BACKUP_FILE_BASE64 secret configured?");

        QSignalSpy errorOccurredSpy(m_coreController->m_settingsUiController, &SettingsUiController::errorOccurred);

        m_coreController->m_settingsUiController->restoreAppConfig(backup);
        QVERIFY2(errorOccurredSpy.count() == 0, "errorOccurred signal should NOT be emitted (default is already 0)");

        QByteArray backupData = m_coreController->m_settingsController->backupAppConfig();

        QFile file(backup);
        QVERIFY2(file.open(QIODevice::ReadOnly), "Backup file should open for reading");
        const QByteArray fileData = file.readAll();

        QJsonObject importedObj = QJsonDocument::fromJson(fileData).object();
        QJsonObject exportedObj = QJsonDocument::fromJson(backupData).object();
        QVERIFY2(!importedObj.isEmpty(), "Backup file should contain valid JSON");
        QVERIFY2(!exportedObj.isEmpty(), "Exported backup should contain valid JSON");

        // Migration adds full lists/projections even when importing an old official
        // backup. Verify every projection before excluding generated metadata from
        // the comparison of the original backup's keys.
        for (bool apps : {false, true}) {
            for (int mode = 0; mode <= 2; ++mode) {
                const auto legacy = SplitTunnelCompatibility::legacyKey(apps, mode);
                const auto full = SplitTunnelCompatibility::fullKey(apps, mode);
                const auto groups = SplitTunnelCompatibility::groupsKey(apps, mode);
                QVERIFY2(exportedObj.contains(full), "Full split tunneling list must be backed up");
                const auto projected = SplitTunnelCompatibility::project(apps, exportedObj.value(full).toVariant(),
                                                                          exportedObj.value(groups).toObject());
                QVERIFY2(exportedObj.value(legacy) == QJsonValue::fromVariant(projected),
                         "Official list must match the active full-list projection");
                const auto legacyValue = exportedObj.value(legacy);
                if (!importedObj.contains(legacy) && ((legacyValue.isArray() && legacyValue.toArray().isEmpty())
                    || (legacyValue.isObject() && legacyValue.toObject().isEmpty()))) exportedObj.remove(legacy);
            }
        }
        for (const auto &key : exportedObj.keys()) {
            if (key.startsWith("Conf/splitGroups/") && !importedObj.contains(key)) exportedObj.remove(key);
        }
        // Ordering of app entries is not a routing rule; imported official edits
        // can be reconciled in canonical path order.
        const auto sortApps = [](QJsonObject &config, const QString &key) {
            if (!config.value(key).isArray()) return;
            QMap<QString, QJsonObject> entries;
            for (const auto &value : config.value(key).toArray()) {
                const auto entry = value.toObject();
                const auto path = entry.value("appPath").toString();
                entries.insert(SplitTunnelGroups::key(path.isEmpty() ? entry.value("packageName").toString() : path, true), entry);
            }
            QJsonArray sorted;
            for (const auto &entry : entries) sorted.append(entry);
            config.insert(key, sorted);
        };
        for (int mode = 0; mode <= 2; ++mode) {
            const auto key = SplitTunnelCompatibility::legacyKey(true, mode);
            sortApps(importedObj, key); sortApps(exportedObj, key);
        }

        // backupAppConfig() injects live platform state on every export
        // (AppPlatform, autoStart, killSwitch...), and restoreAppConfigFromData()
        // normalizes split tunneling / startMinimized / ExceptApps to the current
        // platform, so these keys legitimately differ from a backup made on
        // another platform/machine.
        const QStringList volatileKeys = { "AppPlatform", "Conf/autoStart", "Conf/killSwitchEnabled",
                                           "Conf/strictKillSwitchEnabled", "Conf/useAmneziaDns",
                                           "Conf/appsSplitTunnelingEnabled", "Conf/sitesSplitTunnelingEnabled",
                                           "Conf/startMinimized", "Conf/ExceptApps",
                                           SplitTunnelCompatibility::fullKey(true, 2),
                                           SplitTunnelCompatibility::mirrorKey(true, 2),
                                           SplitTunnelCompatibility::groupsKey(true, 2),
                                           SplitTunnelCompatibility::pendingKey(true, 2) };
        for (const QString &key : volatileKeys) {
            importedObj.remove(key);
            exportedObj.remove(key);
        }

        if (exportedObj != importedObj) {
            for (auto it = importedObj.constBegin(); it != importedObj.constEnd(); ++it) {
                if (!exportedObj.contains(it.key())) {
                    qWarning() << "Key missing in exported backup:" << it.key();
                } else if (exportedObj.value(it.key()) != it.value()) {
                    qWarning() << "Key differs:" << it.key();
                }
            }
            for (auto it = exportedObj.constBegin(); it != exportedObj.constEnd(); ++it) {
                if (!importedObj.contains(it.key())) {
                    qWarning() << "Extra key in exported backup:" << it.key();
                }
            }
        }
        QVERIFY2(exportedObj == importedObj,
                 "Imported and exported backup config should be equal (ignoring platform-specific keys)");
    }
};

QTEST_MAIN(TestBackupExportImport)
#include "testBackupExportImport.moc"
