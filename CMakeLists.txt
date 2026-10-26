cmake_minimum_required(VERSION 3.21)

project(OpenTalk
    VERSION 1.0.0
    DESCRIPTION "Open-source voice, text and file transfer platform"
    HOMEPAGE_URL "https://github.com/mheesen65/OpenTalk"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Qt sits behind these components; moc needs AUTOMOC for every target below.
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

include(GNUInstallDirs)

# The portable package puts the binaries next to the Qt libraries it bundles.
# Without an $ORIGIN rpath the loader only searches system paths, so an
# extracted archive would fail unless LD_LIBRARY_PATH happened to be set.
if(UNIX AND NOT APPLE)
    set(CMAKE_INSTALL_RPATH "$ORIGIN")
    set(CMAKE_BUILD_WITH_INSTALL_RPATH OFF)
endif()

option(OPENTALK_BUILD_SERVER "Build the headless OpenTalk server" ON)
option(OPENTALK_BUILD_CLIENT "Build the Qt desktop client" ON)
option(OPENTALK_BUILD_TESTS "Build the unit tests" ON)

# Static Qt builds cannot be redistributed under LGPLv3 without extra
# obligations; the portable package links Qt dynamically. See README.
find_package(Qt6 6.2 REQUIRED COMPONENTS Core Network)

# Opus is a hard requirement: it is the only audio codec the protocol carries.
find_path(OPUS_INCLUDE_DIR opus/opus.h)
find_library(OPUS_LIBRARY NAMES opus)
if(NOT OPUS_INCLUDE_DIR OR NOT OPUS_LIBRARY)
    message(FATAL_ERROR
        "libopus was not found. Install it (Debian/Ubuntu: libopus-dev, "
        "macOS: brew install opus, Windows: vcpkg install opus:x64-windows) "
        "or pass -DOPUS_INCLUDE_DIR and -DOPUS_LIBRARY explicitly.")
endif()
add_library(OpenTalk::Opus UNKNOWN IMPORTED)
set_target_properties(OpenTalk::Opus PROPERTIES
    IMPORTED_LOCATION "${OPUS_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${OPUS_INCLUDE_DIR}")

option(OPENTALK_ENABLE_AUDIO "Build microphone/speaker support (needs Qt Multimedia)" ON)
set(OPENTALK_AUDIO_DEVICES OFF)
if(OPENTALK_ENABLE_AUDIO AND OPENTALK_BUILD_CLIENT)
    find_package(Qt6 6.2 QUIET COMPONENTS Multimedia)
    if(Qt6Multimedia_FOUND)
        set(OPENTALK_AUDIO_DEVICES ON)
    else()
        message(WARNING
            "Qt Multimedia not found: the client will build without "
            "microphone/speaker support. Pass -DOPENTALK_ENABLE_AUDIO=OFF to silence this.")
    endif()
endif()

add_subdirectory(sdk)

if(OPENTALK_BUILD_SERVER)
    add_subdirectory(server)
endif()

if(OPENTALK_BUILD_CLIENT)
    find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets)
    add_subdirectory(client)
endif()

if(OPENTALK_BUILD_TESTS)
    enable_testing()
    find_package(Qt6 6.2 REQUIRED COMPONENTS Test)
    add_subdirectory(tests)
endif()

# Portable, no-install ZIP: `cpack -G ZIP` after a Release build.
set(CPACK_GENERATOR "ZIP")
set(CPACK_PACKAGE_NAME "OpenTalk")
set(CPACK_PACKAGE_VENDOR "OpenTalk contributors")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_FILE_NAME "OpenTalk-${PROJECT_VERSION}-portable")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
include(CPack)
