# SPDX-License-Identifier: GPL-3.0-or-later
#
# Settings schema, shortcut table and UI metadata shared by the effect,
# the System Settings module and the settings app.

add_library(infinitedesktop_shared STATIC
    shortcutdefs.cpp
    settingsmeta.cpp
)
kconfig_add_kcfg_files(infinitedesktop_shared infinitedesktopconfig.kcfgc)

set_target_properties(infinitedesktop_shared PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(infinitedesktop_shared PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
)
target_link_libraries(infinitedesktop_shared PUBLIC
    Qt6::Core
    Qt6::Gui
    KF6::ConfigCore
    KF6::ConfigGui
    KF6::I18n
)

install(FILES infinitedesktop.kcfg DESTINATION ${KDE_INSTALL_KCFGDIR})
install(FILES org.kde.InfiniteDesktop.xml DESTINATION ${KDE_INSTALL_DBUSINTERFACEDIR})
