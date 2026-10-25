/*
    KDE Infinit Desktop - infinite canvas for KWin
    SPDX-FileCopyrightText: 2026 KDE Infinit Desktop contributors
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
#include "notestore.h"

#include <KAboutData>
#include <KLocalizedString>

#include <QApplication>
#include <QCommandLineParser>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QIcon>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickStyle>

using namespace InfinitDesktop;

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

    KAboutData about(QStringLiteral("kde-infinit-desktop-companion"),
                     i18nc("@title application name", "Infinit Desktop Notes"),
                     QStringLiteral(INFINITDESKTOP_VERSION_STRING),
                     i18n("Notes on the infinite canvas"),
                     KAboutLicense::GPL_V3,
                     i18n("© 2026 KDE Infinit Desktop contributors"));
    // The Wayland app id: the canvas recognises note windows by it.
    about.setDesktopFileName(QStringLiteral("org.kde.infinitdesktop.companion"));
    KAboutData::setApplicationData(about);
    QGuiApplication::setDesktopFileName(QStringLiteral("org.kde.infinitdesktop.companion"));
    QApplication::setWindowIcon(QIcon::fromTheme(QStringLiteral("knotes"), QIcon::fromTheme(QStringLiteral("org.kde.infinitdesktop"))));

    QCommandLineParser parser;
    about.setupCommandLine(&parser);
    const QCommandLineOption newNoteOption(QStringLiteral("new-note"), i18n("Create a new note (also when the app is already running)."));
    parser.addOption(newNoteOption);
    parser.process(app);
    about.processCommandLine(&parser);

    NoteStore store;
    store.load();
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
            const QDBusMessage message = QDBusMessage::createMethodCall(QStringLiteral("org.kde.InfinitDesktop.Companion"), QStringLiteral("/Companion"),
                                                                        QStringLiteral("org.kde.InfinitDesktop.Companion"), QStringLiteral("newNote"));
            QDBusConnection::sessionBus().call(message, QDBus::Block, 2000);
        }
        return 0;
    }
    companion.start();
    if (parser.isSet(newNoteOption)) {
        companion.newNote();
    }
    const int result = app.exec();
    store.flush();
    return result;
}
