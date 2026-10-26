/*
    KDE Infinite Desktop - infinite canvas for KWin
    SPDX-FileCopyrightText: 2026 KDE Infinite Desktop contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/
#pragma once

#include <KConfigWatcher>

#include <QObject>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

#include <functional>

class QDBusMessage;
class QDBusServiceWatcher;
class QJSEngine;
class QQmlEngine;

namespace InfiniteDesktop
{

/**
 * State of the KWin effect, queried over D-Bus.
 *
 * Everything is asynchronous and tolerant: KWin may not run at all (for
 * example in a nested or X11 session), the effect may not be installed or
 * may fail to load (built against another KWin version).
 *
 * @c state is one of "unknown", "nokwin", "notinstalled", "disabled",
 * "loadfailed", "safemode", "paused" and "active".
 */
class EffectStatus : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Effect)
    QML_SINGLETON
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(bool kwinReachable READ kwinReachable NOTIFY changed)
    Q_PROPERTY(bool installed READ installed NOTIFY changed)
    Q_PROPERTY(bool enabled READ enabled NOTIFY changed)
    Q_PROPERTY(bool loaded READ loaded NOTIFY changed)
    Q_PROPERTY(bool supported READ supported NOTIFY changed)
    Q_PROPERTY(bool safeMode READ safeMode NOTIFY changed)
    Q_PROPERTY(bool paused READ paused NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString effectVersion READ effectVersion NOTIFY changed)
    Q_PROPERTY(QString kwinVersion READ kwinVersion NOTIFY changed)
    Q_PROPERTY(QString appVersion READ appVersion CONSTANT)

public:
    explicit EffectStatus(QObject *parent);
    ~EffectStatus() override;

    static EffectStatus *instance();
    static EffectStatus *create(QQmlEngine *qmlEngine, QJSEngine *jsEngine);

    QString state() const;
    bool kwinReachable() const;
    bool installed() const;
    bool enabled() const;
    bool loaded() const;
    bool supported() const;
    bool safeMode() const;
    bool paused() const;
    bool busy() const;
    QString effectVersion() const;
    QString kwinVersion() const;
    QString appVersion() const;

    /// Turns the effect on or off (kwinrc [Plugins] + load/unloadEffect).
    Q_INVOKABLE void setEnabled(bool enabled);
    Q_INVOKABLE void leaveSafeMode();
    Q_INVOKABLE void setPaused(bool paused);

    /// Screenshot mode: pretend a fixed state, no D-Bus at all.
    void setDemoState(const QString &state);

public Q_SLOTS:
    void refresh();

Q_SIGNALS:
    void changed();
    void actionFailed(const QString &message);

private:
    using ReplyHandler = std::function<void(const QDBusMessage &)>;
    void call(const QDBusMessage &message, ReplyHandler handler);
    void finishRefresh();
    void computeState();
    void fetchKWinVersion();
    bool configEnabled(bool *hasKey) const;

    QString m_state = QStringLiteral("unknown");
    bool m_kwinReachable = false;
    bool m_installed = false;
    bool m_enabled = false;
    bool m_loaded = false;
    bool m_supported = true;
    bool m_safeMode = false;
    bool m_paused = false;
    bool m_busy = false;
    QString m_effectVersion;
    QString m_kwinVersion;

    bool m_demo = false;
    bool m_refreshing = false;
    bool m_refreshAgain = false;
    int m_pendingReplies = 0;
    // Collected while a refresh is running.
    bool m_newReachable = false;
    bool m_newInstalled = false;
    bool m_newLoaded = false;
    bool m_newSupported = true;
    bool m_newSafeMode = false;
    bool m_newPaused = false;
    QString m_newEffectVersion;

    QTimer m_pollTimer;
    QDBusServiceWatcher *m_serviceWatcher = nullptr;
    KConfigWatcher::Ptr m_configWatcher;
};

} // namespace InfiniteDesktop
