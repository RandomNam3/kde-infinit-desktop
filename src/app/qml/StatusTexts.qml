// SPDX-FileCopyrightText: 2026 KDE Infinite Desktop contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Titles, explanations and colors for the states of EffectStatus.
pragma Singleton

import QtQuick
import InfiniteDesktop.Settings

QtObject {
    function title(state) {
        switch (state) {
        case "active":
            return i18nc("@label plugin state", "Active");
        case "paused":
            return i18nc("@label plugin state", "Paused");
        case "disabled":
            return i18nc("@label plugin state", "Turned off");
        case "safemode":
            return i18nc("@label plugin state", "Safe mode");
        case "loadfailed":
            return i18nc("@label plugin state", "Could not be loaded");
        case "notinstalled":
            return i18nc("@label plugin state", "Not installed");
        case "nokwin":
            return i18nc("@label plugin state", "KWin not reachable");
        default:
            return i18nc("@label plugin state", "Checking…");
        }
    }

    function description(state) {
        switch (state) {
        case "active":
            return i18n("Every screen is an infinite canvas: pan and zoom freely and place windows anywhere. Changes are applied immediately.");
        case "paused":
            return i18n("Mouse and touchpad gestures of the canvas are paused. Windows stay where they are and the desktop behaves normally.");
        case "disabled":
            return i18n("The plugin is installed but turned off. Turn it on to make every screen an infinite canvas.");
        case "safemode":
            return i18n("KWin crashed several times in a row while the plugin was active, so it was started in safe mode and stays inactive. Leave safe mode to use the canvas again.");
        case "loadfailed":
            return Effect.kwinVersion.length > 0
                ? i18n("KWin %1 could not load the plugin. This usually happens after a KWin update: rebuild and reinstall KDE Infinite Desktop for this KWin version, then log out and back in.", Effect.kwinVersion)
                : i18n("KWin could not load the plugin. This usually happens after a KWin update: rebuild and reinstall KDE Infinite Desktop for this KWin version, then log out and back in.");
        case "notinstalled":
            return i18n("KWin does not know the plugin. Install the KDE Infinite Desktop KWin effect, then log out and back in.");
        case "nokwin":
            return i18n("KWin cannot be reached. Your settings are still saved and take effect in the next Plasma Wayland session.");
        default:
            return i18n("Asking KWin about the plugin…");
        }
    }

    function color(state) {
        switch (state) {
        case "active":
            return Theme.success;
        case "paused":
        case "notinstalled":
            return Theme.warning;
        case "safemode":
        case "loadfailed":
            return Theme.danger;
        default:
            return Theme.textTertiary;
        }
    }

    function isProblem(state) {
        return state === "safemode" || state === "loadfailed" || state === "notinstalled";
    }
}
