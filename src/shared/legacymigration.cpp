/*
    KDE Infinite Desktop - infinite canvas for KWin
    SPDX-FileCopyrightText: 2026 KDE Infinite Desktop contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/
#include "legacymigration.h"

#include <KConfig>
#include <KConfigGroup>

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QStandardPaths>

namespace InfiniteDesktop::Legacy
{

namespace
{

bool copyRecursively(const QString &from, const QString &to)
{
    const QFileInfo source(from);
    if (!source.isDir()) {
        return QFile::copy(from, to);
    }
    if (!QDir().mkpath(to)) {
        return false;
    }
    QDirIterator it(from, QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot | QDir::NoSymLinks);
    while (it.hasNext()) {
        it.next();
        if (!copyRecursively(it.filePath(), to + QLatin1Char('/') + it.fileName())) {
            return false;
        }
    }
    return true;
}

} // namespace

ConfigResult migrateConfig(KConfig &kwinrc)
{
    ConfigResult result;
    const KConfigGroup oldGroup = kwinrc.group(QString::fromLatin1(OldEffectGroup));
    KConfigGroup newGroup = kwinrc.group(QString::fromLatin1(NewEffectGroup));
    if (oldGroup.exists() && !oldGroup.keyList().isEmpty() && newGroup.keyList().isEmpty()) {
        const QMap<QString, QString> entries = oldGroup.entryMap();
        for (auto it = entries.cbegin(); it != entries.cend(); ++it) {
            newGroup.writeEntry(it.key(), it.value());
        }
        result.settingsCopied = true;
    }

    KConfigGroup plugins = kwinrc.group(QStringLiteral("Plugins"));
    const QString oldKey = QString::fromLatin1(OldEnabledKey);
    const QString newKey = QString::fromLatin1(NewEnabledKey);
    if (plugins.hasKey(oldKey) && !plugins.hasKey(newKey)) {
        plugins.writeEntry(newKey, plugins.readEntry(oldKey, false));
        result.enabledCopied = true;
    }
    if (plugins.readEntry(oldKey, false)) {
        // Only one of the two plugins may run.
        plugins.writeEntry(oldKey, false);
        result.enabledCopied = true;
    }
    result.enabled = plugins.readEntry(newKey, false);

    if (result.changed()) {
        kwinrc.sync();
    }
    return result;
}

QString legacyShortcutId(const QString &id)
{
    static const QString newPrefix = QStringLiteral("InfiniteDesktop ");
    static const QString oldPrefix = QStringLiteral("InfinitDesktop ");
    return id.startsWith(newPrefix) ? oldPrefix + id.mid(newPrefix.size()) : id;
}

bool migratePath(const QString &oldPath, const QString &newPath)
{
    const QFileInfo oldInfo(oldPath);
    if (!oldInfo.exists() || oldInfo.isSymLink() || QFileInfo::exists(newPath)) {
        return false;
    }
    QDir().mkpath(QFileInfo(newPath).absolutePath());
    if (QFile::rename(oldPath, newPath)) {
        return true;
    }
    if (copyRecursively(oldPath, newPath)) {
        return true;
    }
    // A half copy would hide the old files from the next attempt.
    if (QFileInfo(newPath).isDir()) {
        QDir(newPath).removeRecursively();
    } else {
        QFile::remove(newPath);
    }
    return false;
}

QStringList migrateUserFiles(const QStringList &baseDirectories)
{
    const QString oldName = QString::fromLatin1(OldDirectoryName);
    const QString newName = QString::fromLatin1(NewDirectoryName);
    QStringList migrated;
    QSet<QString> seen;
    for (const QString &base : baseDirectories) {
        if (base.isEmpty() || seen.contains(base)) {
            continue;
        }
        seen.insert(base);
        const QDir dir(base);
        // "kde-infinit-desktop" (the state and notes folder) and "kde-infinit-desktop-*rc".
        const QStringList entries = dir.entryList({oldName, oldName + QStringLiteral("-*")}, QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot);
        for (const QString &entry : entries) {
            const QString target = newName + entry.mid(oldName.size());
            if (migratePath(dir.filePath(entry), dir.filePath(target))) {
                migrated.append(dir.filePath(target));
            }
        }
    }
    return migrated;
}

QStringList migrateUserFiles()
{
    QStringList bases;
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
    bases.append(QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation));
#endif
    bases.append(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation));
    bases.append(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation));
    return migrateUserFiles(bases);
}

} // namespace InfiniteDesktop::Legacy
