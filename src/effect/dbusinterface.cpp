/*
    KDE Infinite Desktop - infinite canvas for KWin
    SPDX-FileCopyrightText: 2026 KDE Infinite Desktop contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/
#include "dbusinterface.h"
#include "infinitedesktopeffect.h"
#include "logging.h"
#include "organizer.h"

#include "core/output.h"
#include "effect/effecthandler.h"

#include <QDBusConnection>

namespace InfiniteDesktop
{

namespace
{
const QString ObjectPath = QStringLiteral("/InfiniteDesktop");
}

DBusInterface::DBusInterface(InfiniteDesktopEffect *effect)
    : QObject(effect)
    , m_effect(effect)
{
    m_registered = QDBusConnection::sessionBus().registerObject(ObjectPath, this,
                                                                QDBusConnection::ExportScriptableSlots | QDBusConnection::ExportScriptableSignals
                                                                    | QDBusConnection::ExportScriptableProperties);
    if (!m_registered) {
        qCWarning(KWIN_INFINITEDESKTOP) << "could not register D-Bus object" << ObjectPath;
    }
    connect(effect, &InfiniteDesktopEffect::statusChanged, this, &DBusInterface::statusChanged);
    if (Organizer *organizer = effect->organizer()) {
        connect(organizer, &Organizer::groupsChanged, this, &DBusInterface::groupsChanged);
        connect(organizer, &Organizer::areasChanged, this, &DBusInterface::areasChanged);
        connect(organizer, &Organizer::notePinnedChanged, this, &DBusInterface::notePinnedChanged);
    }
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
    return QStringLiteral(INFINITEDESKTOP_VERSION_STRING);
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

void DBusInterface::expectNote(const QString &id)
{
    if (Organizer *organizer = m_effect->organizer()) {
        organizer->expectNote(id);
    }
}

QString DBusInterface::noteState(const QString &id) const
{
    Organizer *organizer = m_effect->organizer();
    return organizer ? organizer->noteState(id) : QString();
}

void DBusInterface::setNotePinned(const QString &id, bool pinned, bool keepAbove)
{
    if (Organizer *organizer = m_effect->organizer(); organizer && m_effect->isOperational()) {
        organizer->setNotePinned(id, pinned, keepAbove);
    }
}

void DBusInterface::forgetNote(const QString &id)
{
    if (Organizer *organizer = m_effect->organizer()) {
        organizer->forgetNote(id);
    }
}

void DBusInterface::retainNotes(const QStringList &existing)
{
    if (Organizer *organizer = m_effect->organizer()) {
        organizer->retainNotes(existing);
    }
}

void DBusInterface::newNote()
{
    if (Organizer *organizer = m_effect->organizer(); organizer && m_effect->isOperational()) {
        organizer->newNote(m_effect->pointerOutput(), KWin::effects->cursorPos());
    }
}

QString DBusInterface::groups() const
{
    Organizer *organizer = m_effect->organizer();
    return organizer ? organizer->groupsJson() : QString();
}

bool DBusInterface::setGroups(const QString &json)
{
    Organizer *organizer = m_effect->organizer();
    return organizer && organizer->setGroupsJson(json);
}

void DBusInterface::openGroup(const QString &id)
{
    if (Organizer *organizer = m_effect->organizer(); organizer && m_effect->isOperational()) {
        organizer->openGroup(id);
    }
}

void DBusInterface::saveGroupLayout(const QString &id)
{
    if (Organizer *organizer = m_effect->organizer(); organizer && m_effect->isOperational()) {
        organizer->saveGroupLayout(id);
    }
}

void DBusInterface::newGroupFromSelection()
{
    if (Organizer *organizer = m_effect->organizer(); organizer && m_effect->isOperational()) {
        organizer->startSelection(true, m_effect->pointerOutput());
    }
}

QString DBusInterface::areas() const
{
    Organizer *organizer = m_effect->organizer();
    return organizer ? organizer->areasJson() : QString();
}

bool DBusInterface::setAreas(const QString &json)
{
    Organizer *organizer = m_effect->organizer();
    return organizer && organizer->setAreasJson(json);
}

void DBusInterface::newAreaFromSelection()
{
    if (Organizer *organizer = m_effect->organizer(); organizer && m_effect->isOperational()) {
        organizer->startSelection(false, m_effect->pointerOutput());
    }
}

void DBusInterface::showMap()
{
    if (Organizer *organizer = m_effect->organizer(); organizer && m_effect->isOperational()) {
        organizer->toggleMap(m_effect->pointerOutput());
    }
}

} // namespace InfiniteDesktop

#include "moc_dbusinterface.cpp"
