cmake_minimum_required(VERSION 3.10)
project(AsyncOpTests VERSION 2.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find dependencies
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED glib-2.0)

find_package(spdlog REQUIRED)

# Try to find Qt5 (optional)
find_package(Qt5 COMPONENTS Core Widgets QUIET)
if(NOT Qt5_FOUND)
    message(WARNING "Qt5 not found - skipping Qt tests")
endif()

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../include)

# Qt-based test executable - compile test_asyncop.cpp with Qt flag (only if Qt is available)
if(Qt5_FOUND)
    add_executable(test_asyncop_qt testmain_qt.cpp ../tests/test_asyncop.cpp)
    set_target_properties(test_asyncop_qt PROPERTIES COMPILE_DEFINITIONS "ASYNC_USE_QT")
    target_include_directories(test_asyncop_qt PRIVATE ${Qt5Core_INCLUDE_DIRS})
    target_link_libraries(test_asyncop_qt Qt5::Core Qt5::Widgets spdlog::spdlog)
endif()

# GLib-based test executable - compile test_asyncop.cpp without Qt flag
add_executable(test_asyncop_glib testmain_glib.cpp ../tests/test_asyncop.cpp)
target_include_directories(test_asyncop_glib PRIVATE ${GLIB_INCLUDE_DIRS})
target_compile_options(test_asyncop_glib PRIVATE ${GLIB_CFLAGS_OTHER})
target_link_libraries(test_asyncop_glib ${GLIB_LIBRARIES} spdlog::spdlog)

# Enable testing
enable_testing()

if(Qt5_FOUND)
    add_test(NAME test_asyncop_qt COMMAND test_asyncop_qt)
endif()
add_test(NAME test_asyncop_glib COMMAND test_asyncop_glib)