/*
    KDE Infinit Desktop - infinite canvas for KWin
    SPDX-FileCopyrightText: 2026 KDE Infinit Desktop contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/
#include "globalshortcutsmodel.h"

#include "shortcutdefs.h"
#include "shortcutmodel.h"

#include <KGlobalShortcutInfo>
#include <KLocalizedString>

#include <QCollator>
#include <QCoreApplication>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QHash>
#include <QJSEngine>
#include <QProcess>
#include <QStandardPaths>

#include <algorithm>

namespace InfinitDesktop
{

namespace
{

GlobalShortcutsModel *s_globalInstance = nullptr;
bool s_demoMode = false;

const QString s_service = QStringLiteral("org.kde.kglobalaccel");
const QString s_path = QStringLiteral("/kglobalaccel");
const QString s_interface = QStringLiteral("org.kde.KGlobalAccel");
const QString s_componentInterface = QStringLiteral("org.kde.kglobalaccel.Component");
const QString s_ownComponent = QString::fromLatin1(ShortcutComponent);

void registerDBusTypes()
{
    static bool registered = false;
    if (!registered) {
        qDBusRegisterMetaType<QKeySequence>();
        qDBusRegisterMetaType<QList<QKeySequence>>();
        qDBusRegisterMetaType<KGlobalShortcutInfo>();
        qDBusRegisterMetaType<QList<KGlobalShortcutInfo>>();
        registered = true;
    }
}

QList<QKeySequence> cleaned(const QList<QKeySequence> &list)
{
    QList<QKeySequence> result;
    for (const QKeySequence &sequence : list) {
        if (!sequence.isEmpty() && !result.contains(sequence)) {
            result.append(sequence);
        }
    }
    return result;
}

bool isOwnAction(const QString &componentId, const QString &actionId)
{
    if (componentId != s_ownComponent) {
        return false;
    }
    for (const ShortcutDefinition &definition : shortcutDefinitions()) {
        if (actionId == QLatin1String(definition.id)) {
            return true;
        }
    }
    return false;
}

QString displayName(const QString &actionName, bool own)
{
    // Our actions are called "Infinit Desktop: …" in KWin's list; the badge says that already.
    if (own) {
        const int colon = actionName.indexOf(QLatin1String(": "));
        if (colon > 0 && colon <= 24) {
            return actionName.mid(colon + 2);
        }
    }
    return actionName;
}

QStringList nativeTexts(const QList<QKeySequence> &list)
{
    QStringList result;
    for (const QKeySequence &sequence : list) {
        result.append(sequence.toString(QKeySequence::NativeText));
    }
    return result;
}

} // namespace

GlobalShortcutsModel::GlobalShortcutsModel(QObject *parent)
    : QAbstractListModel(parent)
{
    registerDBusTypes();
}

GlobalShortcutsModel *GlobalShortcutsModel::instance()
{
    if (!s_globalInstance) {
        s_globalInstance = new GlobalShortcutsModel(QCoreApplication::instance());
    }
    return s_globalInstance;
}

GlobalShortcutsModel *GlobalShortcutsModel::create(QQmlEngine *qmlEngine, QJSEngine *jsEngine)
{
    Q_UNUSED(qmlEngine)
    Q_UNUSED(jsEngine)
    GlobalShortcutsModel *model = instance();
    QJSEngine::setObjectOwnership(model, QJSEngine::CppOwnership);
    return model;
}

void GlobalShortcutsModel::setDemoMode(bool demo)
{
    s_demoMode = demo;
}

QString GlobalShortcutsModel::keyOf(const QString &componentId, const QString &actionId)
{
    return componentId + QLatin1Char('\n') + actionId;
}

int GlobalShortcutsModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_entries.size();
}

QHash<int, QByteArray> GlobalShortcutsModel::roleNames() const
{
    return {
        {KeyRole, "shortcutKey"},
        {ComponentIdRole, "componentId"},
        {ComponentNameRole, "componentName"},
        {ActionIdRole, "actionId"},
        {ActionNameRole, "actionName"},
        {SequencesRole, "sequences"},
        {SequenceTextsRole, "sequenceTexts"},
        {KeysRole, "keys"},
        {DefaultTextRole, "defaultText"},
        {IsDefaultRole, "isDefault"},
        {HasConflictRole, "hasConflict"},
        {ConflictTextRole, "conflictText"},
        {IsOwnRole, "isOwn"},
        {SearchTextRole, "searchText"},
    };
}

QVariant GlobalShortcutsModel::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid)) {
        return QVariant();
    }
    const Entry &entry = m_entries.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case ActionNameRole:
        return entry.actionName.isEmpty() ? entry.actionId : displayName(entry.actionName, entry.own);
    case KeyRole:
        return keyOf(entry.componentId, entry.actionId);
    case ComponentIdRole:
        return entry.componentId;
    case ComponentNameRole:
        return entry.componentName.isEmpty() ? entry.componentId : entry.componentName;
    case ActionIdRole:
        return entry.actionId;
    case SequencesRole: {
        QStringList result;
        for (const QKeySequence &sequence : entry.keys) {
            result.append(sequence.toString(QKeySequence::PortableText));
        }
        return result;
    }
    case SequenceTextsRole:
        return nativeTexts(entry.keys);
    case KeysRole: {
        QVariantList result;
        for (const QKeySequence &sequence : entry.keys) {
            result.append(ShortcutModel::keyNames(sequence));
        }
        return result;
    }
    case DefaultTextRole: {
        const QStringList texts = nativeTexts(entry.defaultKeys);
        return texts.isEmpty() ? i18nc("@label no keyboard shortcut assigned", "None") : texts.join(i18nc("@label separator between alternative shortcuts", ", "));
    }
    case IsDefaultRole:
        return entry.keys == entry.defaultKeys;
    case HasConflictRole:
        return entry.conflict;
    case ConflictTextRole:
        return entry.conflictText;
    case IsOwnRole:
        return entry.own;
    case SearchTextRole:
        return entry.searchText;
    }
    return QVariant();
}

bool GlobalShortcutsModel::available() const
{
    return m_available;
}

bool GlobalShortcutsModel::loading() const
{
    return m_loading;
}

int GlobalShortcutsModel::conflictCount() const
{
    return m_conflictCount;
}

int GlobalShortcutsModel::componentCount() const
{
    return m_componentCount;
}

int GlobalShortcutsModel::count() const
{
    return m_entries.size();
}

int GlobalShortcutsModel::rowOf(const QString &key) const
{
    for (int row = 0; row < m_entries.size(); ++row) {
        if (keyOf(m_entries.at(row).componentId, m_entries.at(row).actionId) == key) {
            return row;
        }
    }
    return -1;
}

void GlobalShortcutsModel::updateSearchText(Entry &entry)
{
    entry.searchText = SearchUtil::normalize(QStringList{entry.actionName, entry.componentName, entry.actionId, nativeTexts(entry.keys).join(QLatin1Char(' '))}
                                                 .join(QLatin1Char(' ')));
}

void GlobalShortcutsModel::reload()
{
    if (s_demoMode) {
        loadDemoData();
        return;
    }
    if (m_loading) {
        return;
    }
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        if (m_available) {
            m_available = false;
            Q_EMIT availableChanged();
        }
        return;
    }

    m_loading = true;
    Q_EMIT loadingChanged();
    m_pending.clear();

    const QDBusMessage message = QDBusMessage::createMethodCall(s_service, s_path, s_interface, QStringLiteral("allComponents"));
    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *call) {
        call->deleteLater();
        const QDBusMessage reply = call->reply();
        const bool ok = reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty();
        if (ok != m_available) {
            m_available = ok;
            Q_EMIT availableChanged();
        }
        if (!ok) {
            finishLoading();
            return;
        }
        const QList<QDBusObjectPath> paths = qdbus_cast<QList<QDBusObjectPath>>(reply.arguments().constFirst());
        if (paths.isEmpty()) {
            finishLoading();
            return;
        }
        m_pendingCalls = paths.size();
        for (const QDBusObjectPath &path : paths) {
            const QDBusMessage infoCall = QDBusMessage::createMethodCall(s_service, path.path(), s_componentInterface, QStringLiteral("allShortcutInfos"));
            auto *infoWatcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(infoCall), this);
            connect(infoWatcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *infoReply) {
                infoReply->deleteLater();
                const QDBusMessage result = infoReply->reply();
                if (result.type() == QDBusMessage::ReplyMessage && !result.arguments().isEmpty()) {
                    const QList<KGlobalShortcutInfo> infos = qdbus_cast<QList<KGlobalShortcutInfo>>(result.arguments().constFirst());
                    for (const KGlobalShortcutInfo &info : infos) {
                        Entry entry;
                        entry.componentId = info.componentUniqueName();
                        entry.componentName = info.componentFriendlyName();
                        entry.actionId = info.uniqueName();
                        entry.actionName = info.friendlyName();
                        entry.keys = cleaned(info.keys());
                        entry.defaultKeys = cleaned(info.defaultKeys());
                        entry.own = isOwnAction(entry.componentId, entry.actionId);
                        updateSearchText(entry);
                        m_pending.append(entry);
                    }
                }
                if (--m_pendingCalls == 0) {
                    finishLoading();
                }
            });
        }
    });
}

void GlobalShortcutsModel::finishLoading()
{
    QList<Entry> entries = m_pending;
    m_pending.clear();
    setEntries(entries);
    if (m_loading) {
        m_loading = false;
        Q_EMIT loadingChanged();
    }
}

void GlobalShortcutsModel::setEntries(QList<Entry> entries)
{
    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    collator.setNumericMode(true);
    std::sort(entries.begin(), entries.end(), [&collator](const Entry &a, const Entry &b) {
        const QString componentA = a.componentName.isEmpty() ? a.componentId : a.componentName;
        const QString componentB = b.componentName.isEmpty() ? b.componentId : b.componentName;
        if (componentA != componentB) {
            const int order = collator.compare(componentA, componentB);
            return order != 0 ? order < 0 : a.componentId < b.componentId;
        }
        return collator.compare(a.actionName, b.actionName) < 0;
    });

    bool sameRows = entries.size() == m_entries.size();
    for (int row = 0; sameRows && row < entries.size(); ++row) {
        sameRows = entries.at(row).componentId == m_entries.at(row).componentId && entries.at(row).actionId == m_entries.at(row).actionId;
    }
    if (sameRows) {
        // Update in place so that views keep their scroll position.
        for (int row = 0; row < entries.size(); ++row) {
            const Entry &fresh = entries.at(row);
            Entry &current = m_entries[row];
            if (fresh.keys != current.keys || fresh.defaultKeys != current.defaultKeys || fresh.actionName != current.actionName) {
                current = fresh;
                Q_EMIT dataChanged(index(row), index(row));
            }
        }
    } else {
        beginResetModel();
        m_entries = entries;
        endResetModel();
    }

    QSet<QString> components;
    for (const Entry &entry : std::as_const(m_entries)) {
        components.insert(entry.componentId);
    }
    m_componentCount = components.size();
    Q_EMIT countsChanged();
    updateConflicts();
}

void GlobalShortcutsModel::updateConflicts()
{
    QHash<QKeySequence, QList<int>> users;
    for (int row = 0; row < m_entries.size(); ++row) {
        for (const QKeySequence &sequence : std::as_const(m_entries.at(row).keys)) {
            users[sequence].append(row);
        }
    }

    int conflicts = 0;
    for (int row = 0; row < m_entries.size(); ++row) {
        Entry &entry = m_entries[row];
        QStringList others;
        for (const QKeySequence &sequence : std::as_const(entry.keys)) {
            const QList<int> rows = users.value(sequence);
            for (int other : rows) {
                if (other == row) {
                    continue;
                }
                const Entry &otherEntry = m_entries.at(other);
                others.append(i18nc("@info %1 is a key combination, %2 an action, %3 an application", "%1 is also used by “%2” (%3)",
                                    sequence.toString(QKeySequence::NativeText),
                                    otherEntry.actionName.isEmpty() ? otherEntry.actionId : otherEntry.actionName,
                                    otherEntry.componentName.isEmpty() ? otherEntry.componentId : otherEntry.componentName));
            }
        }
        const bool conflict = !others.isEmpty();
        const QString text = others.join(QLatin1Char('\n'));
        if (conflict) {
            ++conflicts;
        }
        if (conflict != entry.conflict || text != entry.conflictText) {
            entry.conflict = conflict;
            entry.conflictText = text;
            Q_EMIT dataChanged(index(row), index(row), {HasConflictRole, ConflictTextRole});
        }
    }
    if (conflicts != m_conflictCount) {
        m_conflictCount = conflicts;
        Q_EMIT conflictsChanged();
    }
}

QVariantList GlobalShortcutsModel::conflicts(const QString &key, const QString &sequenceText) const
{
    QVariantList result;
    const QKeySequence sequence = QKeySequence::fromString(sequenceText, QKeySequence::PortableText);
    if (sequence.isEmpty()) {
        return result;
    }
    for (const Entry &entry : m_entries) {
        if (keyOf(entry.componentId, entry.actionId) == key || !entry.keys.contains(sequence)) {
            continue;
        }
        result.append(QVariantMap{
            {QStringLiteral("action"), entry.actionName.isEmpty() ? entry.actionId : entry.actionName},
            {QStringLiteral("component"), entry.componentName.isEmpty() ? entry.componentId : entry.componentName},
            {QStringLiteral("key"), keyOf(entry.componentId, entry.actionId)},
        });
    }
    return result;
}

bool GlobalShortcutsModel::setKeys(int row, const QList<QKeySequence> &requested)
{
    Entry &entry = m_entries[row];
    const QList<QKeySequence> keys = cleaned(requested);

    if (entry.own) {
        // Our own actions go through the configuration actions of ShortcutModel,
        // which also keeps its page up to date (and calls updateOwnAction()).
        ShortcutModel::instance()->applyShortcuts(entry.actionId, keys);
        return true;
    }

    if (!s_demoMode && m_available) {
        const QStringList actionId{entry.componentId, entry.actionId, entry.componentName, entry.actionName};
        QDBusMessage message = QDBusMessage::createMethodCall(s_service, s_path, s_interface, QStringLiteral("setForeignShortcutKeys"));
        message << actionId << QVariant::fromValue(keys);
        const QDBusMessage reply = QDBusConnection::sessionBus().call(message, QDBus::Block, 3000);
        if (reply.type() == QDBusMessage::ErrorMessage) {
            Q_EMIT changeFailed(reply.errorMessage());
            return false;
        }
        // KGlobalAccel silently drops keys that are taken; read back the real state.
        QDBusMessage readBack = QDBusMessage::createMethodCall(s_service, s_path, s_interface, QStringLiteral("shortcutKeys"));
        readBack << actionId;
        const QDBusMessage keysReply = QDBusConnection::sessionBus().call(readBack, QDBus::Block, 3000);
        if (keysReply.type() == QDBusMessage::ReplyMessage && !keysReply.arguments().isEmpty()) {
            entry.keys = cleaned(qdbus_cast<QList<QKeySequence>>(keysReply.arguments().constFirst()));
        } else {
            entry.keys = keys;
        }
    } else {
        entry.keys = keys;
    }
    updateSearchText(entry);
    Q_EMIT dataChanged(index(row), index(row));
    updateConflicts();
    return true;
}

bool GlobalShortcutsModel::assign(const QString &key, int slot, const QString &sequenceText, bool steal)
{
    const int row = rowOf(key);
    const QKeySequence sequence = QKeySequence::fromString(sequenceText, QKeySequence::PortableText);
    if (row < 0 || sequence.isEmpty() || slot < 0) {
        return false;
    }
    if (steal) {
        for (int other = 0; other < m_entries.size(); ++other) {
            if (other != row && m_entries.at(other).keys.contains(sequence)) {
                QList<QKeySequence> keys = m_entries.at(other).keys;
                keys.removeAll(sequence);
                setKeys(other, keys);
            }
        }
    }
    QList<QKeySequence> keys = m_entries.at(row).keys;
    keys.removeAll(sequence);
    if (slot < keys.size()) {
        keys[slot] = sequence;
    } else {
        keys.append(sequence);
    }
    setKeys(row, keys);
    return m_entries.at(row).keys.contains(sequence);
}

void GlobalShortcutsModel::clear(const QString &key, int slot)
{
    const int row = rowOf(key);
    if (row < 0 || slot < 0 || slot >= m_entries.at(row).keys.size()) {
        return;
    }
    QList<QKeySequence> keys = m_entries.at(row).keys;
    keys.removeAt(slot);
    setKeys(row, keys);
}

void GlobalShortcutsModel::resetToDefault(const QString &key)
{
    const int row = rowOf(key);
    if (row >= 0) {
        setKeys(row, m_entries.at(row).defaultKeys);
    }
}

void GlobalShortcutsModel::updateOwnAction(const QString &actionId, const QList<QKeySequence> &keys)
{
    const int row = rowOf(keyOf(s_ownComponent, actionId));
    if (row < 0) {
        return;
    }
    Entry &entry = m_entries[row];
    const QList<QKeySequence> fresh = cleaned(keys);
    if (entry.keys == fresh) {
        return;
    }
    entry.keys = fresh;
    updateSearchText(entry);
    Q_EMIT dataChanged(index(row), index(row));
    updateConflicts();
}

bool GlobalShortcutsModel::openSystemSettings() const
{
    if (!QStandardPaths::findExecutable(QStringLiteral("systemsettings")).isEmpty()) {
        return QProcess::startDetached(QStringLiteral("systemsettings"), {QStringLiteral("kcm_keys")});
    }
    if (!QStandardPaths::findExecutable(QStringLiteral("kcmshell6")).isEmpty()) {
        return QProcess::startDetached(QStringLiteral("kcmshell6"), {QStringLiteral("kcm_keys")});
    }
    return false;
}

void GlobalShortcutsModel::loadDemoData()
{
    // Example data for --screenshot (no KGlobalAccel in the build containers).
    auto add = [](QList<Entry> &list, const QString &componentId, const QString &componentName, const QString &actionId, const QString &actionName, const QList<QKeySequence> &keys, const QList<QKeySequence> &defaults) {
        Entry entry;
        entry.componentId = componentId;
        entry.componentName = componentName;
        entry.actionId = actionId;
        entry.actionName = actionName;
        entry.keys = keys;
        entry.defaultKeys = defaults;
        entry.own = isOwnAction(componentId, actionId);
        list.append(entry);
    };
    auto same = [&add](QList<Entry> &list, const QString &componentId, const QString &componentName, const QString &actionId, const QString &actionName, const QList<QKeySequence> &keys) {
        add(list, componentId, componentName, actionId, actionName, keys, keys);
    };

    QList<Entry> list;
    const QString kwin = QStringLiteral("KWin");
    for (const ShortcutDefinition &definition : shortcutDefinitions()) {
        same(list, s_ownComponent, kwin, QString::fromLatin1(definition.id), definition.text.toString(), cleaned(definition.defaultShortcuts));
    }
    same(list, s_ownComponent, kwin, QStringLiteral("Overview"), QStringLiteral("Toggle Overview"), {QKeySequence(Qt::META | Qt::Key_W)});
    same(list, s_ownComponent, kwin, QStringLiteral("Grid View"), QStringLiteral("Toggle Grid View"), {QKeySequence(Qt::META | Qt::Key_G)});
    same(list, s_ownComponent, kwin, QStringLiteral("Walk Through Windows"), QStringLiteral("Walk Through Windows"), {QKeySequence(Qt::ALT | Qt::Key_Tab)});
    same(list, s_ownComponent, kwin, QStringLiteral("Window Close"), QStringLiteral("Close Window"), {QKeySequence(Qt::ALT | Qt::Key_F4)});
    same(list, s_ownComponent, kwin, QStringLiteral("Window Maximize"), QStringLiteral("Maximize Window"), {QKeySequence(Qt::META | Qt::Key_PageUp)});
    same(list, s_ownComponent, kwin, QStringLiteral("Show Desktop"), QStringLiteral("Peek at Desktop"), {QKeySequence(Qt::META | Qt::Key_D)});
    same(list, s_ownComponent, kwin, QStringLiteral("Switch to Next Desktop"), QStringLiteral("Switch to Next Desktop"), {QKeySequence(Qt::META | Qt::CTRL | Qt::Key_Right)});
    same(list, s_ownComponent, kwin, QStringLiteral("view_zoom_in"), QStringLiteral("Zoom In"), {QKeySequence(Qt::META | Qt::Key_Plus), QKeySequence(Qt::META | Qt::Key_Equal)});
    same(list, s_ownComponent, kwin, QStringLiteral("view_zoom_out"), QStringLiteral("Zoom Out"), {QKeySequence(Qt::META | Qt::Key_Minus)});
    const QString plasma = QStringLiteral("plasmashell");
    const QString plasmaName = QStringLiteral("Plasma Workspace");
    same(list, plasma, plasmaName, QStringLiteral("activate application launcher"), QStringLiteral("Activate Application Launcher"), {QKeySequence(Qt::ALT | Qt::Key_F1)});
    same(list, plasma, plasmaName, QStringLiteral("show-on-mouse-pos"), QStringLiteral("Show Clipboard Items at Mouse Position"), {QKeySequence(Qt::META | Qt::Key_V)});
    same(list, plasma, plasmaName, QStringLiteral("toggle do not disturb"), QStringLiteral("Toggle Do Not Disturb"), {});
    same(list, plasma, plasmaName, QStringLiteral("activate task manager entry 1"), QStringLiteral("Activate Task Manager Entry 1"), {QKeySequence(Qt::META | Qt::Key_1)});
    const QString krunner = QStringLiteral("services/org.kde.krunner.desktop");
    same(list, krunner, QStringLiteral("KRunner"), QStringLiteral("_launch"), QStringLiteral("KRunner"), {QKeySequence(Qt::ALT | Qt::Key_Space), QKeySequence(Qt::ALT | Qt::Key_F2)});
    const QString spectacle = QStringLiteral("services/org.kde.spectacle.desktop");
    same(list, spectacle, QStringLiteral("Spectacle"), QStringLiteral("_launch"), QStringLiteral("Launch Spectacle"), {QKeySequence(Qt::Key_Print)});
    same(list, spectacle, QStringLiteral("Spectacle"), QStringLiteral("RectangularRegionScreenShot"), QStringLiteral("Capture Rectangular Region"), {QKeySequence(Qt::META | Qt::SHIFT | Qt::Key_S)});
    same(list, spectacle, QStringLiteral("Spectacle"), QStringLiteral("FullScreenScreenShot"), QStringLiteral("Capture Entire Desktop"), {QKeySequence(Qt::SHIFT | Qt::Key_Print)});
    // A user-changed shortcut that clashes with "Pin Window to Screen".
    add(list, QStringLiteral("services/org.kde.konsole.desktop"), QStringLiteral("Konsole"), QStringLiteral("_launch"), QStringLiteral("Konsole"),
        {QKeySequence(Qt::META | Qt::ALT | Qt::Key_T)}, {QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_T)});
    same(list, QStringLiteral("ksmserver"), QStringLiteral("Session Management"), QStringLiteral("Lock Session"), QStringLiteral("Lock Session"), {QKeySequence(Qt::META | Qt::Key_L)});
    same(list, QStringLiteral("ksmserver"), QStringLiteral("Session Management"), QStringLiteral("Log Out"), QStringLiteral("Show Logout Screen"), {QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Delete)});
    same(list, QStringLiteral("kaccess"), QStringLiteral("Accessibility"), QStringLiteral("Toggle Screen Reader On and Off"), QStringLiteral("Toggle Screen Reader On and Off"), {QKeySequence(Qt::META | Qt::ALT | Qt::Key_S)});
    for (Entry &entry : list) {
        updateSearchText(entry);
    }
    if (!m_available) {
        m_available = true;
        Q_EMIT availableChanged();
    }
    setEntries(list);
}

// ---------------------------------------------------------------------------

GlobalShortcutsFilterModel::GlobalShortcutsFilterModel(QObject *parent)
    : FilterProxyModel(parent)
{
    setSourceModel(GlobalShortcutsModel::instance());
    connect(this, &QAbstractItemModel::rowsInserted, this, &GlobalShortcutsFilterModel::countChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &GlobalShortcutsFilterModel::countChanged);
    connect(this, &QAbstractItemModel::modelReset, this, &GlobalShortcutsFilterModel::countChanged);
    connect(this, &QAbstractItemModel::layoutChanged, this, &GlobalShortcutsFilterModel::countChanged);
    // Conflicts change the filter result of "only conflicts".
    connect(GlobalShortcutsModel::instance(), &GlobalShortcutsModel::conflictsChanged, this, [this] {
        if (m_onlyConflicts) {
            beginFilterUpdate();
            endFilterUpdate();
        }
    });
    // First/last flags of the neighbours change when rows appear or disappear.
    auto refreshEdges = [this] {
        if (rowCount() > 0) {
            Q_EMIT dataChanged(index(0, 0), index(rowCount() - 1, 0), {FirstInComponentRole, LastInComponentRole});
        }
    };
    connect(this, &QAbstractItemModel::rowsInserted, this, refreshEdges);
    connect(this, &QAbstractItemModel::rowsRemoved, this, refreshEdges);
}

QString GlobalShortcutsFilterModel::query() const
{
    return m_query;
}

void GlobalShortcutsFilterModel::setQuery(const QString &query)
{
    if (m_query == query) {
        return;
    }
    beginFilterUpdate();
    m_query = query;
    m_terms = SearchUtil::terms(query);
    endFilterUpdate();
    if (rowCount() > 0) {
        Q_EMIT dataChanged(index(0, 0), index(rowCount() - 1, 0), {HighlightedActionNameRole, FirstInComponentRole, LastInComponentRole});
    }
    Q_EMIT queryChanged();
}

bool GlobalShortcutsFilterModel::onlyConflicts() const
{
    return m_onlyConflicts;
}

void GlobalShortcutsFilterModel::setOnlyConflicts(bool onlyConflicts)
{
    if (m_onlyConflicts == onlyConflicts) {
        return;
    }
    beginFilterUpdate();
    m_onlyConflicts = onlyConflicts;
    endFilterUpdate();
    Q_EMIT onlyConflictsChanged();
}

bool GlobalShortcutsFilterModel::excludeOwn() const
{
    return m_excludeOwn;
}

void GlobalShortcutsFilterModel::setExcludeOwn(bool excludeOwn)
{
    if (m_excludeOwn == excludeOwn) {
        return;
    }
    beginFilterUpdate();
    m_excludeOwn = excludeOwn;
    endFilterUpdate();
    Q_EMIT excludeOwnChanged();
}

QColor GlobalShortcutsFilterModel::highlightColor() const
{
    return m_highlightColor;
}

void GlobalShortcutsFilterModel::setHighlightColor(const QColor &color)
{
    if (m_highlightColor == color) {
        return;
    }
    m_highlightColor = color;
    if (rowCount() > 0) {
        Q_EMIT dataChanged(index(0, 0), index(rowCount() - 1, 0), {HighlightedActionNameRole});
    }
    Q_EMIT highlightColorChanged();
}

int GlobalShortcutsFilterModel::count() const
{
    return rowCount();
}

QHash<int, QByteArray> GlobalShortcutsFilterModel::roleNames() const
{
    QHash<int, QByteArray> roles = QSortFilterProxyModel::roleNames();
    roles.insert(HighlightedActionNameRole, "highlightedActionName");
    roles.insert(FirstInComponentRole, "firstInComponent");
    roles.insert(LastInComponentRole, "lastInComponent");
    return roles;
}

QVariant GlobalShortcutsFilterModel::data(const QModelIndex &index, int role) const
{
    if (role == HighlightedActionNameRole) {
        return SearchUtil::highlight(QSortFilterProxyModel::data(index, GlobalShortcutsModel::ActionNameRole).toString(), m_terms, m_highlightColor);
    }
    if (role == FirstInComponentRole || role == LastInComponentRole) {
        const int neighbour = index.row() + (role == FirstInComponentRole ? -1 : 1);
        if (neighbour < 0 || neighbour >= rowCount()) {
            return true;
        }
        return this->index(neighbour, 0).data(GlobalShortcutsModel::ComponentIdRole) != index.data(GlobalShortcutsModel::ComponentIdRole);
    }
    return QSortFilterProxyModel::data(index, role);
}

bool GlobalShortcutsFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    const QModelIndex index = sourceModel()->index(sourceRow, 0, sourceParent);
    if (m_onlyConflicts && !index.data(GlobalShortcutsModel::HasConflictRole).toBool()) {
        return false;
    }
    if (m_excludeOwn && index.data(GlobalShortcutsModel::IsOwnRole).toBool()) {
        return false;
    }
    if (m_terms.isEmpty()) {
        return true;
    }
    return SearchUtil::matches(index.data(GlobalShortcutsModel::SearchTextRole).toString(), m_terms);
}

} // namespace InfinitDesktop
