/*
    KDE Infinit Desktop - infinite canvas for KWin
    SPDX-FileCopyrightText: 2026 KDE Infinit Desktop contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/
#include "applicationindex.h"

#include <KConfigGroup>
#include <KDesktopFile>

#include <QCollator>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QIcon>
#include <QJSEngine>
#include <QSet>
#include <QStandardPaths>

#include <algorithm>

namespace InfinitDesktop
{

namespace
{
ApplicationIndex *s_applicationsInstance = nullptr;

bool iconExists(const QString &name)
{
    if (name.isEmpty()) {
        return false;
    }
    if (QFileInfo(name).isAbsolute()) {
        return QFileInfo::exists(name);
    }
    return QIcon::hasThemeIcon(name);
}
}

ApplicationIndex::ApplicationIndex(QObject *parent)
    : QAbstractListModel(parent)
{
}

ApplicationIndex *ApplicationIndex::instance()
{
    if (!s_applicationsInstance) {
        s_applicationsInstance = new ApplicationIndex(QCoreApplication::instance());
    }
    return s_applicationsInstance;
}

ApplicationIndex *ApplicationIndex::create(QQmlEngine *qmlEngine, QJSEngine *jsEngine)
{
    Q_UNUSED(qmlEngine)
    Q_UNUSED(jsEngine)
    ApplicationIndex *index = instance();
    QJSEngine::setObjectOwnership(index, QJSEngine::CppOwnership);
    return index;
}

void ApplicationIndex::load()
{
    if (m_loaded) {
        return;
    }
    m_loaded = true;

    QList<Entry> entries;
    QSet<QString> seen;
    // Earlier directories (the user's own) take precedence, like in the menu.
    const QStringList directories = QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);
    for (const QString &directory : directories) {
        const QDir base(directory);
        QDirIterator it(directory, {QStringLiteral("*.desktop")}, QDir::Files, QDirIterator::Subdirectories | QDirIterator::FollowSymlinks);
        while (it.hasNext()) {
            const QString path = it.next();
            QString id = base.relativeFilePath(path);
            id.chop(int(QStringLiteral(".desktop").size()));
            id.replace(QLatin1Char('/'), QLatin1Char('-'));
            if (seen.contains(id)) {
                continue;
            }
            seen.insert(id);

            const KDesktopFile file(path);
            if (file.readType() != QLatin1String("Application")) {
                continue;
            }
            const KConfigGroup group = file.desktopGroup();
            if (group.readEntry("Hidden", false)) {
                continue;
            }
            Entry entry;
            entry.id = id;
            entry.name = file.readName();
            if (entry.name.isEmpty()) {
                continue;
            }
            entry.genericName = file.readGenericName();
            entry.iconName = file.readIcon();
            entry.windowClass = group.readEntry("StartupWMClass", QString());
            entry.noDisplay = file.noDisplay();
            entry.hasIcon = iconExists(entry.iconName);
            entry.searchText = SearchUtil::normalize(QStringList{entry.name, entry.genericName, entry.id, entry.windowClass, group.readEntry("Keywords", QString())}
                                                         .join(QLatin1Char(' ')));
            entries.append(entry);
        }
    }

    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    collator.setNumericMode(true);
    std::sort(entries.begin(), entries.end(), [&collator](const Entry &a, const Entry &b) {
        return collator.compare(a.name, b.name) < 0;
    });

    beginResetModel();
    m_entries = entries;
    endResetModel();
    Q_EMIT countChanged();
}

int ApplicationIndex::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_entries.size();
}

int ApplicationIndex::count() const
{
    return m_entries.size();
}

QHash<int, QByteArray> ApplicationIndex::roleNames() const
{
    return {
        {AppIdRole, "appId"},
        {NameRole, "name"},
        {GenericNameRole, "genericName"},
        {IconNameRole, "iconName"},
        {HasIconRole, "hasIcon"},
        {WindowClassRole, "windowClass"},
        {NoDisplayRole, "noDisplay"},
        {SearchTextRole, "searchText"},
    };
}

QVariant ApplicationIndex::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid)) {
        return QVariant();
    }
    const Entry &entry = m_entries.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case NameRole:
        return entry.name;
    case AppIdRole:
        return entry.id;
    case GenericNameRole:
        return entry.genericName;
    case IconNameRole:
        return entry.iconName;
    case HasIconRole:
        return entry.hasIcon;
    case WindowClassRole:
        return entry.windowClass;
    case NoDisplayRole:
        return entry.noDisplay;
    case SearchTextRole:
        return entry.searchText;
    }
    return QVariant();
}

int ApplicationIndex::find(const QString &id) const
{
    if (id.isEmpty()) {
        return -1;
    }
    QString key = id;
    if (key.endsWith(QLatin1String(".desktop"))) {
        key.chop(8);
    }
    for (int row = 0; row < m_entries.size(); ++row) {
        if (m_entries.at(row).id == key) {
            return row;
        }
    }
    for (int row = 0; row < m_entries.size(); ++row) {
        const Entry &entry = m_entries.at(row);
        if (entry.id.compare(key, Qt::CaseInsensitive) == 0 || (!entry.windowClass.isEmpty() && entry.windowClass.compare(key, Qt::CaseInsensitive) == 0)) {
            return row;
        }
    }
    // "krunner" for org.kde.krunner, but only if that is unambiguous.
    int match = -1;
    const QString suffix = QLatin1Char('.') + key;
    for (int row = 0; row < m_entries.size(); ++row) {
        if (m_entries.at(row).id.endsWith(suffix, Qt::CaseInsensitive)) {
            if (match >= 0) {
                return -1;
            }
            match = row;
        }
    }
    return match;
}

QVariantMap ApplicationIndex::describe(const QString &id)
{
    load();
    const int row = find(id);
    if (row >= 0) {
        const Entry &entry = m_entries.at(row);
        return {
            {QStringLiteral("id"), id},
            {QStringLiteral("name"), entry.name},
            {QStringLiteral("iconName"), entry.iconName},
            {QStringLiteral("hasIcon"), entry.hasIcon},
            {QStringLiteral("known"), true},
        };
    }
    return {
        {QStringLiteral("id"), id},
        {QStringLiteral("name"), id},
        {QStringLiteral("iconName"), id},
        {QStringLiteral("hasIcon"), iconExists(id)},
        {QStringLiteral("known"), false},
    };
}

// ---------------------------------------------------------------------------

ApplicationFilterModel::ApplicationFilterModel(QObject *parent)
    : FilterProxyModel(parent)
{
    ApplicationIndex::instance()->load();
    setSourceModel(ApplicationIndex::instance());
    connect(this, &QAbstractItemModel::rowsInserted, this, &ApplicationFilterModel::countChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &ApplicationFilterModel::countChanged);
    connect(this, &QAbstractItemModel::modelReset, this, &ApplicationFilterModel::countChanged);
    connect(this, &QAbstractItemModel::layoutChanged, this, &ApplicationFilterModel::countChanged);
}

QString ApplicationFilterModel::query() const
{
    return m_query;
}

void ApplicationFilterModel::setQuery(const QString &query)
{
    if (m_query == query) {
        return;
    }
    beginFilterUpdate();
    m_query = query;
    m_terms = SearchUtil::terms(query);
    endFilterUpdate();
    if (rowCount() > 0) {
        Q_EMIT dataChanged(index(0, 0), index(rowCount() - 1, 0), {HighlightedNameRole});
    }
    Q_EMIT queryChanged();
}

bool ApplicationFilterModel::showHidden() const
{
    return m_showHidden;
}

void ApplicationFilterModel::setShowHidden(bool showHidden)
{
    if (m_showHidden == showHidden) {
        return;
    }
    beginFilterUpdate();
    m_showHidden = showHidden;
    endFilterUpdate();
    Q_EMIT showHiddenChanged();
}

QColor ApplicationFilterModel::highlightColor() const
{
    return m_highlightColor;
}

void ApplicationFilterModel::setHighlightColor(const QColor &color)
{
    if (m_highlightColor == color) {
        return;
    }
    m_highlightColor = color;
    if (rowCount() > 0) {
        Q_EMIT dataChanged(index(0, 0), index(rowCount() - 1, 0), {HighlightedNameRole});
    }
    Q_EMIT highlightColorChanged();
}

int ApplicationFilterModel::count() const
{
    return rowCount();
}

QHash<int, QByteArray> ApplicationFilterModel::roleNames() const
{
    QHash<int, QByteArray> roles = QSortFilterProxyModel::roleNames();
    roles.insert(HighlightedNameRole, "highlightedName");
    return roles;
}

QVariant ApplicationFilterModel::data(const QModelIndex &index, int role) const
{
    if (role == HighlightedNameRole) {
        return SearchUtil::highlight(QSortFilterProxyModel::data(index, ApplicationIndex::NameRole).toString(), m_terms, m_highlightColor);
    }
    return QSortFilterProxyModel::data(index, role);
}

bool ApplicationFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    const QModelIndex index = sourceModel()->index(sourceRow, 0, sourceParent);
    if (!m_showHidden && m_terms.isEmpty() && index.data(ApplicationIndex::NoDisplayRole).toBool()) {
        return false;
    }
    if (m_terms.isEmpty()) {
        return true;
    }
    return SearchUtil::matches(index.data(ApplicationIndex::SearchTextRole).toString(), m_terms);
}

} // namespace InfinitDesktop
