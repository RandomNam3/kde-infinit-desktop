/*
    KDE Infinite Desktop - infinite canvas for KWin
    SPDX-FileCopyrightText: 2026 KDE Infinite Desktop contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/
#include "themecontroller.h"

#include <QEvent>
#include <QGuiApplication>
#include <QJSEngine>
#include <QPalette>
#include <QStyleHints>

namespace InfiniteDesktop
{

namespace
{
ThemeController *s_themeInstance = nullptr;

// Breeze blue, used when the platform does not provide a real accent color.
const QColor s_fallbackAccent(0x3d, 0xae, 0xe9);
}

ThemeController::ThemeController(QObject *parent)
    : QObject(parent)
{
    qApp->installEventFilter(this);
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, &ThemeController::update);
#endif
    update();
}

ThemeController *ThemeController::instance()
{
    if (!s_themeInstance) {
        s_themeInstance = new ThemeController(QCoreApplication::instance());
    }
    return s_themeInstance;
}

ThemeController *ThemeController::create(QQmlEngine *qmlEngine, QJSEngine *jsEngine)
{
    Q_UNUSED(qmlEngine)
    Q_UNUSED(jsEngine)
    ThemeController *controller = instance();
    QJSEngine::setObjectOwnership(controller, QJSEngine::CppOwnership);
    return controller;
}

bool ThemeController::dark() const
{
    return m_dark;
}

QColor ThemeController::accent() const
{
    return m_accent;
}

bool ThemeController::animations() const
{
    return !m_screenshotMode;
}

bool ThemeController::screenshotMode() const
{
    return m_screenshotMode;
}

void ThemeController::setScreenshotMode(bool screenshotMode)
{
    if (m_screenshotMode != screenshotMode) {
        m_screenshotMode = screenshotMode;
        Q_EMIT changed();
    }
}

QString ThemeController::forcedScheme() const
{
    return m_forcedScheme;
}

void ThemeController::setForcedScheme(const QString &scheme)
{
    if (m_forcedScheme != scheme) {
        m_forcedScheme = scheme;
        update();
    }
}

void ThemeController::setForcedAccent(const QColor &accent)
{
    m_forcedAccent = accent;
    update();
}

bool ThemeController::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == qApp && event->type() == QEvent::ApplicationPaletteChange) {
        update();
    }
    return QObject::eventFilter(watched, event);
}

void ThemeController::update()
{
    const QPalette palette = QGuiApplication::palette();
    bool dark = palette.color(QPalette::Window).lightnessF() < 0.5;
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    const Qt::ColorScheme scheme = QGuiApplication::styleHints()->colorScheme();
    if (scheme != Qt::ColorScheme::Unknown) {
        dark = scheme == Qt::ColorScheme::Dark;
    }
#endif
    if (m_forcedScheme == QLatin1String("dark")) {
        dark = true;
    } else if (m_forcedScheme == QLatin1String("light")) {
        dark = false;
    }

    QColor accent = m_forcedAccent.isValid() ? m_forcedAccent : palette.color(QPalette::Active, QPalette::Highlight);
    // Qt's built-in default palette is not a user choice; prefer the Plasma default then.
    if (!accent.isValid() || accent == QColor(0x30, 0x8c, 0xc6)) {
        accent = s_fallbackAccent;
    }
    accent.setAlpha(255);

    if (dark != m_dark || accent != m_accent) {
        m_dark = dark;
        m_accent = accent;
        Q_EMIT changed();
    }
}

} // namespace InfiniteDesktop
