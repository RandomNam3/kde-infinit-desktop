/*
    KDE Infinite Desktop - infinite canvas for KWin
    SPDX-FileCopyrightText: 2026 KDE Infinite Desktop contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/
#pragma once

#include <KConfigWatcher>

#include <QColor>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>

#include <memory>

class QDBusServiceWatcher;
class QMenu;
class QQmlEngine;
class QQuickWindow;
class QSystemTrayIcon;

namespace InfiniteDesktop
{

class Note;
class NoteStore;

/**
 * The companion app: note windows, the notes icon in the system tray, and the D-Bus
 * interface org.kde.InfiniteDesktop.Companion on /Companion that the KWin effect calls
 * (new note, show / hide notes, announce the note windows again).
 *
 * Every note window is announced to the effect (expectNote) right before it is shown, so the
 * canvas knows which note it is and places it where it was. A window exists only while its
 * note is shown; hiding a note destroys its window.
 */
class Companion : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.InfiniteDesktop.Companion")

public:
    Companion(QQmlEngine *engine, NoteStore *store, QObject *parent = nullptr);
    ~Companion() override;

    /// Registers the D-Bus name; false if another companion has it already.
    bool registerService();
    /// Shows the notes that are not hidden, sets up the tray icon.
    void start();

public Q_SLOTS:
    Q_SCRIPTABLE void newNote();
    Q_SCRIPTABLE void toggleNotes();
    Q_SCRIPTABLE void setNotesVisible(bool visible);
    /// The canvas (re)started and does not know the open note windows: show them again.
    Q_SCRIPTABLE void reannounceNotes();
    /// JSON array of {id, title, color, hidden}.
    Q_SCRIPTABLE QString notes() const;
    Q_SCRIPTABLE void quit();

    // From the note windows
    Q_INVOKABLE void togglePin(const QString &id, bool keepAbove);
    Q_INVOKABLE void hideNote(const QString &id);
    Q_INVOKABLE void deleteNote(const QString &id);
    Q_INVOKABLE void showMap();
    Q_INVOKABLE void openSettings();
    /// Paper color of note color index `color`.
    Q_INVOKABLE QColor paperColor(int color) const;
    Q_INVOKABLE QString colorName(int color) const;

private Q_SLOTS:
    void onNotePinnedChangedSlot(const QString &id, bool pinned, bool keepAbove);
    void noteStateReceived(const QString &json);

private:
    void showWindow(Note *note);
    void destroyWindow(const QString &id);
    void announce(const QString &id);
    void callEffect(const QString &method, const QVariantList &arguments);
    void updateTray();
    void loadSettings();
    void refreshPinStates();

    QQmlEngine *const m_engine;
    NoteStore *const m_store;
    QHash<QString, QPointer<QQuickWindow>> m_windows;
    std::unique_ptr<QSystemTrayIcon> m_tray;
    std::unique_ptr<QMenu> m_menu;
    QDBusServiceWatcher *m_kwinWatcher = nullptr;
    KConfigWatcher::Ptr m_configWatcher;
    bool m_enabled = true;
    int m_defaultColor = 0;
    /// Windows hidden by us (not by the user closing them).
    bool m_closing = false;
};

} // namespace InfiniteDesktop
