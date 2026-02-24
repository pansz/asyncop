cmake_minimum_required(VERSION 3.10)
project(AsyncOpExamples VERSION 2.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find dependencies
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED glib-2.0)
find_package(Qt5 COMPONENTS Core Widgets REQUIRED)
find_package(spdlog REQUIRED)

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../include)

# Options for different example builds
option(BUILD_EXAMPLE_CALLBACK_CONVERSION "Build callback conversion example" ON)
option(BUILD_EXAMPLE_MESSAGE_REGISTRY "Build message registry example" ON)

# Example: Callback conversion
if(BUILD_EXAMPLE_CALLBACK_CONVERSION)
    add_executable(example_callback_conversion 
        example_callback_conversion.cpp)
    target_link_libraries(example_callback_conversion 
        Qt5::Core Qt5::Widgets ${GLIB_LIBRARIES} spdlog::spdlog)
endif()

# Example: Message registry
if(BUILD_EXAMPLE_MESSAGE_REGISTRY)
    add_executable(example_message_registry 
        example_message_registry.cpp)
    target_link_libraries(example_message_registry 
        Qt5::Core Qt5::Widgets ${GLIB_LIBRARIES} spdlog::spdlog)
endif()

# Qt-based examples (compile with ASYNC_USE_QT)
if(BUILD_EXAMPLE_CALLBACK_CONVERSION)
    add_executable(example_callback_conversion_qt 
        example_callback_conversion.cpp)
    set_target_properties(example_callback_conversion_qt 
        PROPERTIES COMPILE_DEFINITIONS "ASYNC_USE_QT")
    target_link_libraries(example_callback_conversion_qt 
        Qt5::Core Qt5::Widgets spdlog::spdlog)
endif()

if(BUILD_EXAMPLE_MESSAGE_REGISTRY)
    add_executable(example_message_registry_qt 
        example_message_registry.cpp)
    set_target_properties(example_message_registry_qt 
        PROPERTIES COMPILE_DEFINITIONS "ASYNC_USE_QT")
    target_link_libraries(example_message_registry_qt 
        Qt5::Core Qt5::Widgets spdlog::spdlog)
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