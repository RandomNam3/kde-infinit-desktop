/*
    KDE Infinit Desktop - infinite canvas for KWin
    SPDX-FileCopyrightText: 2026 KDE Infinit Desktop contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/
#pragma once

#include "motion.h"

#include <QObject>
#include <QPointer>
#include <QPointF>

#include <optional>

namespace KWin
{
class EffectWindow;
}

namespace InfinitDesktop
{

class InfinitDesktopEffect;

/**
 * Optionally opens the Plasma application launcher at the mouse pointer.
 *
 * Plasma places the launcher popup next to its panel. When the Super key is
 * tapped on its own (or Alt+F1 is pressed) the input filter reports the pointer
 * position here; the next applet popup that appears shortly after is moved so
 * that it is centered on that position, clamped into the work area. If
 * plasmashell positions the popup again (it does so on resize) the position is
 * re-applied while the popup is open.
 */
class LauncherPositioner : public QObject
{
    Q_OBJECT

public:
    explicit LauncherPositioner(InfinitDesktopEffect *effect);
    ~LauncherPositioner() override;

    void noteLauncherRequest(const QPointF &cursor);

private:
    void windowAdded(KWin::EffectWindow *w);
    void place();

    InfinitDesktopEffect *const m_effect;
    struct Request
    {
        QPointF cursor;
        TimePoint time;
    };
    std::optional<Request> m_request;
    QPointer<KWin::EffectWindow> m_popup;
    QPointF m_anchor;
    bool m_moving = false;
};

} // namespace InfinitDesktop
