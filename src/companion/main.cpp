/*
    KDE Infinite Desktop - infinite canvas for KWin
    SPDX-FileCopyrightText: 2026 KDE Infinite Desktop contributors
    SPDX-License-Identifier: GPL-3.0-or-later

    Companion app: notes on the canvas and the notes icon in the system tray.
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

#include "companion.h"
#include "legacymigration.h"
#include "notestore.h"

#include <KAboutData>
#include <KSharedConfig>
#include <KLocalizedString>

#include <QApplication>
#include <QCommandLineParser>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QIcon>
#include <QPalette>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickStyle>

using namespace InfiniteDesktop;

int main(int argc, char *argv[])
{
    // All note windows render one after the other on this thread: no render thread and no
    // extra GPU context per note, which matters more than throughput for small, mostly
    // static windows.
    if (qEnvironmentVariableIsEmpty("QSG_RENDER_LOOP")) {
        qputenv("QSG_RENDER_LOOP", "basic");
    }
    QApplication app(argc, argv);
    QApplication::setQuitOnLastWindowClosed(false);
    KLocalizedString::setApplicationDomain(QByteArrayLiteral(TRANSLATION_DOMAIN));

    KAboutData about(QStringLiteral("kde-infinite-desktop-companion"),
                     i18nc("@title application name", "Infinite Desktop Notes"),
                     QStringLiteral(INFINITEDESKTOP_VERSION_STRING),
                     i18n("Notes on the infinite canvas"),
                     KAboutLicense::GPL_V3,
                     i18n("© 2026 KDE Infinite Desktop contributors"));
    // The Wayland app id: the canvas recognises note windows by it.
    about.setDesktopFileName(QStringLiteral("org.kde.infinitedesktop.companion"));
    KAboutData::setApplicationData(about);
    QGuiApplication::setDesktopFileName(QStringLiteral("org.kde.infinitedesktop.companion"));
    QApplication::setWindowIcon(QIcon::fromTheme(QStringLiteral("knotes"), QIcon::fromTheme(QStringLiteral("org.kde.infinitedesktop"))));

    QCommandLineParser parser;
    about.setupCommandLine(&parser);
    const QCommandLineOption newNoteOption(QStringLiteral("new-note"), i18n("Create a new note (also when the app is already running)."));
    parser.addOption(newNoteOption);
    parser.process(app);
    about.processCommandLine(&parser);

    // Up to 0.5.1 the project was called "KDE Infinit Desktop": take over its notes and
    // settings once (the effect does the same; whoever runs first does it).
    Legacy::migrateUserFiles();
    const Legacy::ConfigResult migration = Legacy::migrateConfig(*KSharedConfig::openConfig(QStringLiteral("kwinrc")));

    NoteStore store;
    store.load();
    // Links in the formatted text lie on light paper with every color scheme; the Markdown
    // reader takes their color from the application palette.
    QPalette palette = QApplication::palette();
    palette.setColor(QPalette::Link, QColor(0x1d, 0x64, 0xb8));
    palette.setColor(QPalette::LinkVisited, QColor(0x1d, 0x64, 0xb8));
    QApplication::setPalette(palette);
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QQmlEngine engine;
#if KI18N_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    KLocalization::setupLocalizedContext(&engine);
#else
    auto *context = new KLocalizedContext(&engine);
    context->setTranslationDomain(QStringLiteral(TRANSLATION_DOMAIN));
    engine.rootContext()->setContextObject(context);
#endif

    Companion companion(&engine, &store);
    if (!companion.registerService()) {
        // Already running: hand the request over and leave.
        if (parser.isSet(newNoteOption)) {
            const QDBusMessage message = QDBusMessage::createMethodCall(QStringLiteral("org.kde.InfiniteDesktop.Companion"), QStringLiteral("/Companion"),
                                                                        QStringLiteral("org.kde.InfiniteDesktop.Companion"), QStringLiteral("newNote"));
            QDBusConnection::sessionBus().call(message, QDBus::Block, 2000);
        }
        return 0;
    }
    // An old companion of the previous name may still run in this session: one notes icon
    // is enough. Never start it for that (no D-Bus activation).
    QDBusMessage quitLegacy = QDBusMessage::createMethodCall(QStringLiteral("org.kde.InfinitDesktop.Companion"), QStringLiteral("/Companion"),
                                                             QStringLiteral("org.kde.InfinitDesktop.Companion"), QStringLiteral("quit"));
    quitLegacy.setAutoStartService(false);
    QDBusConnection::sessionBus().send(quitLegacy);
    if (migration.changed() && migration.enabled) {
        // The effect was enabled under the old name only: KWin did not load it at login.
        QDBusMessage load = QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"), QStringLiteral("/Effects"), QStringLiteral("org.kde.kwin.Effects"),
                                                           QStringLiteral("loadEffect"));
        load << QStringLiteral("infinitedesktop");
        QDBusConnection::sessionBus().send(load);
    }
    companion.start();
    if (parser.isSet(newNoteOption)) {
        companion.newNote();
    }
    const int result = app.exec();
    store.flush();
    return result;
}
