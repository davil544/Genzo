# CMakeList.txt : CMake project for Genzo, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.10)

# Enable Hot Reload for MSVC compilers if supported.
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

project ("Genzo")

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg.json" VCPKG_JSON_DATA)
string(JSON PROJECT_VERSION_FROM_VCPKG GET "${VCPKG_JSON_DATA}" "version-string")

if(APPLE)
    message(STATUS "Configuring Genzo as a macOS .app bundle")

    add_executable(Genzo MACOSX_BUNDLE
        "App.cpp" "App.h"
        "MainFrame.cpp" "MainFrame.h"
        "Codec.cpp" "Codec.h"
        "ResponsiveImageFrame.cpp" "ResponsiveImageFrame.h"
    )

    set_source_files_properties(${CMAKE_SOURCE_DIR}/Genzo.icns
        PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")

    target_sources(Genzo PRIVATE ${CMAKE_SOURCE_DIR}/Genzo.icns)
    set_target_properties(Genzo PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.dylanspcrepairs.genzo"
    MACOSX_BUNDLE_BUNDLE_NAME "Genzo"
    MACOSX_BUNDLE_INFO_STRING "Genzo Image Converter"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION_FROM_VCPKG}"
    MACOSX_BUNDLE_LONG_VERSION_STRING "${PROJECT_VERSION_FROM_VCPKG}"
    MACOSX_BUNDLE_COPYRIGHT "Copyright © 2026 Dylan Aviles, All Rights Reserved"
)

else()
    add_executable(Genzo WIN32
        "App.cpp" "App.h"
        "MainFrame.cpp" "MainFrame.h"
        "Codec.cpp" "Codec.h"
        "app_resources.rc"
        "ResponsiveImageFrame.cpp" "ResponsiveImageFrame.h"
    )
endif()

if(APPLE AND GENZO_BUILD_UNIVERSAL)
    string(TOLOWER "${CMAKE_BUILD_TYPE}" _bt)
    set(_x64_preset macos-x64-${_bt})
    set(_x64_dir ${CMAKE_SOURCE_DIR}/out/build/${_x64_preset})
    set(_x64_exe ${_x64_dir}/Genzo.app/Contents/MacOS/Genzo)

    add_custom_target(genzo_x64 ALL
        COMMENT "Building x86_64 slice with preset: ${_x64_preset}"
        COMMAND ${CMAKE_COMMAND} --preset ${_x64_preset}
        COMMAND ${CMAKE_COMMAND} --build ${_x64_dir} --target Genzo
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        VERBATIM
    )

    add_custom_target(genzo_universal ALL
        COMMENT "Creating universal Genzo.app"
        COMMAND ${CMAKE_COMMAND} -E echo "Merging arm64 + x86_64 slices"
        COMMAND ${CMAKE_COMMAND} -E echo "arm64 slice: $<TARGET_FILE:Genzo>"
        COMMAND ${CMAKE_COMMAND} -E echo "x86_64 slice: ${_x64_exe}"
        COMMAND lipo -create -output $<TARGET_FILE:Genzo>.universal ${_x64_exe} $<TARGET_FILE:Genzo>
        COMMAND ${CMAKE_COMMAND} -E echo "Replacing original binary with universal binary"
        COMMAND ${CMAKE_COMMAND} -E rename $<TARGET_FILE:Genzo>.universal $<TARGET_FILE:Genzo>
        COMMAND ${CMAKE_COMMAND} -E echo "Codesigning final Genzo.app bundle"
        COMMAND codesign --force --deep --sign - $<TARGET_BUNDLE_DIR:Genzo>
        COMMAND ${CMAKE_COMMAND} -E echo "Universal binary build complete"
        DEPENDS Genzo genzo_x64
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        VERBATIM
    )
endif()

add_custom_command(
     TARGET Genzo POST_BUILD
     COMMAND ${CMAKE_COMMAND} -E copy_directory
         "${CMAKE_CURRENT_SOURCE_DIR}/third-party-licenses"
         "$<TARGET_FILE_DIR:Genzo>/third-party-licenses"
     COMMENT "Copying third party license folder to executable build directory..."
)

target_compile_definitions(Genzo PRIVATE "APP_VERSION=\"${PROJECT_VERSION_FROM_VCPKG}\"")

# Path to wxWidgets for compiling on Windows, set by user environment variable WXWIN
if(WIN32)
    set(wxWidgets_ROOT_DIR "$ENV{WXWIN}")
    set(wxWidgets_LIB_DIR "$ENV{WXWIN}/lib/vc_x64_lib")
endif()

find_package(wxWidgets REQUIRED COMPONENTS core base)
include(${wxWidgets_USE_FILE})
target_link_libraries(Genzo PRIVATE ${wxWidgets_LIBRARIES})

find_package(aom CONFIG QUIET)
if(aom_FOUND)
    if(TARGET aom AND NOT TARGET AOM::aom)
        add_library(AOM::aom INTERFACE IMPORTED)
        set_target_properties(AOM::aom PROPERTIES INTERFACE_LINK_LIBRARIES aom)
    elseif(TARGET aom::aom AND NOT TARGET AOM::aom)
        add_library(AOM::aom INTERFACE IMPORTED)
        set_target_properties(AOM::aom PROPERTIES INTERFACE_LINK_LIBRARIES aom::aom)
    endif()
else()
    message(STATUS "aom CONFIG not found. Falling back to pkg-config...")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(AOM_PKG REQUIRED IMPORTED_TARGET aom)
    
    if(TARGET PkgConfig::AOM_PKG AND NOT TARGET AOM::aom)
        add_library(AOM::aom INTERFACE IMPORTED)
        set_target_properties(AOM::aom PROPERTIES INTERFACE_LINK_LIBRARIES PkgConfig::AOM_PKG)
    endif()
endif()

find_package(libheif CONFIG REQUIRED)
target_link_libraries(Genzo PRIVATE heif)

find_package(JPEG REQUIRED)
target_link_libraries(Genzo PRIVATE JPEG::JPEG)

find_package(WebP CONFIG REQUIRED)
target_link_libraries(Genzo PRIVATE WebP::webp WebP::webpdecoder WebP::webpdemux)

find_package(OpenJPEG CONFIG REQUIRED)
target_link_libraries(Genzo PRIVATE openjp2)

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET Genzo PROPERTY CXX_STANDARD 20)
endif()