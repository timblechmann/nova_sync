cmake_minimum_required(VERSION 3.25)

project(nova_sync VERSION 0.1.0 LANGUAGES CXX C)

if (NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 20)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (PROJECT_IS_TOP_LEVEL AND NOT CMAKE_MSVC_DEBUG_INFORMATION_FORMAT)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "Embedded")
endif()

if (PROJECT_IS_TOP_LEVEL)
    add_custom_target(nova_sync_project_files SOURCES
        .clang-tidy
        .gitignore
        .pre-commit-config.yaml
        _clang-format
        Readme.md
        License.txt
        tools/bench_plot.py
        .github/workflows/ci.yml
    )
endif()

set(headers
    include/nova/sync/detail/async_support.hpp
    include/nova/sync/detail/backoff.hpp
    include/nova/sync/detail/compat.hpp
    include/nova/sync/detail/pause.hpp
    include/nova/sync/detail/syscall.hpp
    include/nova/sync/detail/timed_wait.hpp

    include/nova/sync/event/auto_reset_event.hpp
    include/nova/sync/event/concepts.hpp
    include/nova/sync/event/detail/async_support.hpp
    include/nova/sync/event/manual_reset_event.hpp
    include/nova/sync/event/native_auto_reset_event.hpp
    include/nova/sync/event/native_manual_reset_event.hpp
    include/nova/sync/event/support/boost_asio_support.hpp
    include/nova/sync/event/support/libdispatch_support.hpp
    include/nova/sync/event/support/qt_support.hpp
    include/nova/sync/event/timed_auto_reset_event.hpp
    include/nova/sync/event/timed_manual_reset_event.hpp
    include/nova/sync/mutex/tsa_annotations.hpp
    include/nova/sync/mutex/apple_os_unfair_mutex.hpp
    include/nova/sync/mutex/concepts.hpp
    include/nova/sync/mutex/detail/async_support.hpp
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
    include/nova/sync/mutex/support/async_waiter_guard.hpp
    include/nova/sync/mutex/support/boost_asio_support.hpp
    include/nova/sync/mutex/support/libdispatch_support.hpp
    include/nova/sync/mutex/support/qt_support.hpp
    include/nova/sync/mutex/tsa_macros.hpp
    include/nova/sync/mutex/win32_critical_section_mutex.hpp
    include/nova/sync/mutex/win32_event_mutex.hpp
    include/nova/sync/mutex/win32_mutex.hpp
    include/nova/sync/mutex/win32_srw_mutex.hpp
)

set(sources
    source/nova/sync/detail/syscall.cpp
    source/nova/sync/detail/timed_wait.cpp
    source/nova/sync/event/native_auto_reset_event.cpp
    source/nova/sync/event/native_manual_reset_event.cpp
    source/nova/sync/event/timed_auto_reset_event.cpp
    source/nova/sync/event/timed_manual_reset_event.cpp
    source/nova/sync/mutex/eventfd_mutex.cpp
    source/nova/sync/mutex/fair_mutex.cpp
    source/nova/sync/mutex/fast_mutex.cpp
    source/nova/sync/mutex/kqueue_mutex.cpp
    source/nova/sync/mutex/recursive_spinlock_mutex.cpp
    source/nova/sync/mutex/shared_spinlock_mutex.cpp
    source/nova/sync/mutex/spinlock_mutex.cpp
    source/nova/sync/mutex/win32_critical_section_mutex.cpp
    source/nova/sync/mutex/win32_event_mutex.cpp
    source/nova/sync/mutex/win32_mutex.cpp
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

if(WIN32)
    target_compile_definitions(nova_sync PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        _WIN32_WINNT=0x0a00
    )
endif()

# Testing
option(NOVA_SYNC_TESTS "Build unit tests" ${PROJECT_IS_TOP_LEVEL})
include(CMakeDependentOption)
cmake_dependent_option(
    NOVA_SYNC_TESTS_STRESS_TEST
    "Run stress tests via ctest"
    ON
    "NOVA_SYNC_TESTS"
    OFF
)

if(NOVA_SYNC_TESTS)
    function(nova_sync_install_pmr)
        if (NOT COMMAND CPMAddPackage)
            set(CPM_DOWNLOAD_VERSION 0.42.1)
            if(NOT EXISTS "${CMAKE_BINARY_DIR}/cmake/CPM.cmake")
                message(STATUS "Downloading CPM.cmake ${CPM_DOWNLOAD_VERSION}...")
                file(DOWNLOAD
                    "https://github.com/cpm-cmake/CPM.cmake/releases/download/v${CPM_DOWNLOAD_VERSION}/CPM.cmake"
                    "${CMAKE_BINARY_DIR}/cmake/CPM.cmake"
                    STATUS _cpm_download_status
                    TLS_VERIFY ON
                )
                list(GET _cpm_download_status 0 _cpm_download_error)
                if(_cpm_download_error)
                    list(GET _cpm_download_status 1 _cpm_download_message)
                    message(FATAL_ERROR "Failed to download CPM.cmake: ${_cpm_download_message}")
                endif()
            endif()
            include("${CMAKE_BINARY_DIR}/cmake/CPM.cmake")
        endif()
    endfunction()

    set(CMAKE_FOLDER "tests")

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
        list(APPEND CMAKE_MODULE_PATH ${Catch2_SOURCE_DIR}/extras)
    endif()
    include(CTest)
    include(Catch)

    if (NOT TARGET Boost::asio)
        nova_sync_install_pmr()
        CPMAddPackage(
          NAME Boost
          VERSION 1.91.0-1
          URL https://github.com/boostorg/boost/releases/download/boost-1.91.0-1/boost-1.91.0-1-cmake.tar.xz
          URL_HASH SHA256=cc5dc5006ecbdf0051f90979be31b4eee5987d9ae14ae9fb9c03cfa43fa3cdad
          OPTIONS "BOOST_ENABLE_CMAKE ON" "BOOST_SKIP_INSTALL_RULES ON"
                  "BUILD_SHARED_LIBS OFF" "BOOST_INCLUDE_LIBRARIES asio"
        )
        if(TARGET boost_context)
            set_target_properties(boost_context PROPERTIES EXCLUDE_FROM_ALL TRUE)
        endif()

    endif()

    if (NOT TARGET tl::expected)
        nova_sync_install_pmr()
        CPMAddPackage(
            NAME expected
            GITHUB_REPOSITORY TartanLlama/expected
            VERSION 1.3.1
            OPTIONS "EXPECTED_BUILD_TESTS OFF"
        )
    endif()

    if ("CMAKE_CXX_COMPILER_ID MATCHES Clang OR CMAKE_CXX_COMPILER_ID MATCHES AppleClang")
        add_compile_options(nova_sync PRIVATE -Wthread-safety)
    endif()

    add_executable(nova_sync_tests
        include/nova/sync/event/support/boost_asio_support.hpp # to make clangd happy
        include/nova/sync/event/support/libdispatch_support.hpp
        include/nova/sync/event/support/qt_support.hpp
        include/nova/sync/event/detail/async_support.hpp
        include/nova/sync/mutex/support/boost_asio_support.hpp # to make clangd happy
        include/nova/sync/mutex/support/libdispatch_support.hpp
        include/nova/sync/mutex/support/qt_support.hpp
        include/nova/sync/mutex/detail/async_support.hpp

        test/event_types.hpp
        test/mutex_types.hpp
        test/mutex_test.cpp
        test/mutex_benchmarks.cpp
        test/mutex_async_asio_test.cpp
        test/mutex_thread_safety_test.cpp
        test/event_test.cpp
        test/event_benchmarks.cpp
        test/event_async_asio_test.cpp
    )

    if(WIN32)
        target_compile_definitions(nova_sync_tests PRIVATE
            NOMINMAX
            WIN32_LEAN_AND_MEAN
            _WIN32_WINNT=0x0a00
        )
    endif()

    if(APPLE)
        enable_language(OBJCXX)
        target_sources(nova_sync_tests PRIVATE test/mutex_async_libdispatch_test.mm)
        target_sources(nova_sync_tests PRIVATE test/event_async_libdispatch_test.mm)
        if(FALSE)
            target_compile_options(nova_sync_tests PRIVATE
                $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>
            )
            # Ensure Objective-C runtime is linked when building ObjC++ tests under ARC
            target_link_libraries(nova_sync_tests PRIVATE "-lobjc")
        endif()
    endif()

    target_link_libraries(nova_sync_tests PRIVATE nova::sync Catch2::Catch2WithMain)
    if(TARGET Boost::asio_core)
        target_link_libraries(nova_sync_tests PRIVATE Boost::asio_core)
    else()
        target_link_libraries(nova_sync_tests PRIVATE Boost::asio)
    endif()
    target_link_libraries(nova_sync_tests PRIVATE tl::expected)

    if(CMAKE_CXX_COMPILER_ID MATCHES Clang OR CMAKE_CXX_COMPILER_ID MATCHES AppleClang)
        target_compile_options(nova_sync_tests PRIVATE -Wthread-safety)
        set_target_properties(nova_sync_tests PROPERTIES COMPILE_WARNING_AS_ERROR ON)
    endif()

    # for benchmarking QMutex
    find_package(Qt6 COMPONENTS Core QUIET)
    if(TARGET Qt6::Core)
        target_link_libraries(nova_sync_tests PRIVATE Qt6::Core)
        target_compile_definitions(nova_sync_tests PRIVATE NOVA_SYNC_HAS_QT=1)
        target_sources(nova_sync_tests PRIVATE test/mutex_async_qt_test.cpp)
        target_sources(nova_sync_tests PRIVATE test/event_async_qt_test.cpp)
    endif()

    enable_testing()

    if (NOT NOVA_SYNC_TESTS_STRESS_TEST)
        catch_discover_tests(nova_sync_tests
            TEST_FILTER ~[stress]
            PROPERTIES
                TIMEOUT 20
        )
    else()
        catch_discover_tests(nova_sync_tests
            PROPERTIES
                TIMEOUT 50
        )
    endif()

    # ---------------------------------------------------------------------------
    # Negative compile tests — expected NOT to compile.
    # These are excluded from ALL (not built by default) and must be built
    # explicitly, e.g.:  cmake --build build --target nova_sync_negative_tests
    #
    # They are only meaningful on Clang (or compilers supporting -Wthread-safety).
    # GCC/MSVC will silently ignore TSA attributes and compile successfully.
    # ---------------------------------------------------------------------------
    set(tsa_tests
        requires_without_lock
        guarded_by_without_lock
        excludes_while_holding
        pt_guarded_by_without_lock
        acquired_after_wrong_order
        release_without_lock
        shared_lock_write_guarded
        double_lock_non_reentrant
    )
    foreach(_neg_test IN LISTS tsa_tests)
        add_executable(nova_sync_neg_${_neg_test} EXCLUDE_FROM_ALL
            test/negative/${_neg_test}.cpp
        )
        target_link_libraries(nova_sync_neg_${_neg_test} PRIVATE nova::sync)
        target_compile_options(nova_sync_neg_${_neg_test} PRIVATE
            -Wthread-safety -Werror=thread-safety-analysis
        )
    endforeach()

    # Register negative compile tests with CTest for Clang-only platforms.
    # These tests MUST fail to compile (TSA violations). If compilation succeeds,
    # the test fails. WILL_FAIL reverses this: build success → test FAIL, build
    # failure → test PASS.
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        foreach(_neg_test IN LISTS tsa_tests)
            add_test(
                NAME nova_sync_neg_${_neg_test}
                COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}
                        --target nova_sync_neg_${_neg_test}
                        --config $<CONFIGURATION>
            )
            set_tests_properties(nova_sync_neg_${_neg_test} PROPERTIES
                WILL_FAIL TRUE
                LABELS "negative;thread-safety"
            )
        endforeach()
    endif()
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
