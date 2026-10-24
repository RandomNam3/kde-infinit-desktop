/*
    KDE Infinit Desktop - infinite canvas for KWin
    SPDX-FileCopyrightText: 2026 KDE Infinit Desktop contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/
#pragma once

#include <KLazyLocalizedString>

#include <QList>

namespace InfinitDesktop
{

/**
 * UI metadata for the settings defined in infinitdesktop.kcfg.
 *
 * Labels, explanations, defaults and ranges live in the kcfg file; this table
 * only adds what the kcfg format cannot express: the category an entry is
 * shown in, its unit, search keywords and dependencies between entries.
 * Both the System Settings module and the settings app are generated from it.
 */
struct SettingsCategory
{
    const char *id;
    KLazyLocalizedString title;
    KLazyLocalizedString subtitle;
    /// Name of the icon shipped with the settings UIs (see src/app/icons).
    const char *icon;
};

struct SettingsEntryMeta
{
    /// Entry name in infinitdesktop.kcfg.
    const char *key;
    const char *category;
    /// Unit shown next to numbers: "px", "px/s", "%", "ms" or nullptr.
    const char *unit;
    /// Additional search terms (translated, comma separated).
    KLazyLocalizedString keywords;
    /// Entry that has to be enabled for this one to make sense, or nullptr.
    /// For Bool entries "enabled" means true, for Enum entries any choice except the first.
    const char *dependsOn;
};

/// The special category id of the shortcut page.
inline constexpr const char *ShortcutsCategoryId = "shortcuts";

const QList<SettingsCategory> &settingsCategories();
const QList<SettingsEntryMeta> &settingsEntries();
const SettingsEntryMeta *findSettingsEntry(const char *key);

} // namespace InfinitDesktop
