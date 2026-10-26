/*
    KDE Infinite Desktop - infinite canvas for KWin
    SPDX-FileCopyrightText: 2026 KDE Infinite Desktop contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/
#pragma once

#include <KCModule>

#include <QHash>
#include <QList>
#include <QPointer>

class KActionCollection;
class KConfigSkeletonItem;
class KMessageWidget;
class KShortcutsEditor;
class QAction;
class QListWidget;
class QPushButton;
class QStackedWidget;

namespace InfiniteDesktop
{

struct SettingsCategory;
struct SettingsEntryMeta;

/**
 * System Settings module of the effect.
 *
 * Opened from Desktop Effects -> KDE Infinite Desktop -> Configure. The pages
 * are generated from the kcfg items and the UI metadata in settingsmeta.h:
 * every editor is named kcfg_<EntryName>, so KCModule::addConfig() takes care
 * of loading, saving, defaults and the "changed" state.
 */
class InfiniteDesktopEffectConfig : public KCModule
{
    Q_OBJECT

public:
    InfiniteDesktopEffectConfig(QObject *parent, const KPluginMetaData &data);
    ~InfiniteDesktopEffectConfig() override;

public Q_SLOTS:
    void load() override;
    void save() override;
    void defaults() override;

private:
    QWidget *createSettingsPage(const SettingsCategory &category, const QList<KConfigSkeletonItem *> &items);
    QWidget *createShortcutsPage(const SettingsCategory &category);
    QWidget *createPageHeader(const SettingsCategory &category, QWidget *parent);
    QWidget *createEditor(KConfigSkeletonItem *item, const SettingsEntryMeta *meta, QWidget *parent);
    QWidget *createInfoButton(KConfigSkeletonItem *item, QWidget *parent);
    void updateDependencies();
    void updateShortcutState();
    void openSettingsApp();

    struct DependentRow {
        QString dependsOn;
        QList<QWidget *> widgets;
    };

    QListWidget *m_categoryList = nullptr;
    QStackedWidget *m_pages = nullptr;
    KMessageWidget *m_message = nullptr;
    QPushButton *m_openAppButton = nullptr;
    KShortcutsEditor *m_shortcutsEditor = nullptr;
    QList<KActionCollection *> m_collections;
    QList<QAction *> m_shortcutActions;
    QHash<QString, QWidget *> m_editors;
    QList<DependentRow> m_dependentRows;
    QString m_settingsAppPath;
};

} // namespace InfiniteDesktop
