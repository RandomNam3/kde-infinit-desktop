/*
    KDE Infinite Desktop - infinite canvas for KWin
    SPDX-FileCopyrightText: 2026 KDE Infinite Desktop contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/
#include "launcherpositioner.h"
#include "infinitedesktopeffect.h"
#include "logging.h"

#include "core/output.h"
#include "effect/effecthandler.h"
#include "window.h"
#include "workspace.h"

#include <QScopedValueRollback>

using namespace std::chrono_literals;

namespace InfiniteDesktop
{

namespace
{
constexpr auto RequestLifetime = 1500ms;
}

LauncherPositioner::LauncherPositioner(InfiniteDesktopEffect *effect)
    : m_effect(effect)
{
    connect(KWin::effects, &KWin::EffectsHandler::windowAdded, this, &LauncherPositioner::windowAdded);
    connect(KWin::effects, &KWin::EffectsHandler::windowClosed, this, [this](KWin::EffectWindow *closed) {
        if (m_popup && closed == m_popup) {
            disconnect(m_popup, nullptr, this, nullptr);
            m_popup.clear();
        }
    });
}

LauncherPositioner::~LauncherPositioner() = default;

void LauncherPositioner::noteLauncherRequest(const QPointF &cursor)
{
    if (!m_effect->settings().launcherAtCursor) {
        return;
    }
    m_request = Request{cursor, Clock::now()};
}

void LauncherPositioner::windowAdded(KWin::EffectWindow *w)
{
    if (!m_request || !m_effect->settings().launcherAtCursor || !m_effect->isOperational()) {
        return;
    }
    if (Clock::now() - m_request->time > RequestLifetime) {
        m_request.reset();
        return;
    }
    // The launcher (Kickoff, Kicker, Application Dashboard, ...) is an applet popup of plasmashell.
    if (!w->isAppletPopup()) {
        return;
    }
    m_anchor = m_request->cursor;
    m_request.reset();

    if (m_popup) {
        disconnect(m_popup, nullptr, this, nullptr);
    }
    m_popup = w;
    connect(w, &KWin::EffectWindow::windowFrameGeometryChanged, this, [this]() {
        if (!m_moving) {
            place();
        }
    });
    place();
}

void LauncherPositioner::place()
{
    KWin::EffectWindow *popup = m_popup;
    KWin::Window *window = popup ? popup->window() : nullptr;
    if (!window || popup->isDeleted()) {
        return;
    }
    KWin::LogicalOutput *output = KWin::workspace()->outputAt(m_anchor);
    if (!output) {
        return;
    }
    const KWin::RectF areaF = KWin::effects->clientArea(KWin::MaximizeArea, output);
    const QRectF area(areaF.x(), areaF.y(), areaF.width(), areaF.height());
    const KWin::RectF frameF = popup->frameGeometry();
    const QSizeF size(frameF.width(), frameF.height());

    QPointF topLeft(m_anchor.x() - size.width() / 2.0, m_anchor.y() - size.height() / 2.0);
    topLeft.setX(std::clamp(topLeft.x(), area.left(), std::max(area.left(), area.right() - size.width())));
    topLeft.setY(std::clamp(topLeft.y(), area.top(), std::max(area.top(), area.bottom() - size.height())));
    topLeft = QPointF(std::round(topLeft.x()), std::round(topLeft.y()));

    if (QPointF(frameF.x(), frameF.y()) == topLeft) {
        return;
    }
    QScopedValueRollback<bool> guard(m_moving, true);
    window->move(topLeft);
    qCDebug(KWIN_INFINITEDESKTOP) << "moved launcher popup to" << topLeft;
}

} // namespace InfiniteDesktop

#include "moc_launcherpositioner.cpp"
