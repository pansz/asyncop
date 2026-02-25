cmake_minimum_required(VERSION 3.10)
project(AsyncOp VERSION 2.3 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Options
option(BUILD_TESTS "Build tests" ON)

# Library (header-only)
add_library(asyncop INTERFACE)
target_include_directories(asyncop INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Find dependencies
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED glib-2.0)
find_package(spdlog REQUIRED)

# Try to find Qt5 (optional)
find_package(Qt5 COMPONENTS Core Widgets Network QUIET)
if(Qt5_FOUND)
    set(HAVE_QT5 TRUE)
    message(STATUS "Qt5 found - building Qt support")
else()
    set(HAVE_QT5 FALSE)
    message(WARNING "Qt5 not found - building without Qt support")
endif()

# Link dependencies
if(HAVE_QT5)
    target_link_libraries(asyncop INTERFACE ${GLIB_LIBRARIES} Qt5::Core Qt5::Widgets Qt5::Network spdlog::spdlog)
else()
    target_link_libraries(asyncop INTERFACE ${GLIB_LIBRARIES} spdlog::spdlog)
endif()
target_include_directories(asyncop SYSTEM INTERFACE ${GLIB_INCLUDE_DIRS})
target_compile_options(asyncop INTERFACE ${GLIB_CFLAGS_OTHER})

# Options
option(BUILD_EXAMPLES "Build examples" ON)

# Add tests if enabled
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Add examples if enabled
if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()