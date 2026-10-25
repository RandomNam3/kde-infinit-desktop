# KDE Infinit Desktop - infinite canvas for KWin (Plasma 6, Wayland)
# SPDX-FileCopyrightText: 2026 KDE Infinit Desktop contributors
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.22)

project(kde-infinit-desktop VERSION 0.5.1 LANGUAGES CXX)

set(QT_MIN_VERSION "6.8.0")
set(KF6_MIN_VERSION "6.0.0")
set(KWIN_MIN_VERSION "6.8.0")

find_package(ECM ${KF6_MIN_VERSION} REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(ECMQtDeclareLoggingCategory)
include(FeatureSummary)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BUILD_EFFECT "Build the KWin effect (needs KWin development files)" ON)
option(BUILD_KCM "Build the System Settings module" ON)
option(BUILD_SETTINGS_APP "Build the standalone settings app" ON)
# The companion (notes, tray icon) is a GUI app like the settings app and follows it by default.
option(BUILD_COMPANION "Build the companion app (notes, tray icon)" ${BUILD_SETTINGS_APP})
option(ENABLE_SANITIZERS "Build with AddressSanitizer and UndefinedBehaviorSanitizer (development only)" OFF)

if(ENABLE_SANITIZERS)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

# One translation domain for everything the project ships.
add_compile_definitions(TRANSLATION_DOMAIN=\"kwin_infinitdesktop\")
add_compile_definitions(INFINITDESKTOP_VERSION_STRING=\"${PROJECT_VERSION}\")
add_compile_definitions(QT_NO_KEYWORDS QT_NO_CAST_FROM_ASCII QT_NO_URL_CAST_FROM_STRING)

find_package(Qt6 ${QT_MIN_VERSION} CONFIG REQUIRED COMPONENTS Core Gui DBus Widgets)
find_package(KF6 ${KF6_MIN_VERSION} REQUIRED COMPONENTS Config CoreAddons GlobalAccel I18n)

if(BUILD_EFFECT)
    find_package(KWin ${KWIN_MIN_VERSION} REQUIRED COMPONENTS kwin)
    set_package_properties(KWin PROPERTIES TYPE REQUIRED PURPOSE "The effect is a KWin plugin")
    message(STATUS "Building against KWin ${KWin_VERSION}")
endif()

if(BUILD_KCM)
    find_package(KF6 ${KF6_MIN_VERSION} REQUIRED COMPONENTS KCMUtils)
endif()

if(BUILD_SETTINGS_APP)
    find_package(Qt6 ${QT_MIN_VERSION} CONFIG REQUIRED COMPONENTS Quick QuickControls2)
endif()

add_subdirectory(src)

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/data/CMakeLists.txt)
    add_subdirectory(data)
endif()

if(BUILD_TESTING)
    find_package(Qt6 ${QT_MIN_VERSION} CONFIG REQUIRED COMPONENTS Test)
    add_subdirectory(tests)
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/po)
    ki18n_install(po)
endif()

feature_summary(WHAT ALL INCLUDE_QUIET_PACKAGES FATAL_ON_MISSING_REQUIRED_PACKAGES)
