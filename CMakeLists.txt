cmake_minimum_required(VERSION 3.20)
project(TriggerLab VERSION 0.3.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(TRIGGERLAB_BUILD_TESTS "Build TriggerLab unit tests" ON)

add_library(triggerlab_core STATIC
    src/TargetDetector.cpp
    src/TriggerEngine.cpp
    src/Simulation.cpp
    src/Config.cpp
    src/Diagnostics.cpp
)
target_include_directories(triggerlab_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(triggerlab_core PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(triggerlab_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(triggerlab_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    target_compile_definitions(triggerlab_core PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    add_executable(TriggerLab WIN32 src/win32/main.cpp)
    target_link_libraries(TriggerLab PRIVATE triggerlab_core user32 gdi32)
    target_compile_definitions(TriggerLab PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    if(MSVC)
        target_compile_options(TriggerLab PRIVATE /W4 /permissive-)
    else()
        target_compile_options(TriggerLab PRIVATE -Wall -Wextra -Wpedantic)
    endif()
else()
    message(STATUS "GUI is Windows-only; portable core/tests are available on this platform.")
endif()

if(TRIGGERLAB_BUILD_TESTS)
    enable_testing()
    add_executable(triggerlab_tests tests/core_tests.cpp)
    target_link_libraries(triggerlab_tests PRIVATE triggerlab_core)
    add_test(NAME triggerlab_tests COMMAND triggerlab_tests)
    add_executable(triggerlab_config_tests tests/config_tests.cpp)
    target_link_libraries(triggerlab_config_tests PRIVATE triggerlab_core)
    add_test(NAME triggerlab_config_tests COMMAND triggerlab_config_tests)
    add_executable(triggerlab_session_tests tests/session_tests.cpp)
    target_link_libraries(triggerlab_session_tests PRIVATE triggerlab_core)
    add_test(NAME triggerlab_session_tests COMMAND triggerlab_session_tests)
    if(WIN32)
        add_test(NAME triggerlab_gui_smoke COMMAND TriggerLab --smoke-test)
        set_tests_properties(triggerlab_gui_smoke PROPERTIES TIMEOUT 15)
    endif()
endif()
