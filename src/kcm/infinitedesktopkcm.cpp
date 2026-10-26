/*
    KDE Infinite Desktop - infinite canvas for KWin
    SPDX-FileCopyrightText: 2026 KDE Infinite Desktop contributors
    SPDX-License-Identifier: GPL-3.0-or-later
*/
#include "infinitedesktopkcm.h"

#include "infinitedesktopconfig.h"
#include "legacymigration.h"
#include "settingsmeta.h"
#include "shortcutdefs.h"
#include "stringlistedit.h"

#include <KActionCollection>
#include <KColorButton>
#include <KConfigSkeleton>
#include <KGlobalAccel>
#include <KLocalizedString>
#include <KMessageWidget>
#include <KPluginFactory>
#include <KSharedConfig>
#include <KShortcutsEditor>

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWhatsThis>

K_PLUGIN_CLASS(InfiniteDesktop::InfiniteDesktopEffectConfig)

namespace InfiniteDesktop
{

namespace
{

const QString s_effectId = QStringLiteral("infinitedesktop");
const QString s_settingsAppName = QStringLiteral("kde-infinite-desktop-settings");

QIcon categoryIcon(const char *iconName)
{
    // The settings app ships its own line icons; System Settings uses the icon theme.
    static const QHash<QString, QStringList> themeNames{
        {QStringLiteral("mouse"), {QStringLiteral("input-mouse"), QStringLiteral("preferences-desktop-mouse")}},
        {QStringLiteral("touchpad"), {QStringLiteral("input-touchpad"), QStringLiteral("preferences-desktop-touchpad")}},
        {QStringLiteral("zoom"), {QStringLiteral("zoom-in"), QStringLiteral("zoom")}},
        {QStringLiteral("windows"), {QStringLiteral("preferences-system-windows"), QStringLiteral("window")}},
        {QStringLiteral("screens"), {QStringLiteral("preferences-desktop-display"), QStringLiteral("video-display")}},
        {QStringLiteral("fullscreen"), {QStringLiteral("view-fullscreen")}},
        {QStringLiteral("appearance"), {QStringLiteral("preferences-desktop-theme-global"), QStringLiteral("preferences-desktop-color")}},
        {QStringLiteral("plasma"), {QStringLiteral("plasma"), QStringLiteral("start-here-kde-plasma"), QStringLiteral("start-here-kde")}},
        {QStringLiteral("keyboard"), {QStringLiteral("preferences-desktop-keyboard-shortcut"), QStringLiteral("input-keyboard")}},
        {QStringLiteral("advanced"), {QStringLiteral("preferences-other"), QStringLiteral("configure")}},
    };
    const QStringList names = themeNames.value(QString::fromLatin1(iconName));
    for (const QString &name : names) {
        if (QIcon::hasThemeIcon(name)) {
            return QIcon::fromTheme(name);
        }
    }
    return QIcon::fromTheme(QStringLiteral("configure"));
}

QString unitSuffix(const char *unit)
{
    if (!unit) {
        return QString();
    }
    const QLatin1StringView u(unit);
    if (u == QLatin1StringView("px")) {
        return i18nc("unit suffix: pixels", " px");
    }
    if (u == QLatin1StringView("px/s")) {
        return i18nc("unit suffix: pixels per second", " px/s");
    }
    if (u == QLatin1StringView("%")) {
        return i18nc("unit suffix: percent", " %");
    }
    if (u == QLatin1StringView("ms")) {
        return i18nc("unit suffix: milliseconds", " ms");
    }
    return QLatin1Char(' ') + QString::fromLatin1(unit);
}

int stepFor(int minimum, int maximum)
{
    const int range = maximum - minimum;
    if (range > 2500) {
        return 50;
    }
    if (range > 500) {
        return 10;
    }
    return 1;
}

QString shortcutGroupTitle(const QString &group)
{
    if (group == QLatin1String("zoom")) {
        return i18nc("@title shortcut group", "Zoom");
    }
    if (group == QLatin1String("pan")) {
        return i18nc("@title shortcut group", "Panning");
    }
    if (group == QLatin1String("focus")) {
        return i18nc("@title shortcut group", "Window Focus");
    }
    if (group == QLatin1String("bookmarks")) {
        return i18nc("@title shortcut group", "Bookmarks");
    }
    if (group == QLatin1String("windows")) {
        return i18nc("@title shortcut group", "Windows");
    }
    return group;
}

QWidget *wrapInScrollArea(QWidget *content, QWidget *parent)
{
    auto *scroll = new QScrollArea(parent);
    scroll->setWidget(content);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->viewport()->setAutoFillBackground(false);
    content->setAutoFillBackground(false);
    return scroll;
}

} // namespace

InfiniteDesktopEffectConfig::InfiniteDesktopEffectConfig(QObject *parent, const KPluginMetaData &data)
    : KCModule(parent, data)
{
    // Up to 0.5.1 the project was called "KDE Infinit Desktop": take over its settings once.
    Legacy::migrateConfig(*KSharedConfig::openConfig(QStringLiteral("kwinrc")));
    InfiniteDesktopConfig::instance(KSharedConfig::openConfig(QStringLiteral("kwinrc")));
    InfiniteDesktopConfig *config = InfiniteDesktopConfig::self();

    // Tell other clients (the settings app) about our changes.
    const auto allItems = config->items();
    for (KConfigSkeletonItem *item : allItems) {
        item->setWriteFlags(KConfigBase::Notify);
    }

    // Sort the kcfg items into their categories. Items without metadata still
    // show up (on the advanced page), so the form can never lose an entry.
    QHash<QString, QList<KConfigSkeletonItem *>> itemsByCategory;
    for (KConfigSkeletonItem *item : allItems) {
        const QByteArray key = item->name().toLatin1();
        const SettingsEntryMeta *meta = findSettingsEntry(key.constData());
        const QString category = meta ? QString::fromLatin1(meta->category) : QStringLiteral("advanced");
        itemsByCategory[category].append(item);
    }

    QWidget *root = widget();
    auto *rootLayout = new QVBoxLayout(root);
    rootLayout->setContentsMargins(0, 0, 0, 0);

    m_message = new KMessageWidget(root);
    m_message->setCloseButtonVisible(true);
    m_message->setWordWrap(true);
    m_message->setVisible(false);
    rootLayout->addWidget(m_message);

    auto *body = new QHBoxLayout;
    rootLayout->addLayout(body, 1);

    m_categoryList = new QListWidget(root);
    m_categoryList->setIconSize(QSize(22, 22));
    m_categoryList->setSpacing(1);
    m_categoryList->setUniformItemSizes(true);
    m_categoryList->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    m_categoryList->setAccessibleName(i18nc("@label accessible name of the category list", "Categories"));
    body->addWidget(m_categoryList);

    m_pages = new QStackedWidget(root);
    body->addWidget(m_pages, 1);

    for (const SettingsCategory &category : settingsCategories()) {
        const QString id = QString::fromLatin1(category.id);
        QWidget *page = nullptr;
        if (id == QLatin1String(ShortcutsCategoryId)) {
            page = createShortcutsPage(category);
        } else {
            const QList<KConfigSkeletonItem *> items = itemsByCategory.value(id);
            if (items.isEmpty()) {
                continue;
            }
            page = createSettingsPage(category, items);
        }
        m_pages->addWidget(page);
        auto *listItem = new QListWidgetItem(categoryIcon(category.icon), category.title.toString(), m_categoryList);
        listItem->setToolTip(category.subtitle.toString());
    }

    const int listWidth = m_categoryList->sizeHintForColumn(0) + 2 * m_categoryList->frameWidth() + 16;
    m_categoryList->setFixedWidth(qMax(listWidth, 160));
    connect(m_categoryList, &QListWidget::currentRowChanged, m_pages, &QStackedWidget::setCurrentIndex);
    m_categoryList->setCurrentRow(0);

    // Footer: hand over to the standalone app.
    auto *footer = new QHBoxLayout;
    auto *footerHint = new QLabel(i18n("Search, live preview and status of the plugin are available in the settings app."), root);
    footerHint->setWordWrap(true);
    footerHint->setEnabled(false);
    footer->addWidget(footerHint, 1);
    m_openAppButton = new QPushButton(QIcon::fromTheme(QStringLiteral("org.kde.infinitedesktop"), QIcon::fromTheme(QStringLiteral("configure"))),
                                      i18nc("@action:button", "Open Infinite Desktop Settings…"),
                                      root);
    footer->addWidget(m_openAppButton);
    rootLayout->addLayout(footer);

    m_settingsAppPath = QStandardPaths::findExecutable(s_settingsAppName);
    if (m_settingsAppPath.isEmpty() && QFileInfo::exists(QStringLiteral(INFINITEDESKTOP_SETTINGS_APP_PATH))) {
        m_settingsAppPath = QStringLiteral(INFINITEDESKTOP_SETTINGS_APP_PATH);
    }
    if (m_settingsAppPath.isEmpty()) {
        m_openAppButton->setEnabled(false);
        m_openAppButton->setToolTip(i18n("The settings app “%1” is not installed.", s_settingsAppName));
    }
    connect(m_openAppButton, &QPushButton::clicked, this, &InfiniteDesktopEffectConfig::openSettingsApp);

    root->setMinimumSize(root->fontMetrics().averageCharWidth() * 110, root->fontMetrics().lineSpacing() * 28);

    addConfig(config, root);
    updateDependencies();
    updateShortcutState();
}

InfiniteDesktopEffectConfig::~InfiniteDesktopEffectConfig() = default;

QWidget *InfiniteDesktopEffectConfig::createPageHeader(const SettingsCategory &category, QWidget *parent)
{
    auto *header = new QWidget(parent);
    auto *layout = new QVBoxLayout(header);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    auto *title = new QLabel(category.title.toString(), header);
    QFont titleFont = title->font();
    titleFont.setPointSizeF(titleFont.pointSizeF() * 1.25);
    titleFont.setWeight(QFont::DemiBold);
    title->setFont(titleFont);
    title->setAccessibleName(category.title.toString());
    layout->addWidget(title);

    auto *subtitle = new QLabel(category.subtitle.toString(), header);
    subtitle->setWordWrap(true);
    subtitle->setEnabled(false);
    layout->addWidget(subtitle);
    return header;
}

QWidget *InfiniteDesktopEffectConfig::createInfoButton(KConfigSkeletonItem *item, QWidget *parent)
{
    const QString text = item->whatsThis();
    auto *button = new QToolButton(parent);
    button->setIcon(QIcon::fromTheme(QStringLiteral("help-contextual"), QIcon::fromTheme(QStringLiteral("help-about"))));
    button->setAutoRaise(true);
    button->setToolTip(text);
    button->setAccessibleName(i18nc("@action:button %1 is the name of a setting", "More information about “%1”", item->label()));
    button->setFocusPolicy(Qt::TabFocus);
    connect(button, &QToolButton::clicked, button, [button, text] {
        QWhatsThis::showText(button->mapToGlobal(QPoint(button->width() / 2, button->height())), text, button);
    });
    return button;
}

QWidget *InfiniteDesktopEffectConfig::createEditor(KConfigSkeletonItem *item, const SettingsEntryMeta *meta, QWidget *parent)
{
    const QString objectName = QStringLiteral("kcfg_") + item->name();
    QWidget *editor = nullptr;

    if (auto *enumItem = dynamic_cast<KCoreConfigSkeleton::ItemEnum *>(item)) {
        auto *combo = new QComboBox(parent);
        const auto choices = enumItem->choices();
        for (const auto &choice : choices) {
            combo->addItem(choice.label.isEmpty() ? choice.name : choice.label);
        }
        // Enum entries store the index of the choice.
        combo->setProperty("kcfg_property", QByteArray("currentIndex"));
        combo->setProperty("kcfg_propertyNotify", QByteArray(SIGNAL(currentIndexChanged(int))));
        connect(combo, &QComboBox::currentIndexChanged, this, &InfiniteDesktopEffectConfig::updateDependencies);
        editor = combo;
    } else if (auto *intItem = dynamic_cast<KCoreConfigSkeleton::ItemInt *>(item)) {
        auto *spin = new QSpinBox(parent);
        const int minimum = intItem->minValue().isValid() ? intItem->minValue().toInt() : 0;
        const int maximum = intItem->maxValue().isValid() ? intItem->maxValue().toInt() : 100000;
        spin->setRange(minimum, maximum);
        spin->setSingleStep(stepFor(minimum, maximum));
        spin->setSuffix(unitSuffix(meta ? meta->unit : nullptr));
        spin->setGroupSeparatorShown(maximum >= 10000);
        editor = spin;
    } else if (dynamic_cast<KCoreConfigSkeleton::ItemBool *>(item)) {
        auto *check = new QCheckBox(item->label(), parent);
        connect(check, &QCheckBox::toggled, this, &InfiniteDesktopEffectConfig::updateDependencies);
        editor = check;
    } else if (dynamic_cast<KConfigSkeleton::ItemColor *>(item)) {
        auto *button = new KColorButton(parent);
        button->setAccessibleName(item->label());
        editor = button;
    } else if (item->property().typeId() == QMetaType::QStringList) {
        auto *list = new StringListEdit(parent);
        list->setPlaceholderText(i18n("One entry per line"));
        editor = list;
    } else {
        return nullptr;
    }

    editor->setObjectName(objectName);
    editor->setToolTip(item->whatsThis());
    editor->setWhatsThis(item->whatsThis());
    if (editor->accessibleName().isEmpty()) {
        editor->setAccessibleName(item->label());
    }
    editor->setAccessibleDescription(item->whatsThis());
    m_editors.insert(item->name(), editor);
    return editor;
}

QWidget *InfiniteDesktopEffectConfig::createSettingsPage(const SettingsCategory &category, const QList<KConfigSkeletonItem *> &items)
{
    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->addWidget(createPageHeader(category, content));
    layout->addSpacing(content->fontMetrics().lineSpacing() / 2);

    auto *form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    layout->addLayout(form);
    layout->addStretch(1);

    for (KConfigSkeletonItem *item : items) {
        const QByteArray key = item->name().toLatin1();
        const SettingsEntryMeta *meta = findSettingsEntry(key.constData());

        auto *field = new QWidget(content);
        auto *fieldLayout = new QHBoxLayout(field);
        fieldLayout->setContentsMargins(0, 0, 0, 0);
        QWidget *editor = createEditor(item, meta, field);
        if (!editor) {
            delete field;
            continue;
        }
        const bool isList = qobject_cast<StringListEdit *>(editor);
        fieldLayout->addWidget(editor, isList ? 1 : 0, isList ? Qt::Alignment() : Qt::AlignVCenter);
        fieldLayout->addWidget(createInfoButton(item, field), 0, isList ? Qt::AlignTop : Qt::AlignVCenter);
        if (!isList) {
            fieldLayout->addStretch(1);
        }
        field->setSizePolicy(QSizePolicy::Expanding, isList ? QSizePolicy::Expanding : QSizePolicy::Preferred);

        QLabel *label = nullptr;
        if (qobject_cast<QCheckBox *>(editor)) {
            form->addRow(QString(), field);
        } else {
            label = new QLabel(i18nc("@label:textbox form label, %1 is the name of a setting", "%1:", item->label()), content);
            label->setBuddy(editor);
            label->setWordWrap(true);
            label->setMaximumWidth(content->fontMetrics().averageCharWidth() * 40);
            form->addRow(label, field);
        }

        if (meta && meta->dependsOn) {
            DependentRow row;
            row.dependsOn = QString::fromLatin1(meta->dependsOn);
            row.widgets.append(field);
            if (label) {
                row.widgets.append(label);
            }
            m_dependentRows.append(row);
        }
    }

    return wrapInScrollArea(content, m_pages);
}

QWidget *InfiniteDesktopEffectConfig::createShortcutsPage(const SettingsCategory &category)
{
    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->addWidget(createPageHeader(category, content));

    m_shortcutsEditor = new KShortcutsEditor(content, KShortcutsEditor::GlobalAction);
    layout->addWidget(m_shortcutsEditor, 1);

    const QString component = QString::fromLatin1(ShortcutComponent);
    const QString componentDisplayName = QString::fromLatin1(ShortcutComponentDisplayName);

    QStringList groups;
    for (const ShortcutDefinition &definition : shortcutDefinitions()) {
        const QString group = QString::fromLatin1(definition.group);
        if (!groups.contains(group)) {
            groups.append(group);
        }
    }

    for (const QString &group : std::as_const(groups)) {
        // One collection per group, all of them in the "kwin" component like the effect's own actions.
        auto *collection = new KActionCollection(this, component);
        collection->setComponentDisplayName(componentDisplayName);
        for (const ShortcutDefinition &definition : shortcutDefinitions()) {
            if (group != QLatin1String(definition.group)) {
                continue;
            }
            QAction *action = collection->addAction(QString::fromLatin1(definition.id));
            action->setText(definition.text.toString());
            action->setWhatsThis(definition.description.toString());
            action->setToolTip(definition.description.toString());
            // The real action lives inside KWin; this one only edits its shortcut.
            action->setProperty("isConfigurationAction", true);
            KGlobalAccel::self()->setGlobalShortcut(action, definition.defaultShortcuts);
            m_shortcutActions.append(action);
        }
        m_shortcutsEditor->addCollection(collection, shortcutGroupTitle(group));
        m_collections.append(collection);
    }

    connect(m_shortcutsEditor, &KShortcutsEditor::keyChange, this, &InfiniteDesktopEffectConfig::updateShortcutState);

    // Conflicts with other applications are best resolved in the global shortcut settings.
    auto *allShortcutsRow = new QHBoxLayout;
    auto *allShortcutsHint = new QLabel(i18n("To see and resolve conflicts with other applications, open the shortcut settings of the whole system."), content);
    allShortcutsHint->setWordWrap(true);
    allShortcutsHint->setEnabled(false);
    allShortcutsRow->addWidget(allShortcutsHint, 1);
    auto *allShortcutsButton = new QPushButton(QIcon::fromTheme(QStringLiteral("preferences-desktop-keyboard-shortcut")),
                                               i18nc("@action:button", "All KDE Shortcuts…"),
                                               content);
    connect(allShortcutsButton, &QPushButton::clicked, this, [this] {
        bool started = false;
        if (!QStandardPaths::findExecutable(QStringLiteral("kcmshell6")).isEmpty()) {
            started = QProcess::startDetached(QStringLiteral("kcmshell6"), {QStringLiteral("kcm_keys")});
        }
        if (!started && !QStandardPaths::findExecutable(QStringLiteral("systemsettings")).isEmpty()) {
            started = QProcess::startDetached(QStringLiteral("systemsettings"), {QStringLiteral("kcm_keys")});
        }
        if (!started) {
            m_message->setMessageType(KMessageWidget::Error);
            m_message->setText(i18n("The shortcut settings could not be opened."));
            m_message->animatedShow();
        }
    });
    allShortcutsRow->addWidget(allShortcutsButton);
    layout->addLayout(allShortcutsRow);
    return content;
}

void InfiniteDesktopEffectConfig::updateDependencies()
{
    for (const DependentRow &row : std::as_const(m_dependentRows)) {
        QWidget *controller = m_editors.value(row.dependsOn);
        bool enabled = true;
        if (auto *check = qobject_cast<QCheckBox *>(controller)) {
            enabled = check->isChecked();
        } else if (auto *combo = qobject_cast<QComboBox *>(controller)) {
            enabled = combo->currentIndex() > 0;
        }
        for (QWidget *widget : row.widgets) {
            widget->setEnabled(enabled);
        }
    }
}

void InfiniteDesktopEffectConfig::updateShortcutState()
{
    if (!m_shortcutsEditor) {
        return;
    }
    bool allDefault = true;
    for (QAction *action : std::as_const(m_shortcutActions)) {
        if (KGlobalAccel::self()->shortcut(action) != KGlobalAccel::self()->defaultShortcut(action)) {
            allDefault = false;
            break;
        }
    }
    unmanagedWidgetChangeState(m_shortcutsEditor->isModified());
    unmanagedWidgetDefaultState(allDefault);
}

void InfiniteDesktopEffectConfig::load()
{
    KCModule::load();
    if (m_shortcutsEditor && m_shortcutsEditor->isModified()) {
        m_shortcutsEditor->undo();
    }
    updateDependencies();
    updateShortcutState();
}

void InfiniteDesktopEffectConfig::save()
{
    KCModule::save();
    for (KActionCollection *collection : std::as_const(m_collections)) {
        collection->writeSettings();
    }
    if (m_shortcutsEditor) {
        m_shortcutsEditor->save();
    }
    updateShortcutState();

    // No dependency on KWin's development files: call org.kde.kwin.Effects directly.
    QDBusMessage message = QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"),
                                                          QStringLiteral("/Effects"),
                                                          QStringLiteral("org.kde.kwin.Effects"),
                                                          QStringLiteral("reconfigureEffect"));
    message << s_effectId;
    QDBusConnection::sessionBus().send(message);
}

void InfiniteDesktopEffectConfig::defaults()
{
    KCModule::defaults();
    if (m_shortcutsEditor) {
        m_shortcutsEditor->allDefault();
    }
    updateDependencies();
    updateShortcutState();
}

void InfiniteDesktopEffectConfig::openSettingsApp()
{
    if (m_settingsAppPath.isEmpty() || !QProcess::startDetached(m_settingsAppPath, {})) {
        m_message->setMessageType(KMessageWidget::Error);
        m_message->setText(i18n("The settings app “%1” could not be started.", s_settingsAppName));
        m_message->animatedShow();
    }
}

} // namespace InfiniteDesktop

#include "infinitedesktopkcm.moc"
