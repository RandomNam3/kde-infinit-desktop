/*
    KDE Infinit Desktop - infinite canvas for KWin
    SPDX-FileCopyrightText: 2026 KDE Infinit Desktop contributors
    SPDX-License-Identifier: GPL-3.0-or-later

    Standalone settings app with a custom Qt Quick UI.
*/
// The QML context declares methods called i18n(), so it has to be included before
// KLocalizedString defines the i18n() macros for TRANSLATION_DOMAIN.
#include <QtGlobal>
#include <ki18n_version.h>
#if KI18N_VERSION >= QT_VERSION_CHECK(6, 8, 0)
#include <KLocalizedQmlContext>
#else
#include <KLocalizedContext>
#endif

#include "effectstatus.h"
#include "globalshortcutsmodel.h"
#include "iconprovider.h"
#include "screenshotrunner.h"
#include "settingsmodel.h"
#include "shortcutmodel.h"
#include "themecontroller.h"
#include "updatechecker.h"
#include "windowplacements.h"
#include "workspacemodel.h"

#include <KAboutData>
#include <KLocalizedString>

#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTemporaryDir>

using namespace InfinitDesktop;

namespace
{

void setupLocalization(QQmlApplicationEngine *engine)
{
#if KI18N_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    KLocalization::setupLocalizedContext(engine);
#else
    auto *context = new KLocalizedContext(engine);
    context->setTranslationDomain(QStringLiteral(TRANSLATION_DOMAIN));
    engine->rootContext()->setContextObject(context);
#endif
}

QSize parseSize(const QString &text)
{
    const QStringList parts = text.split(QLatin1Char('x'));
    if (parts.size() != 2) {
        return QSize();
    }
    return QSize(parts.at(0).toInt(), parts.at(1).toInt());
}

} // namespace

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    KLocalizedString::setApplicationDomain(QByteArrayLiteral(TRANSLATION_DOMAIN));

    KAboutData about(QStringLiteral("kde-infinit-desktop-settings"),
                     i18nc("@title application name", "KDE Infinit Desktop"),
                     QStringLiteral(INFINITDESKTOP_VERSION_STRING),
                     i18n("Settings for the infinite canvas of KWin"),
                     KAboutLicense::GPL_V3,
                     i18n("© 2026 KDE Infinit Desktop contributors"));
    about.setDesktopFileName(QStringLiteral("kde-infinit-desktop-settings"));
    KAboutData::setApplicationData(about);

    QCommandLineParser parser;
    about.setupCommandLine(&parser);
    const QCommandLineOption pageOption(QStringLiteral("page"),
                                        i18n("Open the given page, for example “mouse” or “shortcuts”."),
                                        i18nc("@info:shell value name", "page"));
    const QCommandLineOption screenshotOption(QStringLiteral("screenshot"),
                                              i18n("Render every page into PNG files in <directory> and quit (for documentation)."),
                                              i18nc("@info:shell value name", "directory"));
    const QCommandLineOption screenshotSizeOption(QStringLiteral("screenshot-size"),
                                                  i18n("Window size for --screenshot, for example 1120x760."),
                                                  i18nc("@info:shell value name", "size"));
    parser.addOption(pageOption);
    parser.addOption(screenshotOption);
    parser.addOption(screenshotSizeOption);
    parser.process(app);
    about.processCommandLine(&parser);

    const bool screenshotMode = parser.isSet(screenshotOption);
    QTemporaryDir screenshotConfig;
    if (screenshotMode) {
        // Never touch the real kwinrc and never talk to KGlobalAccel while rendering screenshots.
        qputenv("XDG_CONFIG_HOME", QFile::encodeName(screenshotConfig.path()));
        ShortcutModel::setOffline(true);
        GlobalShortcutsModel::setDemoMode(true);
        WindowPlacements::setDemoMode(true);
        WorkspaceModel::setDemoMode(true);
        if (QGuiApplication::platformName() == QLatin1String("offscreen")) {
            QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
        }
        QFont font(QStringLiteral("Noto Sans"));
        font.setPointSizeF(10);
        QGuiApplication::setFont(font);
    }

    QQuickStyle::setStyle(QStringLiteral("Basic"));
    if (QIcon::themeName().isEmpty() || QIcon::themeName() == QLatin1String("hicolor")) {
        // Outside of Plasma (no platform theme) Qt only searches its resources,
        // so add the XDG icon directories to still find application icons.
        QStringList searchPaths = QIcon::themeSearchPaths();
        const QStringList iconDirectories = QStandardPaths::locateAll(QStandardPaths::GenericDataLocation, QStringLiteral("icons"), QStandardPaths::LocateDirectory);
        for (const QString &directory : iconDirectories) {
            if (!searchPaths.contains(directory)) {
                searchPaths.append(directory);
            }
        }
        QIcon::setThemeSearchPaths(searchPaths);
        QIcon::setThemeName(QStringLiteral("breeze"));
        QIcon::setFallbackThemeName(QStringLiteral("hicolor"));
    }
    // Must match the .desktop file: on Wayland it is the app id, which Plasma uses
    // to find the icon of the window in the task manager.
    QGuiApplication::setDesktopFileName(QStringLiteral("kde-infinit-desktop-settings"));
    // No dashes in the icon name, see the install rules in CMakeLists.txt.
    QGuiApplication::setWindowIcon(QIcon::fromTheme(QStringLiteral("org.kde.infinitdesktop"),
                                                    QIcon(QStringLiteral(":/infinitdesktop/icons/kde-infinit-desktop.svg"))));

    ThemeController::instance()->setScreenshotMode(screenshotMode);
    if (screenshotMode) {
        ThemeController::instance()->setForcedAccent(QColor(0x3d, 0xae, 0xe9));
        EffectStatus::instance()->setDemoState(QStringLiteral("active"));
        UpdateChecker::instance()->setDemoMode(true);
    }

    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("idicon"), new IconProvider);
    engine.addImageProvider(QStringLiteral("themeicon"), new ThemeIconProvider);
    setupLocalization(&engine);
    engine.setInitialProperties({{QStringLiteral("initialPage"), parser.value(pageOption)}});
    engine.loadFromModule("InfinitDesktop.Settings", "Main");
    if (engine.rootObjects().isEmpty()) {
        return 1;
    }

    if (screenshotMode) {
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        if (!window) {
            return 1;
        }
        auto *runner = new ScreenshotRunner(window, QDir(parser.value(screenshotOption)).absolutePath(), parseSize(parser.value(screenshotSizeOption)), &app);
        runner->start();
    } else {
        UpdateChecker::instance()->checkOnStartIfEnabled();
    }

    return app.exec();
}
