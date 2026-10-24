/*
    KDE Infinit Desktop - infinite canvas for KWin
    SPDX-FileCopyrightText: 2026 KDE Infinit Desktop contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/
#include "dbusinterface.h"
#include "infinitdesktopeffect.h"
#include "logging.h"

#include "core/output.h"
#include "effect/effecthandler.h"

#include <QDBusConnection>

namespace InfinitDesktop
{

namespace
{
const QString ObjectPath = QStringLiteral("/InfinitDesktop");
}

DBusInterface::DBusInterface(InfinitDesktopEffect *effect)
    : QObject(effect)
    , m_effect(effect)
{
    m_registered = QDBusConnection::sessionBus().registerObject(ObjectPath, this,
                                                                QDBusConnection::ExportScriptableSlots | QDBusConnection::ExportScriptableSignals
                                                                    | QDBusConnection::ExportScriptableProperties);
    if (!m_registered) {
        qCWarning(KWIN_INFINITDESKTOP) << "could not register D-Bus object" << ObjectPath;
    }
    connect(effect, &InfinitDesktopEffect::statusChanged, this, &DBusInterface::statusChanged);
}

DBusInterface::~DBusInterface()
{
    if (m_registered) {
        QDBusConnection::sessionBus().unregisterObject(ObjectPath);
    }
}

bool DBusInterface::safeMode() const
{
    return m_effect->isSafeMode();
}

bool DBusInterface::paused() const
{
    return m_effect->isPaused();
}

QString DBusInterface::version() const
{
    return QStringLiteral(INFINITDESKTOP_VERSION_STRING);
}

void DBusInterface::leaveSafeMode()
{
    m_effect->leaveSafeMode();
}

void DBusInterface::setPaused(bool paused)
{
    m_effect->setPaused(paused);
}

void DBusInterface::reloadSettings()
{
    m_effect->reconfigure(KWin::Effect::ReconfigureAll);
}

QString DBusInterface::state() const
{
    return m_effect->stateJson();
}

void DBusInterface::pan(double dx, double dy)
{
    if (!m_effect->isOperational()) {
        return;
    }
    if (Canvas *canvas = m_effect->currentCanvas(m_effect->pointerOutput())) {
        m_effect->stopMotion(*canvas);
        m_effect->panBy(*canvas, QPointF(-dx, -dy));
        m_effect->commitIfDirty(*canvas);
    }
}

void DBusInterface::zoomBy(double factor)
{
    if (!m_effect->isOperational() || !(factor > 0.0)) {
        return;
    }
    KWin::LogicalOutput *output = m_effect->pointerOutput();
    if (Canvas *canvas = m_effect->currentCanvas(output)) {
        const KWin::RectF geometry = output->geometryF();
        m_effect->zoomBy(*canvas, QPointF(geometry.x() + geometry.width() / 2, geometry.y() + geometry.height() / 2), factor, false);
        m_effect->commitIfDirty(*canvas);
    }
}

void DBusInterface::setZoom(double zoom)
{
    if (!m_effect->isOperational() || !(zoom > 0.0)) {
        return;
    }
    KWin::LogicalOutput *output = m_effect->pointerOutput();
    if (Canvas *canvas = m_effect->currentCanvas(output)) {
        const KWin::RectF geometry = output->geometryF();
        m_effect->setZoom(*canvas, QPointF(geometry.x() + geometry.width() / 2, geometry.y() + geometry.height() / 2), zoom, false);
        m_effect->commitIfDirty(*canvas);
    }
}

void DBusInterface::setCamera(double x, double y, double zoom)
{
    if (!m_effect->isOperational() || !(zoom > 0.0)) {
        return;
    }
    if (Canvas *canvas = m_effect->currentCanvas(m_effect->pointerOutput())) {
        const EffectSettings &settings = m_effect->settings();
        m_effect->stopMotion(*canvas);
        m_effect->setCamera(*canvas, Camera{QPointF(x, y), clampZoom(zoom, settings.minZoom, settings.maxZoom, settings.snapZoomToOne ? 0.02 : 0.0)});
        m_effect->commitIfDirty(*canvas);
    }
}

void DBusInterface::zoomToFit()
{
    if (m_effect->isOperational()) {
        if (Canvas *canvas = m_effect->currentCanvas(m_effect->pointerOutput())) {
            m_effect->zoomToFit(*canvas, false);
        }
    }
}

void DBusInterface::resetZoom()
{
    setZoom(1.0);
}

void DBusInterface::goHome()
{
    if (m_effect->isOperational()) {
        if (Canvas *canvas = m_effect->currentCanvas(m_effect->pointerOutput())) {
            m_effect->goHome(*canvas, false);
        }
    }
}

QString DBusInterface::windowPlacements() const
{
    return m_effect->windowPlacementsJson();
}

void DBusInterface::forgetWindowPlacements()
{
    m_effect->forgetWindowPlacements();
}

void DBusInterface::forgetApplicationPlacements(const QString &application)
{
    m_effect->forgetWindowPlacements(application);
}

} // namespace InfinitDesktop

#include "moc_dbusinterface.cpp"
