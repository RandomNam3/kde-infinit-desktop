/*
    SPDX-FileCopyrightText: 2026 KDE Infinite Desktop contributors
    SPDX-License-Identifier: GPL-3.0-or-later

    Taking over the settings and files of the old name "KDE Infinit Desktop".
*/
#include "legacymigration.h"

#include <KConfig>
#include <KConfigGroup>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

using namespace InfiniteDesktop;

namespace
{

void writeFile(const QString &path, const QByteArray &content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(content);
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

} // namespace

class TestLegacyMigration : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void copiesSettingsAndEnabledState()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("kwinrc"));
        {
            KConfig config(path, KConfig::SimpleConfig);
            config.group(QStringLiteral("Effect-infinitdesktop")).writeEntry("MaxZoom", 250);
            config.group(QStringLiteral("Effect-infinitdesktop")).writeEntry("PanButton", "Middle");
            config.group(QStringLiteral("Plugins")).writeEntry("infinitdesktopEnabled", true);
            config.group(QStringLiteral("Plugins")).writeEntry("blurEnabled", true);
        }
        KConfig config(path, KConfig::SimpleConfig);
        const Legacy::ConfigResult result = Legacy::migrateConfig(config);
        QVERIFY(result.settingsCopied);
        QVERIFY(result.enabledCopied);
        QVERIFY(result.enabled);

        KConfig reread(path, KConfig::SimpleConfig);
        const KConfigGroup group = reread.group(QStringLiteral("Effect-infinitedesktop"));
        QCOMPARE(group.readEntry("MaxZoom", 0), 250);
        QCOMPARE(group.readEntry("PanButton", QString()), QStringLiteral("Middle"));
        const KConfigGroup plugins = reread.group(QStringLiteral("Plugins"));
        QCOMPARE(plugins.readEntry("infinitedesktopEnabled", false), true);
        QCOMPARE(plugins.readEntry("infinitdesktopEnabled", true), false);
        QCOMPARE(plugins.readEntry("blurEnabled", false), true);

        // A second run changes nothing.
        KConfig again(path, KConfig::SimpleConfig);
        const Legacy::ConfigResult second = Legacy::migrateConfig(again);
        QVERIFY(!second.changed());
        QVERIFY(second.enabled);
    }

    void keepsNewSettings()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("kwinrc"));
        {
            KConfig config(path, KConfig::SimpleConfig);
            config.group(QStringLiteral("Effect-infinitdesktop")).writeEntry("MaxZoom", 250);
            config.group(QStringLiteral("Effect-infinitedesktop")).writeEntry("MaxZoom", 300);
            config.group(QStringLiteral("Plugins")).writeEntry("infinitdesktopEnabled", false);
            config.group(QStringLiteral("Plugins")).writeEntry("infinitedesktopEnabled", true);
        }
        KConfig config(path, KConfig::SimpleConfig);
        const Legacy::ConfigResult result = Legacy::migrateConfig(config);
        QVERIFY(!result.changed());
        QVERIFY(result.enabled);
        QCOMPARE(config.group(QStringLiteral("Effect-infinitedesktop")).readEntry("MaxZoom", 0), 300);
    }

    void nothingToMigrate()
    {
        QTemporaryDir dir;
        KConfig config(dir.filePath(QStringLiteral("kwinrc")), KConfig::SimpleConfig);
        const Legacy::ConfigResult result = Legacy::migrateConfig(config);
        QVERIFY(!result.changed());
        QVERIFY(!result.enabled);
        QVERIFY(!config.hasGroup(QStringLiteral("Effect-infinitedesktop")));
    }

    void disablesTheOldPluginEvenIfTheNewOneIsSet()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("kwinrc"));
        {
            KConfig config(path, KConfig::SimpleConfig);
            config.group(QStringLiteral("Plugins")).writeEntry("infinitdesktopEnabled", true);
            config.group(QStringLiteral("Plugins")).writeEntry("infinitedesktopEnabled", false);
        }
        KConfig config(path, KConfig::SimpleConfig);
        const Legacy::ConfigResult result = Legacy::migrateConfig(config);
        QVERIFY(result.changed());
        QVERIFY(!result.enabled);
        QCOMPARE(config.group(QStringLiteral("Plugins")).readEntry("infinitdesktopEnabled", true), false);
    }

    void shortcutIds()
    {
        QCOMPARE(Legacy::legacyShortcutId(QStringLiteral("InfiniteDesktop ZoomIn")), QStringLiteral("InfinitDesktop ZoomIn"));
        QCOMPARE(Legacy::legacyShortcutId(QStringLiteral("Other")), QStringLiteral("Other"));
    }

    void movesFoldersAndConfigFiles()
    {
        QTemporaryDir state;
        QTemporaryDir config;
        writeFile(state.filePath(QStringLiteral("kde-infinit-desktop/canvas.json")), "{\"canvases\":[]}");
        writeFile(state.filePath(QStringLiteral("kde-infinit-desktop/notes/abc.md")), "# Note");
        writeFile(config.filePath(QStringLiteral("kde-infinit-desktop-settingsrc")), "[Updates]\nCheckOnStart=false\n");
        writeFile(config.filePath(QStringLiteral("unrelatedrc")), "x");

        const QStringList migrated = Legacy::migrateUserFiles({state.path(), config.path(), state.path()});
        QCOMPARE(migrated.size(), 2);
        QCOMPARE(readFile(state.filePath(QStringLiteral("kde-infinite-desktop/canvas.json"))), QByteArray("{\"canvases\":[]}"));
        QCOMPARE(readFile(state.filePath(QStringLiteral("kde-infinite-desktop/notes/abc.md"))), QByteArray("# Note"));
        QVERIFY(!QFileInfo::exists(state.filePath(QStringLiteral("kde-infinit-desktop"))));
        QVERIFY(QFileInfo::exists(config.filePath(QStringLiteral("kde-infinite-desktop-settingsrc"))));
        QVERIFY(QFileInfo::exists(config.filePath(QStringLiteral("unrelatedrc"))));

        // Nothing left to do.
        QVERIFY(Legacy::migrateUserFiles({state.path(), config.path()}).isEmpty());
    }

    void neverOverwritesNewFiles()
    {
        QTemporaryDir state;
        writeFile(state.filePath(QStringLiteral("kde-infinit-desktop/canvas.json")), "old");
        writeFile(state.filePath(QStringLiteral("kde-infinite-desktop/canvas.json")), "new");
        QVERIFY(Legacy::migrateUserFiles({state.path()}).isEmpty());
        QCOMPARE(readFile(state.filePath(QStringLiteral("kde-infinite-desktop/canvas.json"))), QByteArray("new"));
        QCOMPARE(readFile(state.filePath(QStringLiteral("kde-infinit-desktop/canvas.json"))), QByteArray("old"));
    }
};

QTEST_GUILESS_MAIN(TestLegacyMigration)

#include "test_legacymigration.moc"
