cmake_minimum_required(VERSION 3.21)
set(CMAKE_WARN_DEPRECATED OFF CACHE BOOL "" FORCE)
project(promptty VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Options (default ON when this is the top-level project)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(_promptty_toplevel ON)
else()
  set(_promptty_toplevel OFF)
endif()
option(PROMPTTY_BUILD_EXAMPLES "Build examples" ${_promptty_toplevel})

# --- Warning flags ---
set(PROMPTTY_WARNINGS
  -Wall -Wextra -Wpedantic -Wconversion -Wshadow
  -Wdouble-promotion -Wformat=2 -Wnull-dereference
)

# --- Debug sanitizers ---
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

# --- Release LTO ---
if(CMAKE_BUILD_TYPE STREQUAL "Release")
  set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
endif()

# --- Library target ---
add_library(promptty STATIC src/promptty.cpp)
add_library(promptty::promptty ALIAS promptty)

target_include_directories(promptty
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(promptty PUBLIC cxx_std_23)
target_compile_options(promptty PRIVATE ${PROMPTTY_WARNINGS})

if(CMAKE_BUILD_TYPE STREQUAL "Release")
  target_compile_options(promptty PRIVATE -march=native)
endif()

# --- Install ---
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS promptty
  EXPORT prompttyTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY include/promptty DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT prompttyTargets
  NAMESPACE promptty::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/promptty
)

configure_package_config_file(
  cmake/prompttyConfig.cmake.in
  "${CMAKE_CURRENT_BINARY_DIR}/prompttyConfig.cmake"
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/promptty
)

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/prompttyConfigVersion.cmake"
  COMPATIBILITY SameMajorVersion
)

install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/prompttyConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/prompttyConfigVersion.cmake"
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/promptty
)

# --- Subdirectories ---
if(PROMPTTY_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()
