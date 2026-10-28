cmake_minimum_required(VERSION 3.16)
project(kyauth VERSION 0.5.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# Ubuntu Touch: Ubuntu Push via Lomiri PushClient, click layout. Otherwise UnifiedPush over D-Bus.
option(KYAUTH_UBUNTU_TOUCH "Build the Ubuntu Touch click flavor" OFF)
option(KYAUTH_TESTS "Build tests" ON)

# Qt 5.15 is the stable Ubuntu Touch target; Qt 6 builds from the same source.
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core)
find_package(Qt${QT_VERSION_MAJOR} 5.15 REQUIRED COMPONENTS Core Network Gui Qml Quick QuickControls2 DBus)
find_package(OpenSSL 3.0 REQUIRED)

add_compile_options(-Wall -Wextra -Werror=format-security -fstack-protector-strong)
if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    # The Flatpak SDK already supplies level 3. Retain it without redefining it.
    include(CheckCXXSourceCompiles)
    # glibc fortifies only when optimising, and GCC on Debian/Ubuntu defines the level
    # from the optimisation flags alone. Probe with the configuration's own flags, or a
    # toolchain level goes undetected and the define below silently lowers it.
    string(TOUPPER "${CMAKE_BUILD_TYPE}" KYAUTH_CONFIG)
    set(CMAKE_REQUIRED_FLAGS "${CMAKE_CXX_FLAGS_${KYAUTH_CONFIG}}")
    check_cxx_source_compiles("\
#if !defined(_FORTIFY_SOURCE) || _FORTIFY_SOURCE < 2
#error no toolchain fortification
#endif
int main() { return 0; }" KYAUTH_TOOLCHAIN_FORTIFY)
    unset(CMAKE_REQUIRED_FLAGS)
    if(NOT KYAUTH_TOOLCHAIN_FORTIFY)
        add_compile_definitions(_FORTIFY_SOURCE=2)
    endif()
endif()
add_link_options(-Wl,-z,relro,-z,now)
add_compile_definitions(QT_NO_CAST_FROM_ASCII KYAUTH_VERSION="${PROJECT_VERSION}")

add_library(kyauthcore STATIC
    src/core/crypto.cpp
    src/core/protocol.cpp
    src/core/totp.cpp
    src/core/vault.cpp)
target_include_directories(kyauthcore PUBLIC src/core)
target_link_libraries(kyauthcore PUBLIC Qt${QT_VERSION_MAJOR}::Core OpenSSL::Crypto)

# App logic without the UI, shared by the binary and the controller tests.
add_library(kyauthapp STATIC
    src/app/controller.cpp
    src/app/unifiedpush.cpp)
target_include_directories(kyauthapp PUBLIC src/app)
target_link_libraries(kyauthapp PUBLIC kyauthcore
    Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Gui Qt${QT_VERSION_MAJOR}::DBus)
if(KYAUTH_UBUNTU_TOUCH)
    target_compile_definitions(kyauthapp PUBLIC KYAUTH_UBUNTU_TOUCH)
endif()

add_executable(kyauth src/app/main.cpp qml/qml.qrc)
target_link_libraries(kyauth PRIVATE kyauthapp
    Qt${QT_VERSION_MAJOR}::Qml Qt${QT_VERSION_MAJOR}::Quick Qt${QT_VERSION_MAJOR}::QuickControls2)

# QR scanning uses QZXing's Qt 5 camera filter. Without it, codes are pasted.
if(QT_VERSION_MAJOR EQUAL 5)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(QZXING IMPORTED_TARGET QZXing)
    endif()
endif()
if(KYAUTH_UBUNTU_TOUCH AND NOT QZXING_FOUND)
    message(FATAL_ERROR "The click needs QZXing for QR scanning.")
endif()
if(QZXING_FOUND)
    find_package(Qt5 REQUIRED COMPONENTS Multimedia)
    target_compile_definitions(kyauthapp PUBLIC KYAUTH_QR)
    target_sources(kyauth PRIVATE qml/scanner.qrc)
    target_link_libraries(kyauth PRIVATE PkgConfig::QZXING Qt5::Multimedia)
endif()

if(KYAUTH_UBUNTU_TOUCH)
    # Click layout: everything at the package root.
    install(TARGETS kyauth DESTINATION .)
    configure_file(packaging/click/manifest.json.in ${CMAKE_BINARY_DIR}/manifest.json @ONLY)
    install(FILES ${CMAKE_BINARY_DIR}/manifest.json
                  packaging/click/kyauth.apparmor packaging/click/kyauth.desktop
                  packaging/click/push-helper.json packaging/click/push-helper.apparmor
                  packaging/click/kyauth.png # full-bleed: Lomiri draws the rounded shape
            DESTINATION .)
    install(PROGRAMS packaging/click/push-helper DESTINATION .)
else()
    include(GNUInstallDirs)
    install(TARGETS kyauth DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES packaging/nura/app.busnes.kyauth.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES packaging/nura/app.busnes.kyauth.metainfo.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
    configure_file(packaging/nura/app.busnes.kyauth.service.in ${CMAKE_BINARY_DIR}/app.busnes.kyauth.service @ONLY)
    install(FILES ${CMAKE_BINARY_DIR}/app.busnes.kyauth.service DESTINATION ${CMAKE_INSTALL_DATADIR}/dbus-1/services)
    install(FILES packaging/kyauth.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/192x192/apps
            RENAME app.busnes.kyauth.png)
    # AppStream (software centres, Flatpak) looks for 64, 128 or scalable icons.
    install(FILES packaging/nura/app.busnes.kyauth-128.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/128x128/apps
            RENAME app.busnes.kyauth.png)
endif()

if(KYAUTH_TESTS)
    enable_testing()
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Test)
    foreach(t crypto protocol totp vault)
        add_executable(test_${t} tests/test_${t}.cpp)
        target_link_libraries(test_${t} PRIVATE kyauthcore Qt${QT_VERSION_MAJOR}::Test)
        add_test(NAME ${t} COMMAND test_${t})
    endforeach()
    add_executable(test_controller tests/test_controller.cpp qml/qml.qrc)
    target_link_libraries(test_controller PRIVATE kyauthapp Qt${QT_VERSION_MAJOR}::Test
        Qt${QT_VERSION_MAJOR}::Qml Qt${QT_VERSION_MAJOR}::Quick Qt${QT_VERSION_MAJOR}::QuickControls2)
    add_test(NAME controller COMMAND test_controller)
    set_tests_properties(controller PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    # Real TLS connections through Controller::request, including pre-send refusals.
    add_executable(test_tls tests/test_tls.cpp)
    target_link_libraries(test_tls PRIVATE kyauthapp Qt${QT_VERSION_MAJOR}::Test)
    add_test(NAME tls COMMAND test_tls)
    set_tests_properties(tls PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 180)
    find_program(DBUS_RUN_SESSION dbus-run-session)
    if(NOT DBUS_RUN_SESSION)
        message(FATAL_ERROR "Tests require dbus-run-session (dbus-daemon package)")
    endif()
    add_executable(test_unifiedpush tests/test_unifiedpush.cpp)
    target_link_libraries(test_unifiedpush PRIVATE kyauthapp Qt${QT_VERSION_MAJOR}::Test)
    add_test(NAME unifiedpush COMMAND "${CMAKE_COMMAND}" -E env KYAUTH_TEST_PRIVATE_BUS=1 "${DBUS_RUN_SESSION}"
        "--config-file=${CMAKE_CURRENT_SOURCE_DIR}/tests/dbus-session.conf" -- $<TARGET_FILE:test_unifiedpush>)
    set_tests_properties(unifiedpush PROPERTIES TIMEOUT 30)
    # Loads the real QML offscreen and fails on any QML warning.
    add_test(NAME qml_smoke COMMAND kyauth --smoke-test)
    # Software scene graph: headless runners have no OpenGL.
    set_tests_properties(qml_smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
endif()
