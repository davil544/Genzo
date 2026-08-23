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

add_executable (Genzo WIN32 "App.cpp" "App.h" "MainFrame.cpp" "MainFrame.h" "Codec.cpp" "Codec.h" app_resources.rc "ResponsiveImageFrame.cpp" "ResponsiveImageFrame.h")

add_custom_command(
     TARGET Genzo POST_BUILD
     COMMAND ${CMAKE_COMMAND} -E copy_directory
         "${CMAKE_CURRENT_SOURCE_DIR}/third-party-licenses"
         "$<TARGET_FILE_DIR:Genzo>/third-party-licenses"
     COMMENT "Copying third party license folder to executable build directory..."
)

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

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET Genzo PROPERTY CXX_STANDARD 20)
endif()