/*
    KDE Infinite Desktop - infinite canvas for KWin
    SPDX-FileCopyrightText: 2026 KDE Infinite Desktop contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/
#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

namespace InfiniteDesktop
{

/**
 * Detects crash loops of KWin while the plugin is active.
 *
 * Nothing running inside the compositor can catch a segmentation fault, so
 * instead of trying to survive a crash the guard makes sure a crash cannot
 * lock the user out: every start increments a counter on disk, a clean
 * shutdown or StableAfter of uninterrupted runtime resets it. If KWin keeps
 * crashing shortly after start (counter >= Threshold) the plugin comes up in
 * safe mode and stays inactive until the user leaves safe mode explicitly.
 */
class CrashGuard : public QObject
{
    Q_OBJECT

public:
    static constexpr int Threshold = 3;
    static constexpr int StableAfterMs = 30000;

    explicit CrashGuard(QObject *parent = nullptr);
    ~CrashGuard() override;

    /// Reads the counter and arms the guard. Returns true if safe mode is required.
    bool start(bool enabled);

    bool isSafeMode() const
    {
        return m_safeMode;
    }

    /// Resets the counter and leaves safe mode.
    void leaveSafeMode();

    /// Marks the current run as clean (called on normal shutdown).
    void markClean();

    static QString stateFilePath();

private:
    int readCounter() const;
    void writeCounter(int value) const;

    QTimer m_stableTimer;
    bool m_safeMode = false;
    bool m_armed = false;
};

} // namespace InfiniteDesktop
