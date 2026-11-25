# Copyright 2022-present Contributors to the jobman project.
# SPDX-License-Identifier: BSD-3-Clause

add_executable(jobman_process_child process_child.cpp)
target_link_libraries(jobman_process_child PRIVATE Qt6::Core)

# Exercise the production implementation without constructing the queue or UI.
add_executable(jobman_regressions
    regression.cpp
    "${PROJECT_SOURCE_DIR}/sources/process.cpp"
    "${PROJECT_SOURCE_DIR}/sources/process.h"
)
target_include_directories(jobman_regressions PRIVATE "${PROJECT_SOURCE_DIR}/sources")
target_link_libraries(jobman_regressions PRIVATE Qt6::Core)
add_dependencies(jobman_regressions jobman_process_child)

add_executable(jobman_preset_regressions preset_regression.cpp
    "${PROJECT_SOURCE_DIR}/sources/preset.cpp" "${PROJECT_SOURCE_DIR}/sources/preset.h")
target_include_directories(jobman_preset_regressions PRIVATE "${PROJECT_SOURCE_DIR}/sources")
target_link_libraries(jobman_preset_regressions PRIVATE Qt6::Core)
foreach(case IN ITEMS preset_json preset_structure preset_options preset_dropdown preset_values
                      preset_tasks preset_variables preset_reuse preset_examples)
    add_test(NAME jobman.${case} COMMAND jobman_preset_regressions ${case} "${PROJECT_SOURCE_DIR}")
    set_tests_properties(jobman.${case} PROPERTIES LABELS "native;preset;regression" TIMEOUT 30)
    if(APPLE)
        set_property(TEST jobman.${case} APPEND PROPERTY ENVIRONMENT "DYLD_IMAGE_SUFFIX=$<$<CONFIG:Debug>:_debug>")
    endif()
    if(WIN32)
        set_property(TEST jobman.${case} APPEND PROPERTY ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::qmake>")
    endif()
endforeach()

add_executable(jobman_queue_regressions
    queue_regression.cpp
    "${PROJECT_SOURCE_DIR}/sources/queue.cpp"
    "${PROJECT_SOURCE_DIR}/sources/queue.h"
    "${PROJECT_SOURCE_DIR}/sources/job.cpp"
    "${PROJECT_SOURCE_DIR}/sources/job.h"
    "${PROJECT_SOURCE_DIR}/sources/process.cpp"
    "${PROJECT_SOURCE_DIR}/sources/process.h"
)
target_include_directories(jobman_queue_regressions PRIVATE "${PROJECT_SOURCE_DIR}/sources")
target_link_libraries(jobman_queue_regressions PRIVATE Qt6::Core Qt6::Concurrent Qt6::Widgets)
add_dependencies(jobman_queue_regressions jobman_process_child)

foreach(case IN ITEMS process_stdout process_stderr process_both_streams process_utf8
                      process_working_directory process_concurrent_directories
                      process_launch_failure process_exit_code process_kill)
    add_test(NAME jobman.${case}
        COMMAND jobman_regressions ${case} $<TARGET_FILE:jobman_process_child>)
    set_tests_properties(jobman.${case} PROPERTIES LABELS "native;process;regression" TIMEOUT 30)
    if(APPLE)
        set_property(TEST jobman.${case} APPEND PROPERTY ENVIRONMENT
            "DYLD_IMAGE_SUFFIX=$<$<CONFIG:Debug>:_debug>")
    endif()
    if(WIN32)
        set_property(TEST jobman.${case} APPEND PROPERTY ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::qmake>")
    endif()
endforeach()

foreach(case IN ITEMS job_timestamps queue_signals queue_worker_submission queue_dependencies queue_failure_propagation
                      queue_priority_exclusive queue_batch queue_stop_start queue_restart queue_remove)
    add_test(NAME jobman.${case}
        COMMAND jobman_queue_regressions ${case} $<TARGET_FILE:jobman_process_child>)
    set_tests_properties(jobman.${case} PROPERTIES LABELS "native;queue;regression" TIMEOUT 30)
    if(APPLE)
        set_property(TEST jobman.${case} APPEND PROPERTY ENVIRONMENT
            "DYLD_IMAGE_SUFFIX=$<$<CONFIG:Debug>:_debug>")
    endif()
    if(WIN32)
        set_property(TEST jobman.${case} APPEND PROPERTY ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::qmake>")
    endif()
endforeach()

if(APPLE)
    set(jobman_test_platform "${PROJECT_SOURCE_DIR}/sources/platform_mac.mm")
else()
    set(jobman_test_platform "${PROJECT_SOURCE_DIR}/sources/platform_win.cpp")
endif()
add_executable(jobman_monitor_regressions
    monitor_regression.cpp
    "${PROJECT_SOURCE_DIR}/sources/monitor.cpp" "${PROJECT_SOURCE_DIR}/sources/monitor.h"
    "${PROJECT_SOURCE_DIR}/sources/jobtree.cpp" "${PROJECT_SOURCE_DIR}/sources/jobtree.h"
    "${PROJECT_SOURCE_DIR}/sources/question.cpp" "${PROJECT_SOURCE_DIR}/sources/question.h"
    "${PROJECT_SOURCE_DIR}/sources/queue.cpp" "${PROJECT_SOURCE_DIR}/sources/queue.h"
    "${PROJECT_SOURCE_DIR}/sources/job.cpp" "${PROJECT_SOURCE_DIR}/sources/job.h"
    "${PROJECT_SOURCE_DIR}/sources/process.cpp" "${PROJECT_SOURCE_DIR}/sources/process.h"
    "${PROJECT_SOURCE_DIR}/sources/ui.cpp" "${PROJECT_SOURCE_DIR}/sources/ui.h"
    "${PROJECT_SOURCE_DIR}/sources/jobman.qrc"
    ${jobman_test_platform}
)
target_include_directories(jobman_monitor_regressions PRIVATE "${PROJECT_SOURCE_DIR}/sources")
target_link_libraries(jobman_monitor_regressions PRIVATE "$<TARGET_PROPERTY:${project_name},LINK_LIBRARIES>")
# The Designer custom-widget header paths assume the application's autogen depth.
set_target_properties(jobman_monitor_regressions PROPERTIES
    AUTOGEN_BUILD_DIR "${CMAKE_BINARY_DIR}/jobman_monitor_regressions_autogen")
add_dependencies(jobman_monitor_regressions jobman_process_child)
add_test(NAME jobman.monitor_signals
    COMMAND jobman_monitor_regressions $<TARGET_FILE:jobman_process_child>)
set_tests_properties(jobman.monitor_signals PROPERTIES LABELS "native;queue;gui;regression" TIMEOUT 30
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
if(APPLE)
    set_property(TEST jobman.monitor_signals APPEND PROPERTY ENVIRONMENT
        "DYLD_IMAGE_SUFFIX=$<$<CONFIG:Debug>:_debug>")
endif()
if(WIN32)
    set_property(TEST jobman.monitor_signals APPEND PROPERTY ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::qmake>")
    set_property(TEST jobman.monitor_signals APPEND PROPERTY ENVIRONMENT
        "QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::qmake>/../plugins")
endif()

add_custom_target(jobman_tests)
add_dependencies(jobman_tests jobman_preset_regressions jobman_regressions jobman_queue_regressions jobman_monitor_regressions jobman_process_child)

# Drive the actual file-drop widget and main-window slots with isolated settings.
set(jobman_filedrop_sources)
foreach(source IN LISTS app_sources)
    if(NOT source MATCHES "(^|/)main\\.cpp$")
        if(IS_ABSOLUTE "${source}")
            list(APPEND jobman_filedrop_sources "${source}")
        else()
            list(APPEND jobman_filedrop_sources "${PROJECT_SOURCE_DIR}/${source}")
        endif()
    endif()
endforeach()
add_executable(jobman_filedrop_regressions filedrop_regression.cpp mcp_regression.cpp mcp_regression.h
    theme_regression.cpp theme_regression.h ${jobman_filedrop_sources})
target_include_directories(jobman_filedrop_regressions PRIVATE "${PROJECT_SOURCE_DIR}/sources")
target_link_libraries(jobman_filedrop_regressions PRIVATE "$<TARGET_PROPERTY:${project_name},LINK_LIBRARIES>")
if(MSVC)
    target_compile_options(jobman_filedrop_regressions PRIVATE "/FI${CMAKE_CURRENT_SOURCE_DIR}/testsettings.h")
else()
    target_compile_options(jobman_filedrop_regressions PRIVATE -include "${CMAKE_CURRENT_SOURCE_DIR}/testsettings.h")
endif()
set_target_properties(jobman_filedrop_regressions PROPERTIES
    AUTOGEN_BUILD_DIR "${CMAKE_BINARY_DIR}/jobman_filedrop_regressions_autogen")
if(APPLE)
    set(jobman_gui_test_resources "$<TARGET_FILE_DIR:jobman_filedrop_regressions>/../Resources")
else()
    set(jobman_gui_test_resources "$<TARGET_FILE_DIR:jobman_filedrop_regressions>/Resources")
endif()
add_custom_command(TARGET jobman_filedrop_regressions POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${jobman_gui_test_resources}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PROJECT_SOURCE_DIR}/resources/App.css" "${jobman_gui_test_resources}/App.css"
    VERBATIM)
add_dependencies(jobman_filedrop_regressions jobman_process_child)
add_dependencies(jobman_tests jobman_filedrop_regressions)
foreach(case IN ITEMS filedrop_rejected_recovery filedrop_empty_directory filedrop_mixed filedrop_declined_directory filedrop_all_files mcp mcp_large ui_theme preset_errors_ui preset_execution)
    add_test(NAME jobman.${case}
        COMMAND jobman_filedrop_regressions ${case} $<TARGET_FILE:jobman_process_child>
            "${CMAKE_CURRENT_BINARY_DIR}/${case}.png")
    set_tests_properties(jobman.${case} PROPERTIES LABELS "native;gui;filedrop;regression" TIMEOUT 30
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    if(case MATCHES "^preset_")
        set_property(TEST jobman.${case} PROPERTY LABELS "native;gui;preset;regression")
    endif()
    if(case MATCHES "^mcp")
        set_property(TEST jobman.${case} PROPERTY LABELS "native;gui;mcp;regression")
    endif()
    if(case STREQUAL "mcp_large")
        set_property(TEST jobman.${case} PROPERTY TIMEOUT 120)
    endif()
    if(case STREQUAL "ui_theme")
        set_property(TEST jobman.${case} PROPERTY LABELS "native;gui;theme;regression")
    endif()
    if(APPLE)
        set_property(TEST jobman.${case} APPEND PROPERTY ENVIRONMENT
            "DYLD_IMAGE_SUFFIX=$<$<CONFIG:Debug>:_debug>")
    endif()
    if(WIN32)
        set_property(TEST jobman.${case} APPEND PROPERTY ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::qmake>")
        set_property(TEST jobman.${case} APPEND PROPERTY ENVIRONMENT
            "QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::qmake>/../plugins")
    endif()
endforeach()

find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME jobman.test_app_runner COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/test_run_all.py")
set_tests_properties(jobman.test_app_runner PROPERTIES LABELS "python;tooling" TIMEOUT 30)
