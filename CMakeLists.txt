cmake_minimum_required(VERSION 3.16)
project(tera_launcher_for_linux)

# Define a common output directory where all executables and libraries will be installed.
set(COMMON_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
# These settings ensure that libraries and executables end up in the same location.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${COMMON_RUNTIME_OUTPUT_DIRECTORY})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${COMMON_RUNTIME_OUTPUT_DIRECTORY})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${COMMON_RUNTIME_OUTPUT_DIRECTORY})

# --------------------------------------------------------------------------
# Build the utils subproject (with preferred native toolchain)
# --------------------------------------------------------------------------
add_subdirectory(utils)

# --------------------------------------------------------------------------
# Build the GUI subproject (with preferred native toolchain)
# --------------------------------------------------------------------------
add_subdirectory(gui)

# --------------------------------------------------------------------------
# Build the stub launcher as an external project using winegcc.
# The stub launcher project is self-contained in the "stub" subdirectory.
# We pass -DCMAKE_C_COMPILER=winegcc to force that compiler.
# Also, we set the install prefix so that the stub launcher is copied to our common output folder.
# --------------------------------------------------------------------------
include(ExternalProject)
ExternalProject_Add(stub_launcher_ex
        SOURCE_DIR         ${CMAKE_SOURCE_DIR}/stub
        BINARY_DIR         ${CMAKE_BINARY_DIR}/stub-build
        DEPENDS            terautils
        CMAKE_ARGS         -DCMAKE_INSTALL_PREFIX=${COMMON_RUNTIME_OUTPUT_DIRECTORY}
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DCMAKE_C_COMPILER=winegcc
        -DTERAUTILS_LIB=$<TARGET_FILE:terautils>
        -DTERAUTILS_INCLUDE_DIR=${CMAKE_SOURCE_DIR}/utils
        INSTALL_COMMAND    ${CMAKE_COMMAND} --build . --target install
)

# --------------------------------------------------------------------------
# Build easylzma as an external project.
# Its install prefix is set so that its binaries also end up in COMMON_RUNTIME_OUTPUT_DIRECTORY.
# --------------------------------------------------------------------------
ExternalProject_Add(easylzma
        GIT_REPOSITORY      https://github.com/justkeepquiet/easylzma.git
        GIT_TAG             master
        PREFIX              ${CMAKE_BINARY_DIR}/external/easylzma
        CMAKE_ARGS          -Wno-dev
                            -DCMAKE_INSTALL_PREFIX=${COMMON_RUNTIME_OUTPUT_DIRECTORY}
                            -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
                            -DCMAKE_C_STANDARD=99
                            -DCMAKE_C_FLAGS=-std=gnu99\ -I${CMAKE_BINARY_DIR}/external/easylzma/src/easylzma/src/easylzma\ -I${CMAKE_BINARY_DIR}/external/easylzma/src/easylzma/src/pavlov\ -I${CMAKE_BINARY_DIR}/external/easylzma/src/easylzma/elzma\ -I${CMAKE_BINARY_DIR}/external/easylzma/src/easylzma/test
        BUILD_ALWAYS        1
        UPDATE_DISCONNECTED 1
        INSTALL_COMMAND ${CMAKE_COMMAND} -E copy_directory
                          ${CMAKE_BINARY_DIR}/external/easylzma/src/easylzma-build/easylzma-0.0.8/bin
                          ${COMMON_RUNTIME_OUTPUT_DIRECTORY}
)
