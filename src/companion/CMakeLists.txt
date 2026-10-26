# SPDX-FileCopyrightText: 2026 KDE Infinite Desktop contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Companion app "kde-infinite-desktop-companion": notes on the canvas and the notes icon
# in the system tray. Started by D-Bus when the effect needs it, and at login.

find_package(Qt6 ${QT_MIN_VERSION} CONFIG REQUIRED COMPONENTS DBus Qml Quick QuickControls2 Widgets)

qt_policy(SET QTP0001 NEW)
if(QT_KNOWN_POLICY_QTP0004)
    qt_policy(SET QTP0004 NEW)
endif()

add_executable(kde-infinite-desktop-companion
    main.cpp
    companion.cpp
    companion.h
    notestore.cpp
    notestore.h
)

qt_add_qml_module(kde-infinite-desktop-companion
    URI InfiniteDesktop.Companion
    VERSION 1.0
    QML_FILES qml/NoteWindow.qml
    DEPENDENCIES QtQuick
)

target_link_libraries(kde-infinite-desktop-companion PRIVATE
    infinitedesktop_shared
    Qt6::DBus
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Widgets
    KF6::ConfigCore
    KF6::CoreAddons
    KF6::I18n
)
if(TARGET KF6::I18nQml)
    target_link_libraries(kde-infinite-desktop-companion PRIVATE KF6::I18nQml)
endif()

install(TARGETS kde-infinite-desktop-companion ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
install(FILES org.kde.infinitedesktop.companion.desktop DESTINATION ${KDE_INSTALL_APPDIR})
install(FILES org.kde.infinitedesktop.companion.autostart.desktop DESTINATION ${KDE_INSTALL_AUTOSTARTDIR}
        RENAME org.kde.infinitedesktop.companion.desktop)
configure_file(org.kde.InfiniteDesktop.Companion.service.in ${CMAKE_CURRENT_BINARY_DIR}/org.kde.InfiniteDesktop.Companion.service @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/org.kde.InfiniteDesktop.Companion.service DESTINATION ${KDE_INSTALL_DBUSSERVICEDIR})
