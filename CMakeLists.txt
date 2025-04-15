# Copyright 2022-present Contributors to the jobman project.
# SPDX-License-Identifier: BSD-3-Clause
# https://github.com/mikaelsundell/jobman

cmake_minimum_required (VERSION 3.27)
set (project_name "Jobman")

project (${project_name})

# packages
set (qt6_modules Core Concurrent Gui Widgets)
find_package (Qt6 COMPONENTS ${qt6_modules} CONFIG REQUIRED)
set (CMAKE_AUTOMOC ON)
set (CMAKE_AUTORCC ON)
set (CMAKE_AUTOUIC ON)
set (CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package (Lcms2 REQUIRED)

file (GLOB app_sources
    "sources/*.h"
    "sources/*.cpp"
    "sources/*.qrc"
    "sources/*.ui"
)
list (FILTER app_sources EXCLUDE REGEX "_")

# resources
file (GLOB app_resources
    "resources/*.icc"
    "resources/*.css"
    "resources/App*.png"
)

# presets
file (GLOB app_presets
    "presets/*.json" 
)

# bundle
set (bundle_sources
    "${CMAKE_SOURCE_DIR}/resources/MacOSXBundle.plist.in"
)

# source groups
source_group ("Resource Files" FILES ${app_resources})
source_group ("Preset Files" FILES ${app_presets})

# app program
set (app_name ${project_name})
set (app_copyright "Copyright 2022-present Contributors to the ${app_name} project")
set (app_identifier "com.github.mikaelsundell.jobman")
set (app_long_version "1.3.13")
set (app_short_version "1.3")
set (app_url "https://github.com/mikaelsundell/jobman")

add_definitions (-DAPP_NAME="${app_name}")
add_definitions (-DAPP_COPYRIGHT="${app_copyright}")
add_definitions (-DAPP_IDENTIFIER="${app_identifier}")
add_definitions (-DAPP_VERSION_STRING="${app_long_version}")
add_definitions (-DGITHUBURL="${app_url}")

if (APPLE)
    list (APPEND app_sources "sources/platform_mac.mm")
    list (APPEND app_resources "resources/AppIcon.icns")
    set (MACOSX_BUNDLE_GUI_IDENTIFIER ${app_identifier})
    set (MACOSX_BUNDLE_EXECUTABLE_NAME ${app_name})
    set (MACOSX_BUNDLE_INFO_STRING ${app_name})
    set (MACOSX_BUNDLE_BUNDLE_NAME ${app_name})
    set (MACOSX_BUNDLE_ICON_FILE AppIcon.icns)
    set (MACOSX_BUNDLE_LONG_VERSION_STRING ${app_long_version})
    set (MACOSX_BUNDLE_SHORT_VERSION_STRING ${app_short_version})
    set (MACOSX_BUNDLE_BUNDLE_VERSION ${app_long_version})
    set (MACOSX_BUNDLE_COPYRIGHT  ${app_copyright})
    set (MACOSX_DEPLOYMENT_TARGET ${CMAKE_OSX_DEPLOYMENT_TARGET})
    set (CMAKE_XCODE_ATTRIBUTE_OTHER_CODE_SIGN_FLAGS "-o linker-signed")
    set_source_files_properties (${app_resources} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    set_source_files_properties (${app_presets} PROPERTIES MACOSX_PACKAGE_LOCATION "Presets")
    add_executable (${project_name} MACOSX_BUNDLE ${app_sources} ${app_resources} ${app_presets})
    # definitions
    set_target_properties (${project_name} PROPERTIES
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${app_identifier}"
        XCODE_ATTRIBUTE_MACOSX_DEPLOYMENT_TARGET ${CMAKE_OSX_DEPLOYMENT_TARGET}
        MACOSX_BUNDLE_INFO_PLIST ${bundle_sources}
    )
    if (NOT PROVISIONING_PROFILE STREQUAL "")
        set_target_properties(${project_name} PROPERTIES
            XCODE_ATTRIBUTE_PROVISIONING_PROFILE_SPECIFIER "${PROVISIONING_PROFILE}"
        )
    endif ()
    set_target_properties (${project_name} PROPERTIES
        OUTPUT_NAME ${project_name}
    )
    target_compile_options (${project_name} PRIVATE -Wno-deprecated-register)
    target_include_directories (${project_name} PRIVATE ${LCMS2_INCLUDE_DIR})
    target_link_libraries (${project_name} 
        Qt6::Core Qt6::Concurrent Qt6::Gui Qt6::Widgets
        ${LCMS2_LIBRARY}
        "-framework CoreFoundation"
        "-framework AppKit")
elseif (WIN32)
    list (APPEND app_sources "${CMAKE_SOURCE_DIR}/sources/platform_win.cpp")
    list (APPEND app_resources "${CMAKE_SOURCE_DIR}/resources/AppIcon.ico")
    add_executable (${project_name} WIN32 ${app_sources} ${app_resources} ${app_presets})
    add_definitions (-DWIN32 -DNOMINMAX -DWIN32_LEAN_AND_MEAN -DWINVER=0x0A00)
    target_include_directories (${project_name} PRIVATE ${LCMS2_INCLUDE_DIR})
    target_link_libraries (${project_name} 
        Qt6::Core Qt6::Concurrent Qt6::Gui Qt6::Widgets
        ${LCMS2_LIBRARY}
        "User32.lib"
        "Gdi32.lib"
        "Shell32.lib"
    )
    set (app_icon "${CMAKE_SOURCE_DIR}/resources/resources.rc")
    target_sources (${project_name} PRIVATE ${app_icon})

    set (app_debug ${CMAKE_BINARY_DIR}/Debug)
    set (app_release ${CMAKE_BINARY_DIR}/Release)
    foreach (resource_file ${app_resources})
        get_filename_component (resource_name ${resource_file} NAME)
        add_custom_command (TARGET ${project_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                $<$<CONFIG:Debug>:${app_release}/Resources>
                $<$<CONFIG:Release>:${app_release}/Resources>
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${resource_file}
                $<$<CONFIG:Debug>:${app_debug}/Resources/${resource_name}>
                $<$<CONFIG:Release>:${app_release}/Resources/${resource_name}>
        )
    endforeach ()

    foreach (preset_file ${app_presets})
        get_filename_component (preset_name ${preset_file} NAME)
        add_custom_command (TARGET ${project_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                $<$<CONFIG:Debug>:${app_debug}/Presets>
                $<$<CONFIG:Release>:${app_release}/Presets>
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${preset_file}
                $<$<CONFIG:Debug>:${app_debug}/Presets/${preset_name}>
                $<$<CONFIG:Release>:${app_release}/Presets/${preset_name}>
        )
    endforeach ()
else ()
    message (WARNING "${project_name} is a Mac or Windows program, will not be built.")
endif ()
