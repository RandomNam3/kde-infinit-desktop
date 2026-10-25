/*
    KDE Infinit Desktop - infinite canvas for KWin
    SPDX-FileCopyrightText: 2026 KDE Infinit Desktop contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/
#pragma once

#include <QObject>
#include <QString>

namespace InfinitDesktop
{

class InfinitDesktopEffect;

/**
 * D-Bus interface org.kde.InfinitDesktop on /InfinitDesktop (service org.kde.KWin),
 * see src/shared/org.kde.InfinitDesktop.xml. Used by the settings app and for
 * scripting/automated tests. Navigation calls act on the screen under the pointer
 * and are applied immediately (no animation), so scripts get deterministic results.
 */
class DBusInterface : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.InfinitDesktop")
    Q_PROPERTY(bool safeMode READ safeMode NOTIFY statusChanged)
    Q_PROPERTY(bool paused READ paused NOTIFY statusChanged)

public:
    explicit DBusInterface(InfinitDesktopEffect *effect);
    ~DBusInterface() override;

    bool safeMode() const;
    bool paused() const;

public Q_SLOTS:
    Q_SCRIPTABLE QString version() const;
    Q_SCRIPTABLE void leaveSafeMode();
    Q_SCRIPTABLE void setPaused(bool paused);
    Q_SCRIPTABLE void reloadSettings();
    Q_SCRIPTABLE QString state() const;
    Q_SCRIPTABLE void pan(double dx, double dy);
    Q_SCRIPTABLE void zoomBy(double factor);
    Q_SCRIPTABLE void setZoom(double zoom);
    Q_SCRIPTABLE void setCamera(double x, double y, double zoom);
    Q_SCRIPTABLE void zoomToFit();
    Q_SCRIPTABLE void resetZoom();
    Q_SCRIPTABLE void goHome();
    Q_SCRIPTABLE QString windowPlacements() const;
    Q_SCRIPTABLE void forgetWindowPlacements();
    Q_SCRIPTABLE void forgetApplicationPlacements(const QString &application);

    // Notes (called by the companion app), groups, areas and the map
    Q_SCRIPTABLE void expectNote(const QString &id);
    Q_SCRIPTABLE QString noteState(const QString &id) const;
    Q_SCRIPTABLE void setNotePinned(const QString &id, bool pinned, bool keepAbove);
    Q_SCRIPTABLE void forgetNote(const QString &id);
    Q_SCRIPTABLE void retainNotes(const QStringList &existing);
    Q_SCRIPTABLE void newNote();
    Q_SCRIPTABLE QString groups() const;
    Q_SCRIPTABLE bool setGroups(const QString &json);
    Q_SCRIPTABLE void openGroup(const QString &id);
    Q_SCRIPTABLE void saveGroupLayout(const QString &id);
    Q_SCRIPTABLE void newGroupFromSelection();
    Q_SCRIPTABLE QString areas() const;
    Q_SCRIPTABLE bool setAreas(const QString &json);
    Q_SCRIPTABLE void newAreaFromSelection();
    Q_SCRIPTABLE void showMap();

Q_SIGNALS:
    Q_SCRIPTABLE void statusChanged();
    Q_SCRIPTABLE void groupsChanged();
    Q_SCRIPTABLE void areasChanged();
    Q_SCRIPTABLE void notePinnedChanged(const QString &id, bool pinned, bool keepAbove);

private:
    InfinitDesktopEffect *const m_effect;
    bool m_registered = false;
};

} // namespace InfinitDesktop
