# SPDX-License-Identifier: GPL-3.0-or-later
#
# Pure logic without any KWin dependency. Everything in here is unit tested
# (see tests/) and can be run with sanitizers outside of the compositor.

add_library(infinitedesktop_core STATIC
    areas.cpp
    camera.cpp
    groups.cpp
    minimap.cpp
    motion.cpp
    navigation.cpp
    noteplacements.cpp
    parking.cpp
    placementmemory.cpp
    velocitytracker.cpp
)
set_target_properties(infinitedesktop_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(infinitedesktop_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(infinitedesktop_core PUBLIC Qt6::Core)
