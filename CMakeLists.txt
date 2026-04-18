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


# Project layout
add_library(nova_sync STATIC
    include/nova/sync/fair_mutex.hpp
    include/nova/sync/fast_mutex.hpp
    include/nova/sync/recursive_spinlock_mutex.hpp
    include/nova/sync/shared_spinlock_mutex.hpp
    include/nova/sync/spinlock_mutex.hpp
    include/nova/sync/detail/backoff.hpp
    include/nova/sync/detail/compat.hpp
    include/nova/sync/detail/pause.hpp
    source/nova/sync/fair_mutex.cpp
    source/nova/sync/fast_mutex.cpp
    source/nova/sync/recursive_spinlock_mutex.cpp
    source/nova/sync/shared_spinlock_mutex.cpp
    source/nova/sync/spinlock_mutex.cpp
)
add_library(nova::sync ALIAS nova_sync)

target_sources(nova_sync PUBLIC FILE_SET HEADERS BASE_DIRS include)
target_sources(nova_sync PUBLIC FILE_SET HEADERS FILES
    include/nova/sync/fair_mutex.hpp
    include/nova/sync/fast_mutex.hpp
    include/nova/sync/recursive_spinlock_mutex.hpp
    include/nova/sync/shared_spinlock_mutex.hpp
    include/nova/sync/spinlock_mutex.hpp
    include/nova/sync/detail/backoff.hpp
    include/nova/sync/detail/compat.hpp
    include/nova/sync/detail/pause.hpp
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

    find_package(Qt6 COMPONENTS Core)

    add_executable(nova_sync_tests
        test/mutex_test.cpp
        test/mutex_benchmarks.cpp
    )
    target_link_libraries(nova_sync_tests PRIVATE nova::sync Catch2::Catch2WithMain)

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
