# SPDX-FileCopyrightText: 2026 KDE Infinit Desktop contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# System Settings module of the effect (Desktop Effects -> KDE Infinit Desktop -> Configure).
# The form is generated from infinitdesktop.kcfg + settingsmeta, so new entries show up
# automatically.

find_package(KF6 ${KF6_MIN_VERSION} REQUIRED COMPONENTS ConfigWidgets WidgetsAddons XmlGui)

kcoreaddons_add_plugin(kwin_infinitdesktop_config
    INSTALL_NAMESPACE "kwin/effects/configs"
    SOURCES
        infinitdesktopkcm.cpp
        infinitdesktopkcm.h
        stringlistedit.cpp
        stringlistedit.h
)

target_compile_definitions(kwin_infinitdesktop_config PRIVATE
    INFINITDESKTOP_SETTINGS_APP_PATH="${KDE_INSTALL_FULL_BINDIR}/kde-infinit-desktop-settings"
)

target_link_libraries(kwin_infinitdesktop_config
    infinitdesktop_shared
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
