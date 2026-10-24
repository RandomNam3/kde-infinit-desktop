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

if(BUILD_SETTINGS_APP)
    find_package(Qt6 ${QT_MIN_VERSION} CONFIG REQUIRED COMPONENTS Network Qml)
    ecm_add_test(test_updatechecker.cpp ${PROJECT_SOURCE_DIR}/src/app/updatechecker.cpp
        TEST_NAME test_updatechecker
        LINK_LIBRARIES Qt6::Network Qt6::Qml KF6::ConfigCore KF6::I18n Qt6::Test
    )
    target_include_directories(test_updatechecker PRIVATE ${PROJECT_SOURCE_DIR}/src/app)
endif()
