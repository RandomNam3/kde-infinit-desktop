/*
    KDE Infinite Desktop - infinite canvas for KWin
    SPDX-FileCopyrightText: 2026 KDE Infinite Desktop contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/
#pragma once

#include <KConfigWatcher>

#include <QAbstractListModel>
#include <QColor>
#include "searchutil.h"

#include <QSortFilterProxyModel>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

class KConfigSkeletonItem;
class QJSEngine;
class QQmlEngine;

namespace InfiniteDesktop
{

struct SettingsEntryMeta;

/**
 * All entries of infinitedesktop.kcfg as a list model.
 *
 * Changes are applied live: the item is updated immediately, the skeleton is
 * saved (with KConfig::Notify) after a short debounce and the effect is asked
 * to reload its configuration via org.kde.kwin.Effects.reconfigureEffect.
 */
class SettingsModel : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(SettingsStore)
    QML_SINGLETON
    Q_PROPERTY(bool allDefault READ allDefault NOTIFY allDefaultChanged)
    Q_PROPERTY(int count READ rowCount CONSTANT)

public:
    enum Role {
        KeyRole = Qt::UserRole + 1,
        LabelRole,
        InfoRole,
        HintRole,
        CategoryRole,
        CategoryTitleRole,
        CategoryIconRole,
        CategoryIndexRole,
        TypeRole,
        ValueRole,
        DefaultValueRole,
        IsDefaultRole,
        MinRole,
        MaxRole,
        StepRole,
        UnitRole,
        ChoicesRole,
        EnabledRole,
        DependsOnRole,
        DependsOnLabelRole,
        KeywordsRole,
        ValueTextRole,
        DefaultTextRole,
        RangeTextRole,
        SearchTextRole,
        LastRole = SearchTextRole,
    };
    Q_ENUM(Role)

    explicit SettingsModel(QObject *parent);
    ~SettingsModel() override;

    static SettingsModel *instance();
    static SettingsModel *create(QQmlEngine *qmlEngine, QJSEngine *jsEngine);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool allDefault() const;

    Q_INVOKABLE QVariant value(const QString &key) const;
    Q_INVOKABLE void setValue(const QString &key, const QVariant &value);
    Q_INVOKABLE void resetToDefault(const QString &key);
    Q_INVOKABLE void resetAll();
    Q_INVOKABLE QString label(const QString &key) const;
    Q_INVOKABLE QString formatValue(const QString &key, const QVariant &value) const;
    Q_INVOKABLE bool isEnabled(const QString &key) const;
    /// Writes pending changes right away (used when the window closes).
    Q_INVOKABLE void flush();

    /// Number of entries in @p category.
    int countInCategory(const QString &category) const;

Q_SIGNALS:
    void allDefaultChanged();
    void valueChanged(const QString &key);
    /// Emitted after a change was written and KWin was asked to reload it.
    void applied(bool reachedKWin);

private:
    struct Entry {
        KConfigSkeletonItem *item = nullptr;
        const SettingsEntryMeta *meta = nullptr;
        QString key;
        QString type;
        QString category;
        int categoryIndex = 0;
        QString hint;
        QString keywords;
        QString searchText;
        QStringList choices;
        int minimum = 0;
        int maximum = 0;
        int step = 1;
        QString unit;
        QString dependsOn;
    };

    int rowOf(const QString &key) const;
    bool isEnabled(int row) const;
    QVariant normalizedValue(const Entry &entry, const QVariant &value) const;
    QString formatValue(const Entry &entry, const QVariant &value) const;
    void changed(int row);
    void scheduleApply();
    void apply();
    void configChangedExternally(const KConfigGroup &group, const QByteArrayList &names);

    QList<Entry> m_entries;
    QHash<QString, QList<int>> m_dependents;
    QTimer m_applyTimer;
    KConfigWatcher::Ptr m_watcher;
    bool m_lastAllDefault = true;
};

/**
 * Filters SettingsModel by category and/or search query. Adds highlighted
 * versions of label and hint for search results.
 */
class SettingsFilterModel : public FilterProxyModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(SettingsFilter)
    Q_PROPERTY(QString category READ category WRITE setCategory NOTIFY categoryChanged)
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged)
    Q_PROPERTY(QColor highlightColor READ highlightColor WRITE setHighlightColor NOTIFY highlightColorChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        HighlightedLabelRole = SettingsModel::LastRole + 1,
        HighlightedHintRole,
    };

    explicit SettingsFilterModel(QObject *parent = nullptr);

    QString category() const;
    void setCategory(const QString &category);
    QString query() const;
    void setQuery(const QString &query);
    QColor highlightColor() const;
    void setHighlightColor(const QColor &color);
    int count() const;

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

Q_SIGNALS:
    void categoryChanged();
    void queryChanged();
    void highlightColorChanged();
    void countChanged();

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    QString m_category;
    QString m_query;
    QStringList m_terms;
    QColor m_highlightColor = QColor(0x3d, 0xae, 0xe9);
};

/// The categories of settingsmeta.h (without the overview page).
class CategoryModel : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Categories)
    QML_SINGLETON

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        TitleRole,
        SubtitleRole,
        IconRole,
        IsShortcutsRole,
        EntryCountRole,
    };

    explicit CategoryModel(QObject *parent);

    static CategoryModel *create(QQmlEngine *qmlEngine, QJSEngine *jsEngine);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE QString title(const QString &id) const;
    Q_INVOKABLE QString subtitle(const QString &id) const;
    Q_INVOKABLE QString icon(const QString &id) const;
    Q_INVOKABLE QStringList ids() const;
};

} // namespace InfiniteDesktop
