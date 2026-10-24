# SPDX-License-Identifier: GPL-3.0-or-later

include(ECMAddTests)

ecm_add_tests(
    test_camera.cpp
    test_motion.cpp
    test_velocitytracker.cpp
    test_parking.cpp
    test_navigation.cpp
    test_placementmemory.cpp
    LINK_LIBRARIES infinitdesktop_core Qt6::Test
)

ecm_add_test(test_shared.cpp
    LINK_LIBRARIES infinitdesktop_shared Qt6::Test
)
