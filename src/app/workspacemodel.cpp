/*
    KDE Infinit Desktop - infinite canvas for KWin
    SPDX-FileCopyrightText: 2026 KDE Infinit Desktop contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/
#include "workspacemodel.h"

#include "applicationindex.h"

#include <KConfigGroup>
#include <KLocalizedString>
#include <KSharedConfig>

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QJSEngine>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QUuid>

#include <algorithm>

namespace InfinitDesktop
{

namespace
{

WorkspaceModel *s_workspaceInstance = nullptr;
bool s_demoMode = false;

const QString s_service = QStringLiteral("org.kde.KWin");
const QString s_path = QStringLiteral("/InfinitDesktop");
const QString s_interface = QStringLiteral("org.kde.InfinitDesktop");
const QString s_companionService = QStringLiteral("org.kde.InfinitDesktop.Companion");

constexpr int GroupShortcutSlots = 9;

QString stringReply(const QDBusMessage &reply)
{
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        return QString();
    }
    return reply.arguments().constFirst().toString();
}

bool newGroupsAreAutomatic()
{
    const KConfigGroup group(KSharedConfig::openConfig(QStringLiteral("kwinrc")), QStringLiteral("Effect-infinitdesktop"));
    const QString value = group.readEntry("GroupLayoutMode", QString());
    return value.compare(QLatin1String("Automatic"), Qt::CaseInsensitive) == 0 || value == QLatin1String("1");
}

} // namespace

WorkspaceModel::WorkspaceModel(QObject *parent)
    : QObject(parent)
{
    if (!s_demoMode) {
        QDBusConnection bus = QDBusConnection::sessionBus();
        bus.connect(s_service, s_path, s_interface, QStringLiteral("groupsChanged"), this, SLOT(remoteChanged()));
        bus.connect(s_service, s_path, s_interface, QStringLiteral("areasChanged"), this, SLOT(remoteChanged()));
    }
}

WorkspaceModel *WorkspaceModel::instance()
{
    if (!s_workspaceInstance) {
        s_workspaceInstance = new WorkspaceModel(QCoreApplication::instance());
    }
    return s_workspaceInstance;
}

WorkspaceModel *WorkspaceModel::create(QQmlEngine *qmlEngine, QJSEngine *jsEngine)
{
    Q_UNUSED(qmlEngine)
    Q_UNUSED(jsEngine)
    WorkspaceModel *model = instance();
    QJSEngine::setObjectOwnership(model, QJSEngine::CppOwnership);
    return model;
}

void WorkspaceModel::setDemoMode(bool demo)
{
    s_demoMode = demo;
}

QString WorkspaceModel::applicationKey(const QString &appId)
{
    QString key = appId.trimmed();
    if (key.endsWith(QLatin1String(".desktop"))) {
        key.chop(8);
    }
    return key.toLower();
}

void WorkspaceModel::callEffect(const QString &method, const QVariantList &arguments, std::function<void(const QDBusMessage &)> handler)
{
    QDBusMessage message = QDBusMessage::createMethodCall(s_service, s_path, s_interface, method);
    message.setArguments(arguments);
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, handler](QDBusPendingCallWatcher *call) {
        call->deleteLater();
        const QDBusMessage reply = call->reply();
        if (reply.type() == QDBusMessage::ErrorMessage) {
            if (m_available) {
                m_available = false;
                Q_EMIT changed();
            }
            return;
        }
        if (handler) {
            handler(reply);
        }
    });
}

void WorkspaceModel::remoteChanged()
{
    refresh();
}

void WorkspaceModel::refresh()
{
    if (s_demoMode) {
        parseGroups(QByteArrayLiteral(
            R"({"groups":[{"id":"demo-mail","name":"Mail & Calendar","layout":"manual","openWindows":2,"members":[{"app":"org.kde.kmail2","launch":"org.kde.kmail2"},{"app":"org.kde.merkuro.calendar","launch":"org.kde.merkuro.calendar"},{"app":"firefox","launch":"firefox"}]},)"
            R"({"id":"demo-code","name":"Code","layout":"automatic","openWindows":0,"members":[{"app":"org.kde.kate","launch":"org.kde.kate"},{"app":"org.kde.konsole","launch":"org.kde.konsole"}]}]})"));
        parseAreas(QByteArrayLiteral(R"({"areas":[{"id":"demo-a","name":"Project","dynamic":true,"color":0,"drawn":{"x":0,"y":0,"width":100,"height":100}},{"id":"demo-b","name":"Reading","dynamic":false,"color":2,"drawn":{"x":0,"y":0,"width":100,"height":100}}]})"));
        m_available = true;
        m_noteCount = 3;
        Q_EMIT changed();
        return;
    }
    callEffect(QStringLiteral("groups"), {}, [this](const QDBusMessage &reply) {
        m_available = true;
        parseGroups(stringReply(reply).toUtf8());
        Q_EMIT changed();
    });
    callEffect(QStringLiteral("areas"), {}, [this](const QDBusMessage &reply) {
        parseAreas(stringReply(reply).toUtf8());
        Q_EMIT changed();
    });
    QDBusMessage notes = QDBusMessage::createMethodCall(s_companionService, QStringLiteral("/Companion"), s_companionService, QStringLiteral("notes"));
    // Only ask a running companion; never start it for this.
    notes.setAutoStartService(false);
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(notes), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *call) {
        call->deleteLater();
        const QJsonArray array = QJsonDocument::fromJson(stringReply(call->reply()).toUtf8()).array();
        if (m_noteCount != array.size()) {
            m_noteCount = array.size();
            Q_EMIT changed();
        }
    });
}

void WorkspaceModel::parseGroups(const QByteArray &json)
{
    m_groupsDocument = QJsonDocument::fromJson(json).object();
    rebuildGroups();
}

void WorkspaceModel::parseAreas(const QByteArray &json)
{
    m_areasDocument = QJsonDocument::fromJson(json).object();
    rebuildAreas();
}

void WorkspaceModel::rebuildGroups()
{
    m_groups.clear();
    const QJsonArray groups = m_groupsDocument.value(QStringLiteral("groups")).toArray();
    for (int i = 0; i < groups.size(); ++i) {
        const QJsonObject group = groups[i].toObject();
        QVariantList members;
        const QJsonArray memberArray = group.value(QStringLiteral("members")).toArray();
        for (const QJsonValue &value : memberArray) {
            const QJsonObject member = value.toObject();
            const QString launch = member.value(QStringLiteral("launch")).toString();
            const QString appKey = member.value(QStringLiteral("app")).toString();
            QVariantMap info = ApplicationIndex::instance()->describe(launch.isEmpty() ? appKey : launch);
            QVariantMap entry{
                {QStringLiteral("appKey"), appKey},
                {QStringLiteral("launch"), launch},
                {QStringLiteral("name"), info.value(QStringLiteral("known")).toBool() ? info.value(QStringLiteral("name")) : QVariant(appKey)},
                {QStringLiteral("iconName"), info.value(QStringLiteral("iconName"))},
                {QStringLiteral("hasIcon"), info.value(QStringLiteral("hasIcon"))},
                {QStringLiteral("startable"), !launch.isEmpty()},
            };
            members.append(entry);
        }
        m_groups.append(QVariantMap{
            {QStringLiteral("id"), group.value(QStringLiteral("id")).toString()},
            {QStringLiteral("name"), group.value(QStringLiteral("name")).toString()},
            {QStringLiteral("slot"), i < GroupShortcutSlots ? i + 1 : 0},
            {QStringLiteral("automatic"), group.value(QStringLiteral("layout")).toString() == QLatin1String("automatic")},
            {QStringLiteral("openWindows"), group.value(QStringLiteral("openWindows")).toInt()},
            {QStringLiteral("members"), members},
        });
    }
}

void WorkspaceModel::rebuildAreas()
{
    m_areas.clear();
    const QJsonArray areas = m_areasDocument.value(QStringLiteral("areas")).toArray();
    for (const QJsonValue &value : areas) {
        const QJsonObject area = value.toObject();
        m_areas.append(QVariantMap{
            {QStringLiteral("id"), area.value(QStringLiteral("id")).toString()},
            {QStringLiteral("name"), area.value(QStringLiteral("name")).toString()},
            {QStringLiteral("dynamic"), area.value(QStringLiteral("dynamic")).toBool(true)},
            {QStringLiteral("color"), area.value(QStringLiteral("color")).toInt()},
        });
    }
}

void WorkspaceModel::pushGroups()
{
    rebuildGroups();
    Q_EMIT changed();
    if (s_demoMode) {
        return;
    }
    const QString json = QString::fromUtf8(QJsonDocument(m_groupsDocument).toJson(QJsonDocument::Compact));
    callEffect(QStringLiteral("setGroups"), {json}, [this](const QDBusMessage &reply) {
        if (reply.arguments().isEmpty() || !reply.arguments().constFirst().toBool()) {
            Q_EMIT failed(i18n("The groups could not be saved."));
        }
    });
}

void WorkspaceModel::pushAreas()
{
    rebuildAreas();
    Q_EMIT changed();
    if (s_demoMode) {
        return;
    }
    const QString json = QString::fromUtf8(QJsonDocument(m_areasDocument).toJson(QJsonDocument::Compact));
    callEffect(QStringLiteral("setAreas"), {json}, [this](const QDBusMessage &reply) {
        if (reply.arguments().isEmpty() || !reply.arguments().constFirst().toBool()) {
            Q_EMIT failed(i18n("The areas could not be saved."));
        }
    });
}

namespace
{

/// Applies `edit` to the group with `id` in `document`; false if there is none.
template<typename Edit>
bool editGroup(QJsonObject &document, const QString &id, Edit edit)
{
    QJsonArray groups = document.value(QStringLiteral("groups")).toArray();
    for (int i = 0; i < groups.size(); ++i) {
        QJsonObject group = groups[i].toObject();
        if (group.value(QStringLiteral("id")).toString() != id) {
            continue;
        }
        edit(group);
        groups[i] = group;
        document.insert(QStringLiteral("groups"), groups);
        return true;
    }
    return false;
}

template<typename Edit>
bool editArea(QJsonObject &document, const QString &id, Edit edit)
{
    QJsonArray areas = document.value(QStringLiteral("areas")).toArray();
    for (int i = 0; i < areas.size(); ++i) {
        QJsonObject area = areas[i].toObject();
        if (area.value(QStringLiteral("id")).toString() != id) {
            continue;
        }
        edit(area);
        areas[i] = area;
        document.insert(QStringLiteral("areas"), areas);
        return true;
    }
    return false;
}

QJsonArray withoutId(const QJsonArray &array, const QString &id)
{
    QJsonArray result;
    for (const QJsonValue &value : array) {
        if (value.toObject().value(QStringLiteral("id")).toString() != id) {
            result.append(value);
        }
    }
    return result;
}

} // namespace

void WorkspaceModel::addGroup()
{
    QJsonArray groups = m_groupsDocument.value(QStringLiteral("groups")).toArray();
    QSet<QString> names;
    for (const QJsonValue &value : std::as_const(groups)) {
        names.insert(value.toObject().value(QStringLiteral("name")).toString());
    }
    QString name;
    for (int n = 1; n <= groups.size() + 1; ++n) {
        name = i18nc("@item name of a new group, %1 is a number", "Group %1", n);
        if (!names.contains(name)) {
            break;
        }
    }
    groups.append(QJsonObject{
        {QStringLiteral("id"), QUuid::createUuid().toString(QUuid::WithoutBraces)},
        {QStringLiteral("name"), name},
        {QStringLiteral("members"), QJsonArray()},
        {QStringLiteral("x"), 0},
        {QStringLiteral("y"), 0},
        {QStringLiteral("layout"), newGroupsAreAutomatic() ? QStringLiteral("automatic") : QStringLiteral("manual")},
    });
    m_groupsDocument.insert(QStringLiteral("groups"), groups);
    if (!m_groupsDocument.contains(QStringLiteral("version"))) {
        m_groupsDocument.insert(QStringLiteral("version"), 1);
    }
    pushGroups();
}

void WorkspaceModel::renameGroup(const QString &id, const QString &name)
{
    const QString trimmed = name.trimmed().left(200);
    if (trimmed.isEmpty()) {
        return;
    }
    if (editGroup(m_groupsDocument, id, [&](QJsonObject &group) {
            group.insert(QStringLiteral("name"), trimmed);
        })) {
        pushGroups();
    }
}

void WorkspaceModel::setGroupAutomatic(const QString &id, bool automatic)
{
    if (editGroup(m_groupsDocument, id, [&](QJsonObject &group) {
            group.insert(QStringLiteral("layout"), automatic ? QStringLiteral("automatic") : QStringLiteral("manual"));
        })) {
        pushGroups();
    }
}

void WorkspaceModel::addGroupApplication(const QString &id, const QString &appId)
{
    const QString key = applicationKey(appId);
    if (key.isEmpty()) {
        return;
    }
    QString launch = appId.trimmed();
    if (launch.endsWith(QLatin1String(".desktop"))) {
        launch.chop(8);
    }
    // A window class without a desktop file cannot be started, only brought along.
    if (!ApplicationIndex::instance()->describe(launch).value(QStringLiteral("known")).toBool()) {
        launch.clear();
    }
    if (editGroup(m_groupsDocument, id, [&](QJsonObject &group) {
            QJsonArray members = group.value(QStringLiteral("members")).toArray();
            // Without geometry: the effect arranges it next to the others when the group opens.
            members.append(QJsonObject{
                {QStringLiteral("app"), key},
                {QStringLiteral("launch"), launch},
                {QStringLiteral("geometry"), QJsonObject{{QStringLiteral("x"), 0}, {QStringLiteral("y"), 0}, {QStringLiteral("width"), 0}, {QStringLiteral("height"), 0}}},
            });
            group.insert(QStringLiteral("members"), members);
        })) {
        pushGroups();
    }
}

void WorkspaceModel::removeGroupApplication(const QString &id, int index)
{
    if (editGroup(m_groupsDocument, id, [&](QJsonObject &group) {
            QJsonArray members = group.value(QStringLiteral("members")).toArray();
            if (index >= 0 && index < members.size()) {
                members.removeAt(index);
            }
            group.insert(QStringLiteral("members"), members);
        })) {
        pushGroups();
    }
}

void WorkspaceModel::moveGroup(const QString &id, int delta)
{
    QJsonArray groups = m_groupsDocument.value(QStringLiteral("groups")).toArray();
    for (int i = 0; i < groups.size(); ++i) {
        if (groups[i].toObject().value(QStringLiteral("id")).toString() != id) {
            continue;
        }
        const int target = std::clamp(i + delta, 0, int(groups.size()) - 1);
        if (target == i) {
            return;
        }
        const QJsonValue group = groups.takeAt(i);
        groups.insert(target, group);
        m_groupsDocument.insert(QStringLiteral("groups"), groups);
        pushGroups();
        return;
    }
}

void WorkspaceModel::removeGroup(const QString &id)
{
    m_groupsDocument.insert(QStringLiteral("groups"), withoutId(m_groupsDocument.value(QStringLiteral("groups")).toArray(), id));
    pushGroups();
}

void WorkspaceModel::openGroup(const QString &id)
{
    if (!s_demoMode) {
        callEffect(QStringLiteral("openGroup"), {id});
    }
}

void WorkspaceModel::saveGroupLayout(const QString &id)
{
    if (!s_demoMode) {
        callEffect(QStringLiteral("saveGroupLayout"), {id});
    }
}

void WorkspaceModel::newGroupFromSelection()
{
    if (!s_demoMode) {
        callEffect(QStringLiteral("newGroupFromSelection"), {});
    }
}

void WorkspaceModel::renameArea(const QString &id, const QString &name)
{
    const QString trimmed = name.trimmed().left(200);
    if (trimmed.isEmpty()) {
        return;
    }
    if (editArea(m_areasDocument, id, [&](QJsonObject &area) {
            area.insert(QStringLiteral("name"), trimmed);
        })) {
        pushAreas();
    }
}

void WorkspaceModel::setAreaDynamic(const QString &id, bool dynamic)
{
    if (editArea(m_areasDocument, id, [&](QJsonObject &area) {
            area.insert(QStringLiteral("dynamic"), dynamic);
            if (!dynamic) {
                // A fixed area keeps the size it has now.
                area.insert(QStringLiteral("drawn"), area.value(QStringLiteral("rect")));
            }
        })) {
        pushAreas();
    }
}

void WorkspaceModel::removeArea(const QString &id)
{
    m_areasDocument.insert(QStringLiteral("areas"), withoutId(m_areasDocument.value(QStringLiteral("areas")).toArray(), id));
    pushAreas();
}

void WorkspaceModel::newAreaFromSelection()
{
    if (!s_demoMode) {
        callEffect(QStringLiteral("newAreaFromSelection"), {});
    }
}

void WorkspaceModel::newNote()
{
    if (!s_demoMode) {
        callEffect(QStringLiteral("newNote"), {});
    }
}

void WorkspaceModel::showMap()
{
    if (!s_demoMode) {
        callEffect(QStringLiteral("showMap"), {});
    }
}

} // namespace InfinitDesktop
