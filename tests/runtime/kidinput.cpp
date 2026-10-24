// Test helper: drives KWin through the org_kde_kwin_fake_input protocol.
// Usage: kid-input <command> [args] [<command> [args]]...
//        kid-input -      (commands line by line from stdin, "ok" after each line)
//   abs X Y | rel DX DY | down left|right|middle | up BTN | click BTN
//   wheel DELTA | hwheel DELTA | keydown CODE | keyup CODE | key CODE
//   sleep MS | drag X1 Y1 X2 Y2 STEPS MS [BTN]
#include <KWayland/Client/connection_thread.h>
#include <KWayland/Client/fakeinput.h>
#include <KWayland/Client/registry.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QPointF>
#include <QSizeF>
#include <QThread>
#include <QTimer>

#include <cstdio>
#include <unistd.h>

using namespace KWayland::Client;

static Qt::MouseButton button(const QString &name)
{
    if (name == QLatin1String("right")) {
        return Qt::RightButton;
    }
    if (name == QLatin1String("middle")) {
        return Qt::MiddleButton;
    }
    return Qt::LeftButton;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QStringList args = app.arguments().mid(1);

    auto *connection = new ConnectionThread;
    QThread thread;
    connection->moveToThread(&thread);
    thread.start();
    QEventLoop loop;
    QObject::connect(connection, &ConnectionThread::connected, &loop, &QEventLoop::quit, Qt::QueuedConnection);
    QObject::connect(connection, &ConnectionThread::failed, &loop, [&]() {
        std::fprintf(stderr, "kid-input: cannot connect\n");
        loop.exit(1);
    }, Qt::QueuedConnection);
    connection->initConnection();
    if (loop.exec() != 0) {
        return 1;
    }

    Registry registry;
    registry.create(connection);
    FakeInput *input = nullptr;
    QObject::connect(&registry, &Registry::fakeInputAnnounced, &app, [&](quint32 name, quint32 version) {
        input = registry.createFakeInput(name, version, &app);
    });
    registry.setup();
    connection->flush();
    for (int i = 0; i < 200 && !input; ++i) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(5);
    }
    if (!input) {
        std::fprintf(stderr, "kid-input: no fake input interface\n");
        return 2;
    }
    input->authenticate(QStringLiteral("kid-input"), QStringLiteral("automated tests"));
    connection->flush();

    const auto pump = [&](int ms) {
        connection->flush();
        QElapsedTimer timer;
        timer.start();
        do {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            if (ms > 0) {
                QThread::msleep(std::min<qint64>(5, std::max<qint64>(0, ms - timer.elapsed())));
            }
        } while (timer.elapsed() < ms);
    };

    const auto run = [&](const QStringList &args) {
        for (int i = 0; i < args.size(); ++i) {
            const QString command = args[i];
            const auto arg = [&](int offset) {
                return args.value(i + offset).toDouble();
            };
            if (command == QLatin1String("abs")) {
                input->requestPointerMoveAbsolute(QPointF(arg(1), arg(2)));
                i += 2;
            } else if (command == QLatin1String("rel")) {
                input->requestPointerMove(QSizeF(arg(1), arg(2)));
                i += 2;
            } else if (command == QLatin1String("down")) {
                input->requestPointerButtonPress(button(args.value(i + 1)));
                i += 1;
            } else if (command == QLatin1String("up")) {
                input->requestPointerButtonRelease(button(args.value(i + 1)));
                i += 1;
            } else if (command == QLatin1String("click")) {
                input->requestPointerButtonClick(button(args.value(i + 1)));
                i += 1;
            } else if (command == QLatin1String("wheel")) {
                input->requestPointerAxis(Qt::Vertical, arg(1));
                i += 1;
            } else if (command == QLatin1String("hwheel")) {
                input->requestPointerAxis(Qt::Horizontal, arg(1));
                i += 1;
            } else if (command == QLatin1String("keydown")) {
                input->requestKeyboardKeyPress(quint32(arg(1)));
                i += 1;
            } else if (command == QLatin1String("keyup")) {
                input->requestKeyboardKeyRelease(quint32(arg(1)));
                i += 1;
            } else if (command == QLatin1String("key")) {
                input->requestKeyboardKeyPress(quint32(arg(1)));
                pump(20);
                input->requestKeyboardKeyRelease(quint32(arg(1)));
                i += 1;
            } else if (command == QLatin1String("sleep")) {
                pump(int(arg(1)));
                i += 1;
            } else if (command == QLatin1String("drag")) {
                const QPointF from(arg(1), arg(2));
                const QPointF to(arg(3), arg(4));
                const int steps = std::max(1, int(arg(5)));
                const int duration = int(arg(6));
                const Qt::MouseButton b = button(args.value(i + 7));
                input->requestPointerMoveAbsolute(from);
                pump(30);
                input->requestPointerButtonPress(b);
                pump(30);
                for (int s = 1; s <= steps; ++s) {
                    input->requestPointerMoveAbsolute(from + (to - from) * (qreal(s) / steps));
                    pump(duration / steps);
                }
                input->requestPointerButtonRelease(b);
                i += 7;
            } else {
                std::fprintf(stderr, "kid-input: unknown command %s\n", qPrintable(command));
                return false;
            }
            pump(10);
        }
        return true;
    };

    if (args == QStringList{QStringLiteral("-")}) {
        // One command line per line on stdin, answered with "ok": buttons can stay held
        // between lines (recording scripts take screenshots in between).
        char line[4096];
        while (std::fgets(line, sizeof line, stdin)) {
            const QStringList lineArgs = QString::fromUtf8(line).simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
            const bool ok = run(lineArgs);
            pump(10);
            std::printf(ok ? "ok\n" : "error\n");
            std::fflush(stdout);
        }
    } else if (!run(args)) {
        return 3;
    }
    pump(30);
    connection->flush();
    std::fflush(nullptr);
    // Skip tearing down the Wayland objects across threads: this is a short lived test tool.
    _exit(0);
}
