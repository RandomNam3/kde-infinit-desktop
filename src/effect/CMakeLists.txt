# SPDX-License-Identifier: GPL-3.0-or-later
#
# The KWin effect. It runs inside kwin_wayland and must be built against the
# exact KWin version it is loaded into (KWin plugins have no stable ABI; KWin
# refuses to load a plugin that was built for another version).

set(infinitdesktop_SOURCES
    main.cpp
    infinitdesktopeffect.cpp
    inputfilter.cpp
    canvasrenderer.cpp
    crashguard.cpp
    statestore.cpp
    dbusinterface.cpp
    launcherpositioner.cpp
    organizer.cpp
)

ecm_qt_declare_logging_category(infinitdesktop_SOURCES
    HEADER logging.h
    IDENTIFIER KWIN_INFINITDESKTOP
    CATEGORY_NAME kwin_effect_infinitdesktop
    DEFAULT_SEVERITY Warning
    DESCRIPTION "KDE Infinit Desktop (KWin effect)"
)

kcoreaddons_add_plugin(infinitdesktop INSTALL_NAMESPACE "kwin/effects/plugins" SOURCES ${infinitdesktop_SOURCES})

target_link_libraries(infinitdesktop PRIVATE
    infinitdesktop_core
    infinitdesktop_shared
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
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/built-for-kwin DESTINATION ${KDE_INSTALL_DATADIR}/kde-infinit-desktop)
