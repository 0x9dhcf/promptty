cmake_minimum_required(VERSION 3.21)
set(CMAKE_WARN_DEPRECATED OFF CACHE BOOL "" FORCE)
project(promptty VERSION 0.2.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Options (default ON when this is the top-level project)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(_promptty_toplevel ON)
else()
  set(_promptty_toplevel OFF)
endif()
option(PROMPTTY_BUILD_EXAMPLES "Build examples" ${_promptty_toplevel})

# --- CPM dependencies ---
include(cmake/CPM.cmake)

CPMAddPackage(
  NAME utf8proc
  GITHUB_REPOSITORY JuliaStrings/utf8proc
  VERSION 2.9.0
  OPTIONS "UTF8PROC_INSTALL ON"
)

# --- Generated version header ---
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.hpp.in
  ${CMAKE_CURRENT_BINARY_DIR}/include/promptty/version.hpp
  @ONLY
)

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

# --- Library targets ---
foreach(_type STATIC SHARED)
  if(_type STREQUAL "STATIC")
    set(_target promptty_static)
  else()
    set(_target promptty_shared)
  endif()

  add_library(${_target} ${_type} src/promptty.cpp)
  set_target_properties(${_target} PROPERTIES OUTPUT_NAME promptty)

  if(_type STREQUAL "SHARED")
    set_target_properties(${_target} PROPERTIES
      VERSION   ${PROJECT_VERSION}
      SOVERSION ${PROJECT_VERSION_MAJOR}
    )
  endif()

  target_include_directories(${_target}
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
           $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
           $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  )

  target_compile_features(${_target} PUBLIC cxx_std_23)
  target_compile_options(${_target} PRIVATE ${PROMPTTY_WARNINGS})
  target_link_libraries(${_target} PRIVATE $<BUILD_INTERFACE:utf8proc>)

  if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(${_target} PRIVATE -march=native)
  endif()
endforeach()

# --- Alias ---
add_library(promptty::promptty ALIAS promptty_static)

# --- Install (only when built standalone) ---
if(_promptty_toplevel)
  include(GNUInstallDirs)
  include(CMakePackageConfigHelpers)

  install(TARGETS promptty_static promptty_shared
    EXPORT prompttyTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  )

  install(DIRECTORY include/promptty DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
  install(FILES ${CMAKE_CURRENT_BINARY_DIR}/include/promptty/version.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/promptty)

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
endif()

# --- Subdirectories ---
if(PROMPTTY_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()
