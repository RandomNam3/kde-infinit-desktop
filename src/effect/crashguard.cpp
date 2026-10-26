/*
    KDE Infinite Desktop - infinite canvas for KWin
    SPDX-FileCopyrightText: 2026 KDE Infinite Desktop contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/
#include "crashguard.h"
#include "logging.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>

namespace InfiniteDesktop
{

CrashGuard::CrashGuard(QObject *parent)
    : QObject(parent)
{
    m_stableTimer.setSingleShot(true);
    m_stableTimer.setInterval(StableAfterMs);
    connect(&m_stableTimer, &QTimer::timeout, this, [this]() {
        if (m_armed) {
            writeCounter(0);
            qCDebug(KWIN_INFINITEDESKTOP) << "crash guard: run considered stable";
        }
    });
}

CrashGuard::~CrashGuard() = default;

QString CrashGuard::stateFilePath()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation);
#else
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
#endif
    return base + QStringLiteral("/kde-infinite-desktop/crashguard");
}

int CrashGuard::readCounter() const
{
    QFile file(stateFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        return 0;
    }
    bool ok = false;
    const int value = QString::fromLatin1(file.readAll()).trimmed().toInt(&ok);
    return ok && value > 0 ? value : 0;
}

void CrashGuard::writeCounter(int value) const
{
    const QString path = stateFilePath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        qCWarning(KWIN_INFINITEDESKTOP) << "crash guard: cannot write" << path;
        return;
    }
    file.write(QByteArray::number(value));
    file.commit();
}

bool CrashGuard::start(bool enabled)
{
    const int counter = readCounter();
    if (enabled && counter >= Threshold) {
        m_safeMode = true;
        m_armed = false;
        qCWarning(KWIN_INFINITEDESKTOP) << "KWin crashed" << counter << "times in a row while KDE Infinite Desktop was active, starting in safe mode";
        return true;
    }
    m_safeMode = false;
    m_armed = true;
    // Written synchronously: if KWin crashes during startup the increment must be on disk.
    writeCounter(counter + 1);
    m_stableTimer.start();
    return false;
}

void CrashGuard::leaveSafeMode()
{
    m_safeMode = false;
    m_armed = true;
    writeCounter(1);
    m_stableTimer.start();
}

void CrashGuard::markClean()
{
    if (m_armed) {
        m_stableTimer.stop();
        writeCounter(0);
    }
    m_armed = false;
}

} // namespace InfiniteDesktop
