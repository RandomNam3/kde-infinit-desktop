/*
    KDE Infinit Desktop - infinite canvas for KWin
    SPDX-FileCopyrightText: 2026 KDE Infinit Desktop contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/
#include "effectstatus.h"

#include <KConfigGroup>
#include <KLocalizedString>
#include <KSharedConfig>

#include <QCoreApplication>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusServiceWatcher>
#include <QDBusVariant>
#include <QJSEngine>
#include <QRegularExpression>

namespace InfinitDesktop
{

namespace
{

EffectStatus *s_statusInstance = nullptr;

const QString s_effectId = QStringLiteral("infinitdesktop");
const QString s_kwinService = QStringLiteral("org.kde.KWin");
const QString s_effectsPath = QStringLiteral("/Effects");
const QString s_effectsInterface = QStringLiteral("org.kde.kwin.Effects");
const QString s_pluginPath = QStringLiteral("/InfinitDesktop");
const QString s_pluginInterface = QStringLiteral("org.kde.InfinitDesktop");
const QString s_propertiesInterface = QStringLiteral("org.freedesktop.DBus.Properties");
const QString s_pluginsGroup = QStringLiteral("Plugins");
const QString s_enabledKey = QStringLiteral("infinitdesktopEnabled");

constexpr int PollIntervalMs = 5000;

QDBusMessage effectsCall(const QString &method)
{
    QDBusMessage message = QDBusMessage::createMethodCall(s_kwinService, s_effectsPath, s_effectsInterface, method);
    message << s_effectId;
    return message;
}

QVariant firstArgument(const QDBusMessage &reply)
{
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        return QVariant();
    }
    QVariant value = reply.arguments().constFirst();
    if (value.metaType() == QMetaType::fromType<QDBusVariant>()) {
        value = value.value<QDBusVariant>().variant();
    }
    return value;
}

} // namespace

EffectStatus::EffectStatus(QObject *parent)
    : QObject(parent)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (bus.isConnected()) {
        m_serviceWatcher = new QDBusServiceWatcher(s_kwinService, bus, QDBusServiceWatcher::WatchForOwnerChange, this);
        connect(m_serviceWatcher, &QDBusServiceWatcher::serviceOwnerChanged, this, &EffectStatus::refresh);
        bus.connect(s_kwinService, s_pluginPath, s_pluginInterface, QStringLiteral("statusChanged"), this, SLOT(refresh()));
    }

    m_configWatcher = KConfigWatcher::create(KSharedConfig::openConfig(QStringLiteral("kwinrc")));
    connect(m_configWatcher.data(), &KConfigWatcher::configChanged, this, [this](const KConfigGroup &group) {
        if (group.name() == s_pluginsGroup) {
            refresh();
        }
    });

    m_pollTimer.setInterval(PollIntervalMs);
    connect(&m_pollTimer, &QTimer::timeout, this, &EffectStatus::refresh);
    m_pollTimer.start();

    QTimer::singleShot(0, this, &EffectStatus::refresh);
}

EffectStatus::~EffectStatus() = default;

EffectStatus *EffectStatus::instance()
{
    if (!s_statusInstance) {
        s_statusInstance = new EffectStatus(QCoreApplication::instance());
    }
    return s_statusInstance;
}

EffectStatus *EffectStatus::create(QQmlEngine *qmlEngine, QJSEngine *jsEngine)
{
    Q_UNUSED(qmlEngine)
    Q_UNUSED(jsEngine)
    EffectStatus *status = instance();
    QJSEngine::setObjectOwnership(status, QJSEngine::CppOwnership);
    return status;
}

QString EffectStatus::state() const
{
    return m_state;
}

bool EffectStatus::kwinReachable() const
{
    return m_kwinReachable;
}

bool EffectStatus::installed() const
{
    return m_installed;
}

bool EffectStatus::enabled() const
{
    return m_enabled;
}

bool EffectStatus::loaded() const
{
    return m_loaded;
}

bool EffectStatus::supported() const
{
    return m_supported;
}

bool EffectStatus::safeMode() const
{
    return m_safeMode;
}

bool EffectStatus::paused() const
{
    return m_paused;
}

bool EffectStatus::busy() const
{
    return m_busy;
}

QString EffectStatus::effectVersion() const
{
    return m_effectVersion;
}

QString EffectStatus::kwinVersion() const
{
    return m_kwinVersion;
}

QString EffectStatus::appVersion() const
{
    return QStringLiteral(INFINITDESKTOP_VERSION_STRING);
}

bool EffectStatus::configEnabled(bool *hasKey) const
{
    KSharedConfig::Ptr config = KSharedConfig::openConfig(QStringLiteral("kwinrc"));
    config->reparseConfiguration();
    const KConfigGroup plugins(config, s_pluginsGroup);
    *hasKey = plugins.hasKey(s_enabledKey);
    return plugins.readEntry(s_enabledKey, false);
}

void EffectStatus::call(const QDBusMessage &message, ReplyHandler handler)
{
    ++m_pendingReplies;
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, handler](QDBusPendingCallWatcher *call) {
        call->deleteLater();
        handler(call->reply());
        if (--m_pendingReplies == 0) {
            finishRefresh();
        }
    });
}

void EffectStatus::refresh()
{
    if (m_demo) {
        return;
    }
    if (m_refreshing) {
        m_refreshAgain = true;
        return;
    }
    if (!QDBusConnection::sessionBus().isConnected()) {
        m_newReachable = false;
        m_newInstalled = m_newLoaded = m_newSafeMode = m_newPaused = false;
        m_refreshing = true;
        finishRefresh();
        return;
    }

    m_refreshing = true;
    m_newReachable = false;
    m_newInstalled = false;
    m_newLoaded = false;
    m_newSupported = true;
    m_newSafeMode = false;
    m_newPaused = false;
    m_newEffectVersion.clear();

    QDBusMessage listOfEffects = QDBusMessage::createMethodCall(s_kwinService, s_effectsPath, s_propertiesInterface, QStringLiteral("Get"));
    listOfEffects << s_effectsInterface << QStringLiteral("listOfEffects");
    call(listOfEffects, [this](const QDBusMessage &reply) {
        if (reply.type() == QDBusMessage::ReplyMessage) {
            m_newReachable = true;
            m_newInstalled = firstArgument(reply).toStringList().contains(s_effectId);
        }
    });
    call(effectsCall(QStringLiteral("isEffectLoaded")), [this](const QDBusMessage &reply) {
        m_newLoaded = firstArgument(reply).toBool();
    });
    call(effectsCall(QStringLiteral("isEffectSupported")), [this](const QDBusMessage &reply) {
        const QVariant value = firstArgument(reply);
        m_newSupported = !value.isValid() || value.toBool();
    });

    QDBusMessage properties = QDBusMessage::createMethodCall(s_kwinService, s_pluginPath, s_propertiesInterface, QStringLiteral("GetAll"));
    properties << s_pluginInterface;
    call(properties, [this](const QDBusMessage &reply) {
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
            return;
        }
        const QVariantMap map = qdbus_cast<QVariantMap>(reply.arguments().constFirst());
        m_newSafeMode = map.value(QStringLiteral("safeMode")).toBool();
        m_newPaused = map.value(QStringLiteral("paused")).toBool();
    });
    call(QDBusMessage::createMethodCall(s_kwinService, s_pluginPath, s_pluginInterface, QStringLiteral("version")), [this](const QDBusMessage &reply) {
        m_newEffectVersion = firstArgument(reply).toString();
    });
}

void EffectStatus::finishRefresh()
{
    m_refreshing = false;
    m_kwinReachable = m_newReachable;
    m_installed = m_newInstalled;
    m_loaded = m_newLoaded;
    m_supported = m_newSupported;
    m_safeMode = m_loaded && m_newSafeMode;
    m_paused = m_loaded && m_newPaused;
    m_effectVersion = m_loaded ? m_newEffectVersion : QString();
    bool hasKey = false;
    const bool configured = configEnabled(&hasKey);
    m_enabled = hasKey ? configured : m_loaded;
    computeState();
    if (m_refreshAgain) {
        m_refreshAgain = false;
        refresh();
    }
}

void EffectStatus::computeState()
{
    QString state;
    if (!m_kwinReachable) {
        state = QStringLiteral("nokwin");
    } else if (!m_installed) {
        state = QStringLiteral("notinstalled");
    } else if (m_loaded && m_safeMode) {
        state = QStringLiteral("safemode");
    } else if (m_loaded && m_paused) {
        state = QStringLiteral("paused");
    } else if (m_loaded) {
        state = QStringLiteral("active");
    } else if (m_enabled || !m_supported) {
        state = QStringLiteral("loadfailed");
    } else {
        state = QStringLiteral("disabled");
    }
    if (state == QLatin1String("loadfailed") && m_kwinVersion.isEmpty()) {
        fetchKWinVersion();
    }
    m_state = state;
    Q_EMIT changed();
}

void EffectStatus::fetchKWinVersion()
{
    const QDBusMessage message = QDBusMessage::createMethodCall(s_kwinService, QStringLiteral("/KWin"), QStringLiteral("org.kde.KWin"), QStringLiteral("supportInformation"));
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *call) {
        call->deleteLater();
        static const QRegularExpression versionLine(QStringLiteral("KWin version:\\s*(\\S+)"));
        const QRegularExpressionMatch match = versionLine.match(firstArgument(call->reply()).toString());
        if (match.hasMatch() && m_kwinVersion != match.captured(1)) {
            m_kwinVersion = match.captured(1);
            Q_EMIT changed();
        }
    });
}

void EffectStatus::setEnabled(bool enabled)
{
    if (m_demo) {
        m_state = enabled ? QStringLiteral("active") : QStringLiteral("disabled");
        m_enabled = m_loaded = enabled;
        Q_EMIT changed();
        return;
    }
    KSharedConfig::Ptr config = KSharedConfig::openConfig(QStringLiteral("kwinrc"));
    KConfigGroup plugins(config, s_pluginsGroup);
    // KWin watches this key and loads/unloads the effect by itself.
    plugins.writeEntry(s_enabledKey, enabled, KConfig::Notify);
    config->sync();

    m_busy = true;
    Q_EMIT changed();
    const QDBusMessage message = effectsCall(enabled ? QStringLiteral("loadEffect") : QStringLiteral("unloadEffect"));
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, enabled](QDBusPendingCallWatcher *call) {
        call->deleteLater();
        if (call->isError() && enabled) {
            Q_EMIT actionFailed(i18n("KWin could not be asked to load the plugin: %1", call->error().message()));
        }
        // Give KWin a moment to finish (un)loading, the answer of loadEffect alone is not reliable.
        QTimer::singleShot(400, this, [this] {
            m_busy = false;
            refresh();
        });
    });
}

void EffectStatus::leaveSafeMode()
{
    if (m_demo) {
        m_state = QStringLiteral("active");
        m_safeMode = false;
        Q_EMIT changed();
        return;
    }
    m_busy = true;
    Q_EMIT changed();
    const QDBusMessage message = QDBusMessage::createMethodCall(s_kwinService, s_pluginPath, s_pluginInterface, QStringLiteral("leaveSafeMode"));
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *call) {
        call->deleteLater();
        if (call->isError()) {
            Q_EMIT actionFailed(i18n("Could not leave safe mode: %1", call->error().message()));
        }
        m_busy = false;
        refresh();
    });
}

void EffectStatus::setPaused(bool paused)
{
    if (m_demo) {
        m_state = paused ? QStringLiteral("paused") : QStringLiteral("active");
        m_paused = paused;
        Q_EMIT changed();
        return;
    }
    QDBusMessage message = QDBusMessage::createMethodCall(s_kwinService, s_pluginPath, s_pluginInterface, QStringLiteral("setPaused"));
    message << paused;
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *call) {
        call->deleteLater();
        if (call->isError()) {
            Q_EMIT actionFailed(i18n("Could not change the pause state: %1", call->error().message()));
        }
        refresh();
    });
}

void EffectStatus::setDemoState(const QString &state)
{
    m_demo = true;
    m_pollTimer.stop();
    m_state = state;
    m_kwinReachable = state != QLatin1String("nokwin");
    m_installed = m_kwinReachable && state != QLatin1String("notinstalled");
    m_loaded = state == QLatin1String("active") || state == QLatin1String("paused") || state == QLatin1String("safemode");
    m_enabled = m_loaded || state == QLatin1String("loadfailed");
    m_safeMode = state == QLatin1String("safemode");
    m_paused = state == QLatin1String("paused");
    m_effectVersion = m_loaded ? appVersion() : QString();
    m_kwinVersion = QStringLiteral("6.8.1");
    Q_EMIT changed();
}

} // namespace InfinitDesktop
