cmake_minimum_required(VERSION 3.10)
project(AsyncOpExamples VERSION 2.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find dependencies
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED glib-2.0)

find_package(spdlog REQUIRED)

# Try to find Qt5 (optional)
find_package(Qt5 COMPONENTS Core Widgets Network QUIET)
if(NOT Qt5_FOUND)
    message(WARNING "Qt5 not found - building without Qt examples")
endif()

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../include)

# Options for different example builds
option(BUILD_EXAMPLE_CALLBACK_CONVERSION "Build callback conversion example" ON)
option(BUILD_EXAMPLE_MESSAGE_REGISTRY "Build message registry example" ON)

# Qt-based examples (compile with ASYNC_USE_QT) - only if Qt is available
if(Qt5_FOUND)
    # Example: Callback conversion (Qt version)
    if(BUILD_EXAMPLE_CALLBACK_CONVERSION)
        add_executable(example_callback_conversion_qt
            example_callback_conversion.cpp)
        set_target_properties(example_callback_conversion_qt
            PROPERTIES COMPILE_DEFINITIONS "ASYNC_USE_QT")
        target_link_libraries(example_callback_conversion_qt
            Qt5::Core Qt5::Widgets spdlog::spdlog)
    endif()

    # Example: Message registry (Qt version)
    if(BUILD_EXAMPLE_MESSAGE_REGISTRY)
        add_executable(example_message_registry_qt
            example_message_registry.cpp)
        set_target_properties(example_message_registry_qt
            PROPERTIES COMPILE_DEFINITIONS "ASYNC_USE_QT")
        target_link_libraries(example_message_registry_qt
            Qt5::Core Qt5::Widgets spdlog::spdlog)
    endif()
endif()

# GLib-based examples
if(BUILD_EXAMPLE_CALLBACK_CONVERSION)
    add_executable(example_callback_conversion_glib 
        example_callback_conversion.cpp)
    target_include_directories(example_callback_conversion_glib 
        PRIVATE ${GLIB_INCLUDE_DIRS})
    target_compile_options(example_callback_conversion_glib 
        PRIVATE ${GLIB_CFLAGS_OTHER})
    target_link_libraries(example_callback_conversion_glib 
        ${GLIB_LIBRARIES} spdlog::spdlog)
endif()

if(BUILD_EXAMPLE_MESSAGE_REGISTRY)
    add_executable(example_message_registry_glib 
        example_message_registry.cpp)
    target_include_directories(example_message_registry_glib 
        PRIVATE ${GLIB_INCLUDE_DIRS})
    target_compile_options(example_message_registry_glib 
        PRIVATE ${GLIB_CFLAGS_OTHER})
    target_link_libraries(example_message_registry_glib 
        ${GLIB_LIBRARIES} spdlog::spdlog)
endif()

# Qt-only HTTP example (only if Qt is available)
if(Qt5_FOUND)
    add_executable(example_qt_http
        example_qt_http.cpp)
    target_compile_definitions(example_qt_http PRIVATE ASYNC_USE_QT)
    target_link_libraries(example_qt_http
        Qt5::Core Qt5::Widgets Qt5::Network spdlog::spdlog)

    # Option to build Qt HTTP example
    option(BUILD_EXAMPLE_QT_HTTP "Build Qt HTTP example" ON)
    if(BUILD_EXAMPLE_QT_HTTP)
        set_target_properties(example_qt_http PROPERTIES EXCLUDE_FROM_ALL FALSE)
    else()
        set_target_properties(example_qt_http PROPERTIES EXCLUDE_FROM_ALL TRUE)
    endif()
endif()