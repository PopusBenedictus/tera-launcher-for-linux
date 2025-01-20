cmake_minimum_required(VERSION 3.16)
project(tera_launcher_for_linux_gui C)

set(CMAKE_C_STANDARD 23)

# Directories for GTK assets and resource generation
set(GTK_ASSETS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/gtk-assets")
set(GRESOURCE_XML "${GTK_ASSETS_DIR}/mylauncher.gresource.xml")
set(GRESOURCE_C "${CMAKE_BINARY_DIR}/generated/mylauncher.gresource.c")

file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/generated)

# Find required packages
find_package(CURL REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Python3 COMPONENTS Interpreter REQUIRED)  # For our asset fetching script
find_package(SQLite3 REQUIRED)

pkg_check_modules(JANSSON REQUIRED jansson)
pkg_check_modules(PROTOBUF_C REQUIRED libprotobuf-c)
pkg_check_modules(GTK4 REQUIRED gtk4)

include_directories(
        ${CURL_INCLUDE_DIRS}
        ${JANSSON_INCLUDE_DIRS}
        ${GTK4_INCLUDE_DIRS}
)

#############################################
# Step 1: Define the path to the Python script.
#############################################
# Adjust the path below if your script is stored elsewhere.
set(FETCH_ASSETS_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/fetch_launcher_assets.py")

#############################################
# Step 2: Add a custom command to fetch the assets.
# A stamp file (.assets_downloaded) will be created in the gtk-assets directory when successful.
#############################################
set(ASSETS_STAMP "${GTK_ASSETS_DIR}/.assets_downloaded")
add_custom_command(
        OUTPUT ${ASSETS_STAMP}
        COMMAND ${Python3_EXECUTABLE} ${FETCH_ASSETS_SCRIPT} ${GTK_ASSETS_DIR}
        COMMAND ${CMAKE_COMMAND} -E touch ${ASSETS_STAMP}
        DEPENDS ${GTK_ASSETS_DIR}/launcher-config.json
        COMMENT "Fetching launcher assets using launcher-config.json..."
)
add_custom_target(fetch_launcher_assets
        DEPENDS ${ASSETS_STAMP}
)

#############################################
# Compile resource bundle into ugly ass struct in a .c file to embed into the executable
#############################################
add_custom_command(
        OUTPUT ${GRESOURCE_C}
        COMMAND glib-compile-resources
        --generate-source
        --sourcedir=${GTK_ASSETS_DIR}
        --target=${GRESOURCE_C}
        ${GRESOURCE_XML}
        DEPENDS fetch_launcher_assets  # e.g. ensure assets are downloaded
        COMMENT "Compiling GResource into .c"
)

set_source_files_properties(${GRESOURCE_C} PROPERTIES GENERATED TRUE)
add_custom_target(gtk_build_resources DEPENDS ${GRESOURCE_C})

#############################################
# Build the GUI executable
#############################################
add_executable(tera_launcher_for_linux
        ${CMAKE_CURRENT_SOURCE_DIR}/main.c
        ${CMAKE_CURRENT_SOURCE_DIR}/updater.c
        ${GRESOURCE_C}  # the generated file
)
add_dependencies(tera_launcher_for_linux gtk_build_resources)
target_link_libraries(tera_launcher_for_linux PRIVATE
        ${GTK4_LIBRARIES}
        ${JANSSON_LIBRARIES}
        ${CURL_LIBRARIES}
        ${OPENSSL_LIBRARIES}
        SQLite::SQLite3
        terautils
)
target_compile_options(tera_launcher_for_linux PRIVATE ${GTK4_CFLAGS_OTHER})

#############################################
# Install the GUI executable
#############################################
install(TARGETS tera_launcher_for_linux RUNTIME DESTINATION .)
