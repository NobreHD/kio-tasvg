# SPDX-FileCopyrightText: 2026 Leonardo Nobre <info@nobrehd.pt>
# SPDX-License-Identifier: LGPL-2.0-or-later

cmake_minimum_required(VERSION 3.16)

project(kio-tasvg VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(QT_MIN_VERSION "6.5.0")
set(KF_MIN_VERSION "6.0.0")

find_package(ECM ${KF_MIN_VERSION} REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(FeatureSummary)

find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS
    Core
    Gui
    Svg
)

find_package(KF6 ${KF_MIN_VERSION} REQUIRED COMPONENTS
    KIO
    Archive
    Config
    ColorScheme
)

# Build the thumbnailer plugin
add_library(tasvgthumbnail MODULE
    tasvgcreator.cpp
    tasvgcreator.h
)
target_link_libraries(tasvgthumbnail
    Qt6::Core
    Qt6::Gui
    Qt6::Svg
    KF6::KIOGui
    KF6::Archive
    KF6::ConfigGui
    KF6::ColorScheme
)

# Install plugin to Qt6 / KF6 thumbcreator directory
install(TARGETS tasvgthumbnail DESTINATION ${KDE_INSTALL_QTPLUGINDIR}/kf6/thumbcreator)

option(BUILD_DAEMON "Build the optional auto-theme sync daemon" ON)

if(BUILD_DAEMON)
    find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS DBus Concurrent)

    add_executable(tasvg-daemon
        daemon/main.cpp
    )

    target_link_libraries(tasvg-daemon
        Qt6::Core
        Qt6::Gui
        Qt6::DBus
        Qt6::Concurrent
    )

    install(TARGETS tasvg-daemon DESTINATION ${KDE_INSTALL_BINDIR})

    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/daemon/tasvg-daemon.service.in
        ${CMAKE_CURRENT_BINARY_DIR}/tasvg-daemon.service
        @ONLY
    )

    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/tasvg-daemon.service
            DESTINATION ${KDE_INSTALL_SYSTEMDUSERUNITDIR})
endif()

# --- Packaging with CPack (.deb, .rpm, .tar.gz) ---
set(CPACK_PACKAGE_NAME "kio-tasvg")
set(CPACK_PACKAGE_VENDOR "NobreHD")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Theme-Aware SVG thumbnailer for KDE Plasma 6")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_GENERATOR "DEB;RPM;TGZ")

# Debian packaging: automatically detect shared library dependencies
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Leonardo Nobre <info@nobrehd.pt>")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)

# RPM packaging automatically detects shared library dependencies via rpmbuild
set(CPACK_RPM_PACKAGE_LICENSE "LGPL-2.0-or-later")

include(CPack)

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)