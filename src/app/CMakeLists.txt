# SPDX-FileCopyrightText: 2026 KDE Infinit Desktop contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Standalone settings app "kde-infinit-desktop-settings" with a custom Qt Quick UI
# (plain QtQuick + QtQuick.Controls.Basic, no Kirigami). QML, icons and the app
# icon are compiled into the executable.

find_package(Qt6 ${QT_MIN_VERSION} CONFIG REQUIRED COMPONENTS Network Qml Quick QuickControls2)

# Module resources under :/qt/qml/<URI>.
qt_policy(SET QTP0001 NEW)
if(QT_KNOWN_POLICY_QTP0004)
    qt_policy(SET QTP0004 NEW)
endif()

add_executable(kde-infinit-desktop-settings)

target_sources(kde-infinit-desktop-settings PRIVATE
    main.cpp
    iconprovider.cpp
    iconprovider.h
    screenshotrunner.cpp
    screenshotrunner.h
    searchutil.cpp
    searchutil.h
)

set(qml_files
    AllShortcutRow.qml
    AllShortcutsView.qml
    AppIcon.qml
    AppListEditor.qml
    AppPicker.qml
    CanvasIllustration.qml
    Card.qml
    CategoryPage.qml
    ColorControl.qml
    ConfirmDialog.qml
    ConflictBanner.qml
    Dropdown.qml
    FocusRing.qml
    Icon.qml
    IconButton.qml
    InfoButton.qml
    Keycap.qml
    Keycaps.qml
    KeyCapture.qml
    Main.qml
    NavItem.qml
    OverviewPage.qml
    PageHeader.qml
    PageScaffold.qml
    Popover.qml
    RememberedWindowsCard.qml
    SearchField.qml
    SearchPage.qml
    SectionLabel.qml
    SegmentedControl.qml
    SettingRow.qml
    Shadow.qml
    ShortcutRow.qml
    ShortcutsPage.qml
    Sidebar.qml
    SliderControl.qml
    StatusDot.qml
    StatusTexts.qml
    Stepper.qml
    Theme.qml
    Toast.qml
    ToggleSwitch.qml
    UiButton.qml
    UiScrollBar.qml
    UiText.qml
    UiTextField.qml
    UiToolTip.qml
    UpdatesCard.qml
)
set(qml_paths)
foreach(file IN LISTS qml_files)
    # Place every file next to the module's qmldir so they share one implicit import.
    set_source_files_properties(qml/${file} PROPERTIES QT_RESOURCE_ALIAS ${file})
    list(APPEND qml_paths qml/${file})
endforeach()
set_source_files_properties(qml/Theme.qml qml/StatusTexts.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(kde-infinit-desktop-settings
    URI InfinitDesktop.Settings
    VERSION 1.0
    QML_FILES ${qml_paths}
    SOURCES
        applicationindex.cpp
        applicationindex.h
        effectstatus.cpp
        effectstatus.h
        globalshortcutsmodel.cpp
        globalshortcutsmodel.h
        settingsmodel.cpp
        settingsmodel.h
        shortcutmodel.cpp
        shortcutmodel.h
        themecontroller.cpp
        themecontroller.h
        updatechecker.cpp
        updatechecker.h
        windowplacements.cpp
        windowplacements.h
    DEPENDENCIES
        QtQuick
)

file(GLOB icon_files RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS icons/*.svg)
qt_add_resources(kde-infinit-desktop-settings "infinitdesktop_icons"
    PREFIX "/infinitdesktop"
    FILES ${icon_files}
)

target_link_libraries(kde-infinit-desktop-settings PRIVATE
    infinitdesktop_shared
    Qt6::DBus
    Qt6::Gui
    Qt6::Network
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    KF6::ConfigCore
    KF6::CoreAddons
    KF6::GlobalAccel
    KF6::I18n
)
if(TARGET KF6::I18nQml)
    target_link_libraries(kde-infinit-desktop-settings PRIVATE KF6::I18nQml)
endif()

install(TARGETS kde-infinit-desktop-settings ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
install(FILES kde-infinit-desktop-settings.desktop DESTINATION ${KDE_INSTALL_APPDIR})

# Application icon. The name must not contain dashes: KIconLoader also tries the
# dash-stripped names ("kde-infinit-desktop" -> "kde-infinit" -> "kde") in every
# theme before falling back to hicolor, so Breeze's "kde" icon would win.
# org.kde.infinitdesktop matches the AppStream id.
install(FILES icons/kde-infinit-desktop.svg DESTINATION ${KDE_INSTALL_FULL_ICONDIR}/hicolor/scalable/apps RENAME org.kde.infinitdesktop.svg)
foreach(size 16 22 24 32 48 64 128 256)
    install(FILES icons/app/org.kde.infinitdesktop-${size}.png
            DESTINATION ${KDE_INSTALL_FULL_ICONDIR}/hicolor/${size}x${size}/apps
            RENAME org.kde.infinitdesktop.png)
endforeach()
