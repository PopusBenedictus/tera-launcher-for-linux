cmake_minimum_required(VERSION 3.16)
project(terautils C)

set(CMAKE_C_STANDARD 23)

add_library(terautils STATIC
        util.c
)

target_include_directories(terautils
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
)

install(TARGETS terautils
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib
        RUNTIME DESTINATION bin
)

install(FILES util.h DESTINATION include)
