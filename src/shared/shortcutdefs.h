/*
    KDE Infinit Desktop - infinite canvas for KWin
    SPDX-FileCopyrightText: 2026 KDE Infinit Desktop contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/
#pragma once

#include <KLazyLocalizedString>

#include <QKeySequence>
#include <QList>

namespace InfinitDesktop
{

/// Global shortcuts are registered by the KWin effect under this KGlobalAccel component.
inline constexpr const char *ShortcutComponent = "kwin";
inline constexpr const char *ShortcutComponentDisplayName = "KWin";

enum class ShortcutAction {
    ZoomIn,
    ZoomOut,
    ZoomReset,
    Overview,
    Home,
    CenterWindow,
    PanLeft,
    PanRight,
    PanUp,
    PanDown,
    FocusLeft,
    FocusRight,
    FocusUp,
    FocusDown,
    GotoBookmark1,
    GotoBookmark2,
    GotoBookmark3,
    GotoBookmark4,
    SaveBookmark1,
    SaveBookmark2,
    SaveBookmark3,
    SaveBookmark4,
    TogglePin,
    TogglePinAbove,
    ToggleEnabled,
};

struct ShortcutDefinition
{
    ShortcutAction action;
    /// Stable action id (QAction::objectName), never translate or change it.
    const char *id;
    KLazyLocalizedString text;
    KLazyLocalizedString description;
    /// Group id used by the settings UIs ("zoom", "pan", "focus", "bookmarks", "windows").
    const char *group;
    QList<QKeySequence> defaultShortcuts;
};

/// All shortcuts of the plugin, in display order.
const QList<ShortcutDefinition> &shortcutDefinitions();

const ShortcutDefinition *findShortcut(ShortcutAction action);

} // namespace InfinitDesktop
