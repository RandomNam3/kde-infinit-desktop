# SPDX-License-Identifier: GPL-3.0-or-later

add_subdirectory(shared)
add_subdirectory(core)

if(BUILD_EFFECT AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/effect/CMakeLists.txt)
    add_subdirectory(effect)
endif()

if(BUILD_KCM AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/kcm/CMakeLists.txt)
    add_subdirectory(kcm)
endif()

if(BUILD_SETTINGS_APP AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/app/CMakeLists.txt)
    add_subdirectory(app)
endif()

if(BUILD_COMPANION AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/companion/CMakeLists.txt)
    add_subdirectory(companion)
endif()
