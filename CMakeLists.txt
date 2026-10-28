cmake_minimum_required(VERSION 3.20)
project(TriggerLab VERSION 0.4.4 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(TRIGGERLAB_BUILD_TESTS "Build TriggerLab tests" ON)
option(TRIGGERLAB_STATIC_RUNTIME "Bundle MSVC runtime into executables" ON)
option(TRIGGERLAB_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
if(MSVC AND TRIGGERLAB_STATIC_RUNTIME)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
include(cmake/ProjectOptions.cmake)

configure_file(include/triggerlab/Version.hpp.in generated/triggerlab/Version.hpp @ONLY)

add_library(triggerlab_core STATIC
    src/TargetDetector.cpp
    src/TriggerEngine.cpp
    src/Simulation.cpp
    src/Diagnostics.cpp
    src/ColorMask.cpp
    src/ImageColor.cpp
    src/AnalyzerSettings.cpp
    src/persistence/AtomicFile.cpp
    src/persistence/Config.cpp
    src/persistence/ColorProfile.cpp
    src/persistence/AnalyzerSettingsFile.cpp
    src/persistence/TrainingProfile.cpp
)
target_include_directories(triggerlab_core
    PUBLIC include "${CMAKE_CURRENT_BINARY_DIR}/generated"
    PRIVATE src
)
triggerlab_configure_target(triggerlab_core)

if(WIN32)
    add_library(triggerlab_capture STATIC src/win32/ScreenCapture.cpp src/win32/ImageFile.cpp)
    target_link_libraries(triggerlab_capture PUBLIC triggerlab_core PRIVATE user32 gdi32 windowscodecs ole32 dwmapi)
    triggerlab_configure_target(triggerlab_capture)

    set(preview_sources
        src/win32/preview/PreviewWindow.cpp
        src/win32/preview/PreviewSettings.cpp
        src/win32/preview/PreviewLayout.cpp
        src/win32/preview/PreviewFeedback.cpp
        src/win32/preview/PreviewCapture.cpp
        src/win32/preview/PreviewInput.cpp
        src/win32/preview/PreviewControls.cpp
        src/win32/preview/PreviewHomePaint.cpp
        src/win32/preview/PreviewNarrowPaint.cpp
        src/win32/preview/PreviewPaint.cpp
        src/win32/ImagePaletteDialog.cpp
        src/win32/WindowPickerDialog.cpp
    )
    function(triggerlab_add_preview_library target)
        add_library(${target} STATIC ${preview_sources})
        target_include_directories(${target} PUBLIC src/win32)
        target_link_libraries(${target} PUBLIC triggerlab_capture PRIVATE user32 gdi32 comdlg32 msimg32 dwmapi comctl32 uxtheme)
        triggerlab_configure_target(${target})
    endfunction()
    triggerlab_add_preview_library(triggerlab_preview)
    add_executable(TriggerLab WIN32 src/win32/preview_main.cpp)
    target_link_libraries(TriggerLab PRIVATE triggerlab_preview)
    triggerlab_configure_target(TriggerLab)

    add_library(triggerlab_trainer STATIC
        src/win32/trainer/TrainerWindow.cpp
        src/win32/trainer/TrainerControls.cpp
        src/win32/trainer/TrainerSession.cpp
        src/win32/trainer/TrainerPaint.cpp
    )
    target_include_directories(triggerlab_trainer PUBLIC src/win32)
    target_link_libraries(triggerlab_trainer PUBLIC triggerlab_core PRIVATE user32 gdi32)
    triggerlab_configure_target(triggerlab_trainer)
    add_executable(TriggerLabTrainer WIN32 src/win32/trainer_main.cpp)
    target_link_libraries(TriggerLabTrainer PRIVATE triggerlab_trainer)
    triggerlab_configure_target(TriggerLabTrainer)
else()
    message(STATUS "GUI is Windows-only; portable core/tests are available on this platform.")
endif()

if(TRIGGERLAB_BUILD_TESTS)
    enable_testing()
    find_package(Threads REQUIRED)
    function(triggerlab_add_test target source library)
        add_executable(${target} ${source})
        target_link_libraries(${target} PRIVATE ${library})
        triggerlab_configure_target(${target})
        add_test(NAME ${target} COMMAND ${target})
    endfunction()

    triggerlab_add_test(triggerlab_color_scene_tests tests/color_scene_tests.cpp triggerlab_core)
    triggerlab_add_test(triggerlab_training_profile_tests tests/training_profile_tests.cpp triggerlab_core)
    triggerlab_add_test(triggerlab_analyzer_tests tests/analyzer_tests.cpp triggerlab_core)
    triggerlab_add_test(triggerlab_image_color_tests tests/image_color_tests.cpp triggerlab_core)
    triggerlab_add_test(triggerlab_tests tests/core_tests.cpp triggerlab_core)
    triggerlab_add_test(triggerlab_config_tests tests/config_tests.cpp triggerlab_core)
    triggerlab_add_test(triggerlab_session_tests tests/session_tests.cpp triggerlab_core)
    triggerlab_add_test(triggerlab_color_click_tests tests/color_click_tests.cpp triggerlab_core)
    triggerlab_add_test(triggerlab_color_tests tests/color_mask_tests.cpp triggerlab_core)
    triggerlab_add_test(triggerlab_region_tests tests/screen_capture_tests.cpp triggerlab_core)
    triggerlab_add_test(triggerlab_input_authorization_tests tests/input_authorization_tests.cpp triggerlab_core)
    triggerlab_add_test(triggerlab_atomic_writer_tests tests/atomic_writer_tests.cpp triggerlab_core)
    triggerlab_add_test(triggerlab_numeric_object_tests tests/numeric_object_tests.cpp triggerlab_core)
    target_link_libraries(triggerlab_atomic_writer_tests PRIVATE Threads::Threads)
    target_include_directories(triggerlab_numeric_object_tests PRIVATE src)

    if(WIN32)
        # Test hooks and native dialog probes are compiled only into this variant.
        triggerlab_add_preview_library(triggerlab_preview_test_support)
        target_compile_definitions(triggerlab_preview_test_support PUBLIC TRIGGERLAB_UI_TESTS)
        add_executable(TriggerLabUiTests WIN32
            tests/win32/PreviewTestMain.cpp
            tests/win32/PreviewTestSupport.cpp
            tests/win32/PreviewTests.cpp
            tests/win32/PreviewIntegrationTests.cpp
        )
        target_link_libraries(TriggerLabUiTests PRIVATE triggerlab_preview_test_support)
        triggerlab_configure_target(TriggerLabUiTests)

        add_test(NAME triggerlab_input_recovery COMMAND TriggerLabUiTests --input-recovery-test)
        set_tests_properties(triggerlab_input_recovery PROPERTIES TIMEOUT 10 RUN_SERIAL TRUE)
        add_test(NAME triggerlab_target_sequence COMMAND TriggerLabUiTests --target-sequence-test)
        set_tests_properties(triggerlab_target_sequence PROPERTIES TIMEOUT 20 RUN_SERIAL TRUE SKIP_RETURN_CODE 77)
        add_test(NAME triggerlab_help_popup COMMAND TriggerLabUiTests --help-popup-test)
        set_tests_properties(triggerlab_help_popup PROPERTIES TIMEOUT 10 RUN_SERIAL TRUE SKIP_RETURN_CODE 77)
        add_test(NAME triggerlab_color_preview_smoke COMMAND TriggerLabUiTests --smoke-test)
        set_tests_properties(triggerlab_color_preview_smoke PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
        add_test(NAME triggerlab_first_run COMMAND TriggerLabUiTests --first-run-test)
        set_tests_properties(triggerlab_first_run PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE)
        add_test(NAME triggerlab_window_picker COMMAND TriggerLabUiTests --window-picker-test)
        set_tests_properties(triggerlab_window_picker PROPERTIES TIMEOUT 20 RUN_SERIAL TRUE)
        add_test(NAME triggerlab_reference_capture COMMAND TriggerLabUiTests --reference-test)
        set_tests_properties(triggerlab_reference_capture PROPERTIES TIMEOUT 20 RUN_SERIAL TRUE SKIP_RETURN_CODE 77)
        add_test(NAME triggerlab_guided_input COMMAND TriggerLabUiTests --guided-click-test)
        set_tests_properties(triggerlab_guided_input PROPERTIES TIMEOUT 15 RUN_SERIAL TRUE SKIP_RETURN_CODE 77)
        add_test(NAME triggerlab_hover_input COMMAND TriggerLabUiTests --click-test)
        set_tests_properties(triggerlab_hover_input PROPERTIES TIMEOUT 15 RUN_SERIAL TRUE SKIP_RETURN_CODE 77)
        add_test(NAME triggerlab_center_input COMMAND TriggerLabUiTests --center-click-test)
        set_tests_properties(triggerlab_center_input PROPERTIES TIMEOUT 15 RUN_SERIAL TRUE SKIP_RETURN_CODE 77)
        add_test(NAME triggerlab_pulse_input COMMAND TriggerLabUiTests --pulse-test)
        set_tests_properties(triggerlab_pulse_input PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE SKIP_RETURN_CODE 77)
        add_executable(TriggerLabTrainerTests WIN32 tests/win32/TrainerTestMain.cpp)
        target_link_libraries(TriggerLabTrainerTests PRIVATE triggerlab_trainer)
        triggerlab_configure_target(TriggerLabTrainerTests)
        add_test(NAME triggerlab_gui_smoke COMMAND TriggerLabTrainerTests)
        set_tests_properties(triggerlab_gui_smoke PROPERTIES TIMEOUT 15)

        triggerlab_add_test(triggerlab_click_pulse_tests tests/win32_click_pulse_tests.cpp triggerlab_core)
        target_link_libraries(triggerlab_click_pulse_tests PRIVATE user32)
        set_tests_properties(triggerlab_click_pulse_tests PROPERTIES TIMEOUT 15)

        triggerlab_add_test(triggerlab_image_file_tests tests/win32_image_file_tests.cpp triggerlab_capture)
        target_link_libraries(triggerlab_image_file_tests PRIVATE windowscodecs ole32)
        add_executable(TriggerLabImageCheck tests/win32_image_check.cpp)
        target_link_libraries(TriggerLabImageCheck PRIVATE triggerlab_capture)
        triggerlab_configure_target(TriggerLabImageCheck)
        add_test(NAME triggerlab_image_check_tests COMMAND TriggerLabImageCheck --self-test)

        triggerlab_add_test(triggerlab_capture_tests tests/win32_capture_tests.cpp triggerlab_capture)
        target_link_libraries(triggerlab_capture_tests PRIVATE user32 gdi32 dwmapi)
        set_tests_properties(triggerlab_capture_tests PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 30)
    endif()
endif()
