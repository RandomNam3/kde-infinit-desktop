/*
    KDE Infinite Desktop - infinite canvas for KWin
    SPDX-FileCopyrightText: 2026 KDE Infinite Desktop contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/
#pragma once

#include <QString>
#include <QStringList>

class KConfig;

namespace InfiniteDesktop::Legacy
{

/// Up to version 0.5.1 the project was called "KDE Infinit Desktop". These are the names
/// the old versions used; everything that still knows them lives here.
inline constexpr const char *OldEffectGroup = "Effect-infinitdesktop";
inline constexpr const char *NewEffectGroup = "Effect-infinitedesktop";
inline constexpr const char *OldEnabledKey = "infinitdesktopEnabled";
inline constexpr const char *NewEnabledKey = "infinitedesktopEnabled";
inline constexpr const char *OldDirectoryName = "kde-infinit-desktop";
inline constexpr const char *NewDirectoryName = "kde-infinite-desktop";
/// Marker in the effect group: the global shortcuts of the old names were taken over.
inline constexpr const char *ShortcutsMigratedKey = "LegacyShortcutsMigrated";

struct ConfigResult
{
    bool settingsCopied = false;
    bool enabledCopied = false;
    /// The effect is enabled under the new name after the migration.
    bool enabled = false;

    bool changed() const
    {
        return settingsCopied || enabledCopied;
    }
};

/**
 * Takes over the settings of the old name in kwinrc, once:
 * - [Effect-infinitdesktop] is copied to [Effect-infinitedesktop] if the new group is empty;
 * - [Plugins] infinitdesktopEnabled is copied to infinitedesktopEnabled if that is not set,
 *   and the old key is set to false, so a leftover old plugin is not loaded as well.
 * Idempotent: a second run changes nothing. Writes and syncs @p kwinrc when it changed.
 */
ConfigResult migrateConfig(KConfig &kwinrc);

/// The action id the old version used for the global shortcut @p id ("InfiniteDesktop ZoomIn").
QString legacyShortcutId(const QString &id);

/// Moves @p oldPath (file or folder) to @p newPath if the new one does not exist yet:
/// a rename, or a copy if that fails (the old one is then kept). True if it was taken over.
bool migratePath(const QString &oldPath, const QString &newPath);

/**
 * Moves the files of the old name inside the given base folders: the canvas state, notes
 * and the app configs (kde-infinit-desktop/ and kde-infinit-desktop-*rc).
 * Returns the paths that were taken over.
 */
QStringList migrateUserFiles(const QStringList &baseDirectories);

/// migrateUserFiles() for the user's state, data and config folders.
QStringList migrateUserFiles();

} // namespace InfiniteDesktop::Legacy
