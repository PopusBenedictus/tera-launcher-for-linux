# stub/CMakeLists.txt
cmake_minimum_required(VERSION 3.16)
project(stub_launcher_project C)

set(CMAKE_C_STANDARD 23)
set(CMAKE_EXECUTABLE_SUFFIX ".exe")
set(CMAKE_BUILD_WITH_INSTALL_RPATH ON)

message(STATUS "Configuring stub launcher (will be built with winegcc).")

set(TERAUTILS_LIB "" CACHE STRING "Path to the compiled terautils library")
set(TERAUTILS_INCLUDE_DIR "" CACHE PATH "Path to the terautils includes")

# Find required packages for the stub launcher.
find_package(CURL REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(OpenSSL REQUIRED)
pkg_check_modules(JANSSON REQUIRED jansson)
pkg_check_modules(MXML REQUIRED mxml)

# Build the common libraries.
add_library(serverlist_fetch STATIC
        ${CMAKE_SOURCE_DIR}/serverlist_fetch.c
        ${CMAKE_SOURCE_DIR}/serverlist.pb-c.c
)
target_include_directories(serverlist_fetch PRIVATE
        ${TERAUTILS_INCLUDE_DIR}
        ${PROTOBUF_C_INCLUDE_DIRS}
        ${CURL_INCLUDE_DIRS}
        ${MXML_INCLUDE_DIRS}
)
target_link_libraries(serverlist_fetch PRIVATE
        ${TERAUTILS_LIB}
        ${CURL_LIBRARIES}
        protobuf-c
        ${MXML_LIBRARIES}
)

add_library(teralib STATIC
        ${CMAKE_SOURCE_DIR}/teralib.c
        ${CMAKE_SOURCE_DIR}/serverlist.pb-c.c
)
target_link_libraries(teralib PRIVATE
        ${TERAUTILS_LIB}
        ${CURL_LIBRARIES}
        ${JANSSON_LIBRARIES}
        protobuf-c
)

# Locate Wine headers (needed for teralib in this build)
find_path(WINE_WINDOWS_INCLUDE_DIR NAMES "wine/windows"
        HINTS "/usr/include" "/usr/local/include" "/opt/wine/include" "/usr/include/wine"
)
if(NOT WINE_WINDOWS_INCLUDE_DIR)
    message(FATAL_ERROR "Could not find Wine headers. Please set WINE_WINDOWS_INCLUDE_DIR.")
else()
    message(STATUS "Found Wine Windows headers at: ${WINE_WINDOWS_INCLUDE_DIR}")
endif()
target_include_directories(teralib PRIVATE
        ${TERAUTILS_INCLUDE_DIR}
        ${WINE_WINDOWS_INCLUDE_DIR}
)

# Build the stub launcher executable.
add_executable(stub_launcher ${CMAKE_CURRENT_SOURCE_DIR}/stub_launcher.c)
set_target_properties(stub_launcher PROPERTIES LINK_FLAGS "-mconsole")

target_include_directories(stub_launcher PRIVATE
        ${TERAUTILS_INCLUDE_DIR}
)

target_link_libraries(stub_launcher PRIVATE
        ${TERAUTILS_LIB}
        teralib
        serverlist_fetch
)

# Installation
install(TARGETS stub_launcher RUNTIME DESTINATION .)
install(FILES "$<TARGET_FILE:stub_launcher>.so" DESTINATION .)
