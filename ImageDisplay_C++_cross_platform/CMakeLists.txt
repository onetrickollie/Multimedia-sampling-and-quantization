cmake_minimum_required(VERSION 3.10)     # changed version to 3.10, compatibility issues

project(ImageDisplay)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add source files
set(SOURCES
    src/Main.cpp
)

if(WIN32)
  list(APPEND SOURCES manifest/MyImageApplication.rc)
  set(wxBUILD_SHARED OFF)
  # or you can set it to ON, but you may face some problems about can't find dlls,
  # in this case, you can copy dlls to your exe directory
else()
  set(wxBUILD_SHARED ON)
endif()

# wxWidgets:
# - On macOS (15+) the bundled wxWidgets in dependency/ may fail to compile due to SDK changes.
# - So we use the system (Homebrew) wxWidgets on APPLE/UNIX.
# - On Windows we keep using the bundled dependency so instructor can build without installing wxWidgets.
if(APPLE OR UNIX)
  find_package(wxWidgets REQUIRED COMPONENTS core base)
  include(${wxWidgets_USE_FILE})
else()
  add_subdirectory(dependency/wxWidgets)
endif()

# Create executable
add_executable(MyImageApplication ${SOURCES})

# X11 required on Linux
if(UNIX AND NOT APPLE)
  find_package(X11 REQUIRED)
  target_link_libraries(MyImageApplication ${X11_LIBRARIES})
endif()

# Link libraries
if(APPLE OR UNIX)
  target_link_libraries(MyImageApplication ${wxWidgets_LIBRARIES})
else()
  target_link_libraries(MyImageApplication wx::core wx::base)
endif()
