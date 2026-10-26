# SPDX-License-Identifier: GPL-3.0-or-later
#
# The KWin effect. It runs inside kwin_wayland and must be built against the
# exact KWin version it is loaded into (KWin plugins have no stable ABI; KWin
# refuses to load a plugin that was built for another version).

set(infinitedesktop_SOURCES
    main.cpp
    infinitedesktopeffect.cpp
    inputfilter.cpp
    canvasrenderer.cpp
    crashguard.cpp
    statestore.cpp
    dbusinterface.cpp
    launcherpositioner.cpp
    organizer.cpp
)

ecm_qt_declare_logging_category(infinitedesktop_SOURCES
    HEADER logging.h
    IDENTIFIER KWIN_INFINITEDESKTOP
    CATEGORY_NAME kwin_effect_infinitedesktop
    DEFAULT_SEVERITY Warning
    DESCRIPTION "KDE Infinite Desktop (KWin effect)"
)

kcoreaddons_add_plugin(infinitedesktop INSTALL_NAMESPACE "kwin/effects/plugins" SOURCES ${infinitedesktop_SOURCES})

target_link_libraries(infinitedesktop PRIVATE
    infinitedesktop_core
    infinitedesktop_shared
    KWin::kwin
    KF6::ConfigCore
    KF6::ConfigGui
    KF6::CoreAddons
    KF6::GlobalAccel
    KF6::I18n
    Qt6::DBus
    Qt6::Gui
)

# Remember which KWin version this plugin was built for (used by the settings
# app and the rebuild helper to detect a KWin update).
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/built-for-kwin "${KWin_VERSION}\n")
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/built-for-kwin DESTINATION ${KDE_INSTALL_DATADIR}/kde-infinite-desktop)
