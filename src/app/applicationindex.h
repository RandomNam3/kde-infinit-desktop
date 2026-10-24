/*
    KDE Infinit Desktop - infinite canvas for KWin
    SPDX-FileCopyrightText: 2026 KDE Infinit Desktop contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/
#pragma once

#include "searchutil.h"

#include <QAbstractListModel>
#include <QColor>
#include <QtQml/qqmlregistration.h>

class QJSEngine;
class QQmlEngine;

namespace InfinitDesktop
{

/**
 * The installed applications (desktop files), used to show names and icons
 * for window classes / desktop file names and to pick applications for the
 * exclusion lists. Loaded on first use.
 */
class ApplicationIndex : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Applications)
    QML_SINGLETON
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        AppIdRole = Qt::UserRole + 1,
        NameRole,
        GenericNameRole,
        IconNameRole,
        HasIconRole,
        WindowClassRole,
        NoDisplayRole,
        SearchTextRole,
        LastRole = SearchTextRole,
    };

    explicit ApplicationIndex(QObject *parent);

    static ApplicationIndex *instance();
    static ApplicationIndex *create(QQmlEngine *qmlEngine, QJSEngine *jsEngine);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const;

    /**
     * Name and icon for a window class or desktop file name:
     * {id, name, iconName, hasIcon, known}. Unknown ids keep the id as name.
     */
    Q_INVOKABLE QVariantMap describe(const QString &id);
    /// Makes sure the desktop files have been read.
    Q_INVOKABLE void load();

Q_SIGNALS:
    void countChanged();

private:
    struct Entry {
        QString id;
        QString name;
        QString genericName;
        QString iconName;
        QString windowClass;
        bool noDisplay = false;
        bool hasIcon = false;
        QString searchText;
    };

    int find(const QString &id) const;

    QList<Entry> m_entries;
    bool m_loaded = false;
};

/// Filters the application index for the picker.
class ApplicationFilterModel : public FilterProxyModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(ApplicationFilter)
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged)
    Q_PROPERTY(bool showHidden READ showHidden WRITE setShowHidden NOTIFY showHiddenChanged)
    Q_PROPERTY(QColor highlightColor READ highlightColor WRITE setHighlightColor NOTIFY highlightColorChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        HighlightedNameRole = ApplicationIndex::LastRole + 1,
    };

    explicit ApplicationFilterModel(QObject *parent = nullptr);

    QString query() const;
    void setQuery(const QString &query);
    bool showHidden() const;
    void setShowHidden(bool showHidden);
    QColor highlightColor() const;
    void setHighlightColor(const QColor &color);
    int count() const;

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

Q_SIGNALS:
    void queryChanged();
    void showHiddenChanged();
    void highlightColorChanged();
    void countChanged();

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    QString m_query;
    QStringList m_terms;
    bool m_showHidden = false;
    QColor m_highlightColor = QColor(0x3d, 0xae, 0xe9);
};

} // namespace InfinitDesktop
