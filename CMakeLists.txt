# CMakeList.txt : CMake project for Genzo, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.8)

# Enable Hot Reload for MSVC compilers if supported.
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

project ("Genzo")

# Add source to this project's executable.
add_executable (Genzo WIN32 "App.cpp" "App.h" "MainFrame.cpp" "MainFrame.h" "Codec.cpp" "Codec.h")

# Path to wxWidgets for compiling on Windows, set by user environment variable WXWIN
set(wxWidgets_ROOT_DIR "$ENV{WXWIN}")
set(wxWidgets_LIB_DIR "$ENV{WXWIN}/lib/vc_x64_lib")

find_package(wxWidgets REQUIRED COMPONENTS core base)
include(${wxWidgets_USE_FILE})
target_link_libraries(Genzo PRIVATE ${wxWidgets_LIBRARIES})

find_package(libheif CONFIG REQUIRED)
target_link_libraries(Genzo PRIVATE heif)

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET Genzo PROPERTY CXX_STANDARD 20)
endif()