cmake_minimum_required(VERSION 3.25)

project(nova_sync VERSION 0.1.0 LANGUAGES CXX C)

if (NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 20)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (PROJECT_IS_TOP_LEVEL AND NOT CMAKE_MSVC_DEBUG_INFORMATION_FORMAT)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "Embedded")
endif()

add_custom_target(nova_sync_project_files SOURCES
    .clang-tidy
    .pre-commit-config.yaml
    _clang-format
    Readme.md
    License.txt
    tools/bench_plot.py
    .github/workflows/ci.yml
)

function(nova_sync_install_pmr)
    if (NOT COMMAND CPMAddPackage)
        set(CPM_DOWNLOAD_VERSION 0.42.1)
        if(NOT EXISTS "${CMAKE_BINARY_DIR}/cmake/CPM.cmake")
        message(STATUS "Downloading CPM.cmake ${CPM_DOWNLOAD_VERSION}...")
        file(DOWNLOAD
            "https://github.com/cpm-cmake/CPM.cmake/releases/download/v${CPM_DOWNLOAD_VERSION}/CPM.cmake"
            "${CMAKE_BINARY_DIR}/cmake/CPM.cmake"
        )
        endif()
        include("${CMAKE_BINARY_DIR}/cmake/CPM.cmake")
    endif()
endfunction()

set(headers
    include/nova/sync/detail/backoff.hpp
    include/nova/sync/detail/compat.hpp
    include/nova/sync/detail/pause.hpp
    include/nova/sync/mutex/apple_os_unfair_mutex.hpp
    include/nova/sync/detail/syscall.hpp
    include/nova/sync/mutex/concepts.hpp
    include/nova/sync/mutex/eventfd_mutex.hpp
    include/nova/sync/mutex/fair_mutex.hpp
    include/nova/sync/mutex/fast_mutex.hpp
    include/nova/sync/mutex/kqueue_mutex.hpp
    include/nova/sync/mutex/native_async_mutex.hpp
    include/nova/sync/mutex/pthread_rt_mutex.hpp
    include/nova/sync/mutex/pthread_spinlock_mutex.hpp
    include/nova/sync/mutex/recursive_spinlock_mutex.hpp
    include/nova/sync/mutex/shared_spinlock_mutex.hpp
    include/nova/sync/mutex/spinlock_mutex.hpp
    include/nova/sync/mutex/win32_mutex.hpp
    include/nova/sync/mutex/win32_recursive_mutex.hpp
    include/nova/sync/mutex/win32_srw_mutex.hpp
)

set(sources
    source/nova/sync/mutex/eventfd_mutex.cpp
    source/nova/sync/mutex/fair_mutex.cpp
    source/nova/sync/mutex/fast_mutex.cpp
    source/nova/sync/mutex/kqueue_mutex.cpp
    source/nova/sync/mutex/recursive_spinlock_mutex.cpp
    source/nova/sync/mutex/shared_spinlock_mutex.cpp
    source/nova/sync/mutex/spinlock_mutex.cpp

    source/nova/sync/mutex/win32_mutex.cpp
    source/nova/sync/mutex/win32_recursive_mutex.cpp
    source/nova/sync/mutex/win32_srw_mutex.cpp
)

# Project layout
add_library(nova_sync STATIC
    ${headers}
    ${sources}
)
add_library(nova::sync ALIAS nova_sync)

target_sources(nova_sync PUBLIC FILE_SET HEADERS BASE_DIRS include)
target_sources(nova_sync PUBLIC FILE_SET HEADERS FILES
    ${headers}
)

target_include_directories(nova_sync PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Testing
option(NOVA_SYNC_TESTS "Build unit tests" ${PROJECT_IS_TOP_LEVEL})
if(NOVA_SYNC_TESTS)
    if (NOT TARGET Catch2::Catch2WithMain)
        nova_sync_install_pmr()
        block()
            set(CMAKE_UNITY_BUILD ON)
            CPMAddPackage(
                NAME Catch2
                GITHUB_REPOSITORY catchorg/Catch2
                VERSION 3.14.0
            )
        endblock()
    endif()

    if (NOT TARGET Boost::asio)
        nova_sync_install_pmr()
        CPMAddPackage(
          NAME Boost
          VERSION 1.86.0
          URL https://github.com/boostorg/boost/releases/download/boost-1.86.0/boost-1.86.0-cmake.tar.xz
          URL_HASH SHA256=2c5ec5edcdff47ff55e27ed9560b0a0b94b07bd07ed9928b476150e16b0efc57
          OPTIONS "BOOST_ENABLE_CMAKE ON" "BOOST_SKIP_INSTALL_RULES ON"
                  "BUILD_SHARED_LIBS OFF" "BOOST_INCLUDE_LIBRARIES asio"
        )
    endif()


    add_executable(nova_sync_tests
        test/mutex_types.hpp
        test/mutex_test.cpp
        test/mutex_benchmarks.cpp
        test/mutex_async_test.cpp
    )
    target_link_libraries(nova_sync_tests PRIVATE nova::sync Catch2::Catch2WithMain)
    target_link_libraries(nova_sync_tests PRIVATE Boost::asio)

    if(APPLE)
        enable_language(OBJCXX)
        set_source_files_properties(test/mutex_async_test.cpp PROPERTIES LANGUAGE OBJCXX)
    endif()

    # for benchmarking QMutex
    find_package(Qt6 COMPONENTS Core)
    if(TARGET Qt6::Core)
        target_link_libraries(nova_sync_tests PRIVATE Qt6::Core)
        target_compile_definitions(nova_sync_tests PRIVATE NOVA_SYNC_HAS_QT=1)
    endif()

    enable_testing()
    add_test(NAME nova_sync_tests COMMAND nova_sync_tests)
endif()

# Installation
include(GNUInstallDirs)

install(TARGETS nova_sync
    EXPORT nova_syncTargets
    FILE_SET HEADERS DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT nova_syncTargets
    FILE nova_syncTargets.cmake
    NAMESPACE nova::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/nova_sync
)
