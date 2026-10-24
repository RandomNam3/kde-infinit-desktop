/*
    SPDX-FileCopyrightText: 2026 KDE Infinit Desktop contributors
    SPDX-License-Identifier: GPL-3.0-or-later

    Consistency checks between the kcfg schema, the UI metadata and the
    shortcut table, so the settings UIs can never silently lose an entry.
*/
#include "infinitdesktopconfig.h"
#include "settingsmeta.h"
#include "shortcutdefs.h"

#include <KConfigSkeleton>
#include <KSharedConfig>

#include <QSet>
#include <QTemporaryDir>
#include <QTest>

using namespace InfinitDesktop;

class TestShared : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        InfinitDesktopConfig::instance(KSharedConfig::openConfig(m_dir.filePath(QStringLiteral("kwinrc")), KConfig::SimpleConfig));
    }

    void everyEntryHasMetadataAndTexts()
    {
        QSet<QString> categoryIds;
        for (const SettingsCategory &category : settingsCategories()) {
            categoryIds.insert(QString::fromLatin1(category.id));
        }
        const auto items = InfinitDesktopConfig::self()->items();
        QVERIFY(!items.isEmpty());
        for (KConfigSkeletonItem *item : items) {
            const QByteArray key = item->name().toLatin1();
            const SettingsEntryMeta *meta = findSettingsEntry(key.constData());
            QVERIFY2(meta, qPrintable(QStringLiteral("no metadata for %1").arg(item->name())));
            QVERIFY2(categoryIds.contains(QString::fromLatin1(meta->category)), meta->key);
            QVERIFY2(!item->label().isEmpty(), meta->key);
            QVERIFY2(!item->whatsThis().isEmpty(), meta->key);
            if (meta->dependsOn) {
                QVERIFY2(InfinitDesktopConfig::self()->findItem(QString::fromLatin1(meta->dependsOn)), meta->dependsOn);
            }
        }
        for (const SettingsEntryMeta &meta : settingsEntries()) {
            QVERIFY2(InfinitDesktopConfig::self()->findItem(QString::fromLatin1(meta.key)), meta.key);
        }
    }

    void defaultsAreSane()
    {
        auto *config = InfinitDesktopConfig::self();
        QVERIFY(config->minZoom() < config->maxZoom());
        QVERIFY(config->minZoom() <= 100);
        QVERIFY(config->maxZoom() >= 100);
        QCOMPARE(config->launcherPosition(), int(InfinitDesktopConfig::EnumLauncherPosition::Panel));
        QCOMPARE(config->minimizedFullscreenContent(), int(InfinitDesktopConfig::EnumMinimizedFullscreenContent::LiveContent));
        QVERIFY(config->lockFullscreen());
    }

    void shortcutIdsAreUnique()
    {
        QSet<QString> ids;
        QSet<QKeySequence> keys;
        for (const ShortcutDefinition &definition : shortcutDefinitions()) {
            const QString id = QString::fromLatin1(definition.id);
            QVERIFY2(!ids.contains(id), definition.id);
            ids.insert(id);
            QVERIFY(!definition.text.isEmpty());
            for (const QKeySequence &sequence : definition.defaultShortcuts) {
                QVERIFY2(!keys.contains(sequence), qPrintable(sequence.toString()));
                keys.insert(sequence);
            }
            QCOMPARE(findShortcut(definition.action), &definition);
        }
    }

private:
    QTemporaryDir m_dir;
};

QTEST_GUILESS_MAIN(TestShared)
#include "test_shared.moc"
