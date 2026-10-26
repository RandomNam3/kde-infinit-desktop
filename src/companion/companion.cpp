/*
    KDE Infinite Desktop - infinite canvas for KWin
    SPDX-FileCopyrightText: 2026 KDE Infinite Desktop contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/
#include "companion.h"
#include "notestore.h"

#include <KConfigGroup>
#include <KLocalizedString>
#include <KSharedConfig>

#include <QApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusServiceWatcher>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QProcess>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QSystemTrayIcon>
#include <QTimer>

using namespace std::chrono_literals;

namespace InfiniteDesktop
{

namespace
{

const QString ServiceName = QStringLiteral("org.kde.InfiniteDesktop.Companion");
const QString ObjectPath = QStringLiteral("/Companion");
const QString KWinService = QStringLiteral("org.kde.KWin");
const QString EffectPath = QStringLiteral("/InfiniteDesktop");
const QString EffectInterface = QStringLiteral("org.kde.InfiniteDesktop");
const QString ConfigGroup = QStringLiteral("Effect-infinitedesktop");

// Paper colors (light, readable with dark text), in the order of the NoteColor setting.
const QColor PaperColors[] = {
    QColor(0xff, 0xf1, 0x9c), // yellow
    QColor(0xcf, 0xe6, 0xff), // blue
    QColor(0xd4, 0xf2, 0xd0), // green
    QColor(0xff, 0xd8, 0xe6), // pink
    QColor(0xe8, 0xe8, 0xe6), // gray
};

} // namespace

Companion::Companion(QQmlEngine *engine, NoteStore *store, QObject *parent)
    : QObject(parent)
    , m_engine(engine)
    , m_store(store)
{
    loadSettings();
    m_configWatcher = KConfigWatcher::create(KSharedConfig::openConfig(QStringLiteral("kwinrc")));
    connect(m_configWatcher.data(), &KConfigWatcher::configChanged, this, [this](const KConfigGroup &group) {
        if (group.name() != ConfigGroup) {
            return;
        }
        const bool wasEnabled = m_enabled;
        loadSettings();
        if (wasEnabled != m_enabled) {
            if (m_enabled) {
                start();
            } else {
                const QStringList ids = m_windows.keys();
                for (const QString &id : ids) {
                    destroyWindow(id);
                }
            }
            updateTray();
        }
    });

    connect(m_store, &NoteStore::noteRemoved, this, [this](const QString &id) {
        destroyWindow(id);
    });

    QDBusConnection::sessionBus().connect(KWinService, EffectPath, EffectInterface, QStringLiteral("notePinnedChanged"), this,
                                          SLOT(onNotePinnedChangedSlot(QString, bool, bool)));
}

Companion::~Companion()
{
    m_closing = true;
    for (QPointer<QQuickWindow> &window : m_windows) {
        delete window.data();
    }
    m_windows.clear();
}

void Companion::loadSettings()
{
    KSharedConfig::Ptr config = KSharedConfig::openConfig(QStringLiteral("kwinrc"));
    config->reparseConfiguration();
    const KConfigGroup group(config, ConfigGroup);
    m_enabled = group.readEntry("NotesEnabled", true);
    m_defaultColor = std::clamp(group.readEntry("NoteColor", 0), 0, 4);
}

bool Companion::registerService()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.registerService(ServiceName)) {
        return false;
    }
    bus.registerObject(ObjectPath, this, QDBusConnection::ExportScriptableSlots);

    // KWin restarted (or the effect was loaded again): it does not know our windows.
    m_kwinWatcher = new QDBusServiceWatcher(KWinService, bus, QDBusServiceWatcher::WatchForRegistration, this);
    connect(m_kwinWatcher, &QDBusServiceWatcher::serviceRegistered, this, [this]() {
        QTimer::singleShot(1500ms, this, &Companion::reannounceNotes);
    });
    return true;
}

void Companion::start()
{
    updateTray();
    if (!m_enabled) {
        return;
    }
    // The canvas forgets places of notes deleted while it was not running.
    callEffect(QStringLiteral("retainNotes"), {m_store->ids()});
    if (!m_store->notesVisible()) {
        return;
    }
    for (const auto &note : m_store->notes()) {
        if (!note->isHidden()) {
            showWindow(note.get());
        }
    }
    refreshPinStates();
}

void Companion::callEffect(const QString &method, const QVariantList &arguments)
{
    QDBusMessage message = QDBusMessage::createMethodCall(KWinService, EffectPath, EffectInterface, method);
    message.setArguments(arguments);
    QDBusConnection::sessionBus().call(message, QDBus::NoBlock);
}

void Companion::announce(const QString &id)
{
    // Must reach KWin before the window is mapped, so this one call waits (briefly).
    QDBusMessage message = QDBusMessage::createMethodCall(KWinService, EffectPath, EffectInterface, QStringLiteral("expectNote"));
    message.setArguments({id});
    QDBusConnection::sessionBus().call(message, QDBus::Block, 500);
}

void Companion::showWindow(Note *note)
{
    if (!note || m_windows.value(note->id())) {
        return;
    }
    QQmlComponent component(m_engine);
    component.loadFromModule("InfiniteDesktop.Companion", "NoteWindow");
    QObject *object = component.createWithInitialProperties({
        {QStringLiteral("note"), QVariant::fromValue(note)},
        {QStringLiteral("companion"), QVariant::fromValue(this)},
    });
    auto *window = qobject_cast<QQuickWindow *>(object);
    if (!window) {
        qWarning() << "cannot create a note window:" << component.errorString();
        delete object;
        return;
    }
    QQmlEngine::setObjectOwnership(window, QQmlEngine::CppOwnership);
    const QString id = note->id();
    m_windows.insert(id, window);
    // Closed by the user (Alt+F4, task manager): the note is hidden, not deleted.
    connect(window, &QWindow::visibleChanged, this, [this, id](bool visible) {
        if (visible || m_closing) {
            return;
        }
        if (Note *note = m_store->find(id)) {
            note->setHidden(true);
        }
        QTimer::singleShot(0, this, [this, id]() {
            destroyWindow(id);
        });
    });
    announce(id);
    window->show();
}

void Companion::destroyWindow(const QString &id)
{
    QPointer<QQuickWindow> window = m_windows.take(id);
    if (!window) {
        return;
    }
    m_closing = true;
    window->hide();
    m_closing = false;
    window->deleteLater();
}

void Companion::newNote()
{
    if (!m_enabled) {
        return;
    }
    if (!m_store->notesVisible()) {
        setNotesVisible(true);
    }
    if (Note *note = m_store->create(m_defaultColor)) {
        showWindow(note);
        if (QQuickWindow *window = m_windows.value(note->id())) {
            window->requestActivate();
        }
        updateTray();
    }
}

void Companion::toggleNotes()
{
    setNotesVisible(!m_store->notesVisible() || m_windows.isEmpty());
}

void Companion::setNotesVisible(bool visible)
{
    m_store->setNotesVisible(visible);
    if (visible && m_enabled) {
        bool any = false;
        for (const auto &note : m_store->notes()) {
            // "Show notes" brings back the ones hidden one by one, too.
            note->setHidden(false);
            showWindow(note.get());
            any = true;
        }
        if (any) {
            refreshPinStates();
        }
    } else {
        const QStringList ids = m_windows.keys();
        for (const QString &id : ids) {
            destroyWindow(id);
        }
    }
    updateTray();
}

void Companion::reannounceNotes()
{
    if (!m_enabled || !m_store->notesVisible()) {
        return;
    }
    const QStringList ids = m_windows.keys();
    for (const QString &id : ids) {
        destroyWindow(id);
    }
    // Windows are destroyed later; show the new ones after that.
    QTimer::singleShot(0, this, [this]() {
        for (const auto &note : m_store->notes()) {
            if (!note->isHidden()) {
                showWindow(note.get());
            }
        }
        refreshPinStates();
    });
}

QString Companion::notes() const
{
    QJsonArray array;
    for (const auto &note : m_store->notes()) {
        array.append(QJsonObject{
            {QStringLiteral("id"), note->id()},
            {QStringLiteral("title"), note->title()},
            {QStringLiteral("color"), note->color()},
            {QStringLiteral("hidden"), note->isHidden()},
        });
    }
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

void Companion::quit()
{
    m_store->flush();
    QCoreApplication::quit();
}

void Companion::togglePin(const QString &id, bool keepAbove)
{
    if (Note *note = m_store->find(id)) {
        const bool pin = keepAbove ? !(note->isPinned() && note->keepAbove()) : !note->isPinned();
        callEffect(QStringLiteral("setNotePinned"), {id, pin, keepAbove && pin});
    }
}

void Companion::hideNote(const QString &id)
{
    if (Note *note = m_store->find(id)) {
        note->setHidden(true);
        destroyWindow(id);
        updateTray();
    }
}

void Companion::deleteNote(const QString &id)
{
    m_store->remove(id);
    callEffect(QStringLiteral("forgetNote"), {id});
}

void Companion::showMap()
{
    callEffect(QStringLiteral("showMap"), {});
}

void Companion::openSettings()
{
    QProcess::startDetached(QStringLiteral("kde-infinite-desktop-settings"), {QStringLiteral("--page"), QStringLiteral("organize")});
}

QColor Companion::paperColor(int color) const
{
    return PaperColors[std::clamp(color, 0, 4)];
}

QString Companion::colorName(int color) const
{
    switch (color) {
    case 1:
        return i18nc("@item note color", "Blue");
    case 2:
        return i18nc("@item note color", "Green");
    case 3:
        return i18nc("@item note color", "Pink");
    case 4:
        return i18nc("@item note color", "Gray");
    default:
        return i18nc("@item note color", "Yellow");
    }
}

void Companion::onNotePinnedChangedSlot(const QString &id, bool pinned, bool keepAbove)
{
    if (Note *note = m_store->find(id)) {
        note->setPinned(pinned, keepAbove);
    }
}

void Companion::noteStateReceived(const QString &json)
{
    const QJsonObject state = QJsonDocument::fromJson(json.toUtf8()).object();
    if (Note *note = m_store->find(state.value(QStringLiteral("id")).toString())) {
        note->setPinned(state.value(QStringLiteral("pinned")).toBool(), state.value(QStringLiteral("keepAbove")).toBool());
    }
}

void Companion::refreshPinStates()
{
    // The canvas owns the pin state; ask it once for the shown notes.
    for (auto it = m_windows.cbegin(); it != m_windows.cend(); ++it) {
        const QString id = it.key();
        QDBusMessage message = QDBusMessage::createMethodCall(KWinService, EffectPath, EffectInterface, QStringLiteral("noteState"));
        message.setArguments({id});
        QDBusConnection::sessionBus().callWithCallback(message, this, SLOT(noteStateReceived(QString)));
    }
}

void Companion::updateTray()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        return;
    }
    if (!m_tray) {
        m_tray = std::make_unique<QSystemTrayIcon>(QIcon::fromTheme(QStringLiteral("knotes"), QIcon::fromTheme(QStringLiteral("org.kde.infinitedesktop"))));
        m_tray->setToolTip(i18nc("@info:tooltip tray icon", "Infinite Desktop notes"));
        m_menu = std::make_unique<QMenu>();
        m_tray->setContextMenu(m_menu.get());
        connect(m_tray.get(), &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
            if (reason == QSystemTrayIcon::Trigger) {
                toggleNotes();
            }
        });
    }
    m_menu->clear();
    QAction *add = m_menu->addAction(QIcon::fromTheme(QStringLiteral("list-add")), i18nc("@action tray menu", "New Note"), this, &Companion::newNote);
    add->setEnabled(m_enabled);
    const bool shown = m_store->notesVisible() && !m_windows.isEmpty();
    QAction *toggle = m_menu->addAction(QIcon::fromTheme(shown ? QStringLiteral("view-hidden") : QStringLiteral("view-visible")),
                                        shown ? i18nc("@action tray menu", "Hide Notes") : i18nc("@action tray menu", "Show Notes"), this,
                                        &Companion::toggleNotes);
    toggle->setEnabled(m_enabled && !m_store->notes().empty());
    m_menu->addSeparator();
    m_menu->addAction(QIcon::fromTheme(QStringLiteral("map-flat")), i18nc("@action tray menu", "Show Map"), this, &Companion::showMap);
    m_menu->addAction(QIcon::fromTheme(QStringLiteral("window-duplicate")), i18nc("@action tray menu", "New Group from Selection…"), this, [this]() {
        callEffect(QStringLiteral("newGroupFromSelection"), {});
    });
    m_menu->addAction(QIcon::fromTheme(QStringLiteral("draw-rectangle")), i18nc("@action tray menu", "New Area from Selection…"), this, [this]() {
        callEffect(QStringLiteral("newAreaFromSelection"), {});
    });
    m_menu->addSeparator();
    m_menu->addAction(QIcon::fromTheme(QStringLiteral("configure")), i18nc("@action tray menu", "Settings…"), this, &Companion::openSettings);
    m_menu->addAction(QIcon::fromTheme(QStringLiteral("application-exit")), i18nc("@action tray menu", "Quit"), this, &Companion::quit);
    m_tray->show();
}

} // namespace InfiniteDesktop
