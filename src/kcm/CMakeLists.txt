# SPDX-FileCopyrightText: 2026 KDE Infinite Desktop contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# System Settings module of the effect (Desktop Effects -> KDE Infinite Desktop -> Configure).
# The form is generated from infinitedesktop.kcfg + settingsmeta, so new entries show up
# automatically.

find_package(KF6 ${KF6_MIN_VERSION} REQUIRED COMPONENTS ConfigWidgets WidgetsAddons XmlGui)

kcoreaddons_add_plugin(kwin_infinitedesktop_config
    INSTALL_NAMESPACE "kwin/effects/configs"
    SOURCES
        infinitedesktopkcm.cpp
        infinitedesktopkcm.h
        stringlistedit.cpp
        stringlistedit.h
)

target_compile_definitions(kwin_infinitedesktop_config PRIVATE
    INFINITEDESKTOP_SETTINGS_APP_PATH="${KDE_INSTALL_FULL_BINDIR}/kde-infinite-desktop-settings"
)

target_link_libraries(kwin_infinitedesktop_config
    infinitedesktop_shared
    Qt6::DBus
    Qt6::Widgets
    KF6::ConfigWidgets
    KF6::CoreAddons
    KF6::GlobalAccel
    KF6::I18n
    KF6::KCMUtils
    KF6::WidgetsAddons
    KF6::XmlGui
)
