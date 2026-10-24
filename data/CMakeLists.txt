# SPDX-FileCopyrightText: 2026 KDE Infinit Desktop contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Distribution metadata (AppStream).

# AppStream metadata (software centers like Discover). It describes the settings
# app as launchable, so it is only installed together with the app.
if(BUILD_SETTINGS_APP AND EXISTS ${PROJECT_SOURCE_DIR}/src/app/CMakeLists.txt)
    install(FILES org.kde.infinitdesktop.metainfo.xml DESTINATION ${KDE_INSTALL_METAINFODIR})
endif()

if(BUILD_TESTING)
    find_program(APPSTREAMCLI_EXECUTABLE appstreamcli)
    if(APPSTREAMCLI_EXECUTABLE)
        add_test(NAME metainfo-validate
            COMMAND ${APPSTREAMCLI_EXECUTABLE} validate --no-net --explain
                    ${CMAKE_CURRENT_SOURCE_DIR}/org.kde.infinitdesktop.metainfo.xml)
    endif()
endif()

# Note: <datadir>/kde-infinit-desktop/built-for-kwin (the KWin version the effect
# was built against, read by scripts/check-kwin-update.sh and the automatic
# rebuild hook) is installed by src/effect/CMakeLists.txt.
