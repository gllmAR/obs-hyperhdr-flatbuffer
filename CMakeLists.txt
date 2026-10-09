# SPDX-License-Identifier: GPL-2.0-or-later
cmake_minimum_required(VERSION 3.24)

project(obs-hyperhdr VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

# Build identifier written to the OBS log. Defaults to the short git commit.
set(HYPERHDR_BUILD_ID "" CACHE STRING "Build identifier shown in the OBS log")
if(NOT HYPERHDR_BUILD_ID)
  set(HYPERHDR_BUILD_ID "unknown")
  find_package(Git QUIET)
  if(Git_FOUND)
    execute_process(
      COMMAND "${GIT_EXECUTABLE}" rev-parse --short HEAD
      WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
      OUTPUT_VARIABLE _hyperhdr_sha
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET
      RESULT_VARIABLE _hyperhdr_rc)
    if(_hyperhdr_rc EQUAL 0 AND _hyperhdr_sha)
      set(HYPERHDR_BUILD_ID "${_hyperhdr_sha}")
    endif()
  endif()
endif()

# Core unit tests need no libobs, so this configure stops before the plugin.
option(HYPERHDR_BUILD_TESTS "Build core unit tests only (no libobs needed)" OFF)
if(HYPERHDR_BUILD_TESTS)
  enable_testing()
  # One transport implementation per platform: POSIX sockets or Winsock.
  if(WIN32)
    set(HHD_TRANSPORT_SRC src/core/hhd_transport_win.cpp)
  else()
    set(HHD_TRANSPORT_SRC src/core/hhd_transport.cpp)
  endif()

  add_executable(hhd_core_tests
    tests/unit/test_core.cpp
    src/core/hhd_encoder.cpp
    ${HHD_TRANSPORT_SRC})
  target_include_directories(hhd_core_tests PRIVATE src/core tests/unit)
  add_test(NAME hhd_core COMMAND hhd_core_tests)

  find_package(Threads REQUIRED)
  add_executable(hhd_sink_tests
    tests/unit/test_sink.cpp
    src/core/hhd_encoder.cpp
    src/core/hhd_sink.cpp
    ${HHD_TRANSPORT_SRC})
  target_include_directories(hhd_sink_tests PRIVATE src/core tests/unit)
  target_link_libraries(hhd_sink_tests PRIVATE Threads::Threads)
  add_test(NAME hhd_sink COMMAND hhd_sink_tests)

  if(WIN32)
    target_link_libraries(hhd_core_tests PRIVATE ws2_32)
    target_link_libraries(hhd_sink_tests PRIVATE ws2_32)
  endif()
  return()
endif()

# libobs comes from the system (Linux) or from an OBS source build whose
# build tree is passed through CMAKE_PREFIX_PATH (macOS, Windows).
find_package(libobs CONFIG REQUIRED)

add_library(${PROJECT_NAME} MODULE
  src/obs/plugin-main.c)

set_target_properties(${PROJECT_NAME} PROPERTIES
  PREFIX ""
  C_VISIBILITY_PRESET hidden)

target_compile_definitions(${PROJECT_NAME} PRIVATE
  HYPERHDR_VERSION="${PROJECT_VERSION}"
  HYPERHDR_BUILD_ID="${HYPERHDR_BUILD_ID}")

if(APPLE)
  # The host OBS process provides the libobs symbols at runtime. Linking the
  # build-tree framework would hard-code its path into the plugin, so only the
  # headers are taken from the target and the symbols are resolved lazily.
  get_target_property(_hyperhdr_obs_includes OBS::libobs INTERFACE_INCLUDE_DIRECTORIES)
  target_include_directories(${PROJECT_NAME} PRIVATE ${_hyperhdr_obs_includes})
  # libobs headers include simde/, which is not in its exported include list.
  target_link_libraries(${PROJECT_NAME} PRIVATE SIMDe::SIMDe)
  target_link_options(${PROJECT_NAME} PRIVATE -undefined dynamic_lookup)

  set_target_properties(${PROJECT_NAME} PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION "plugin"
    MACOSX_BUNDLE_BUNDLE_NAME "${PROJECT_NAME}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
else()
  target_link_libraries(${PROJECT_NAME} PRIVATE OBS::libobs)
endif()

# Package layout:
#   macOS:          obs-hyperhdr.plugin           -> <plugins>/
#   Linux/Windows:  obs-hyperhdr/bin/64bit/<lib>  -> <plugins>/
if(APPLE)
  install(TARGETS ${PROJECT_NAME} LIBRARY DESTINATION . BUNDLE DESTINATION .)
else()
  install(TARGETS ${PROJECT_NAME}
    LIBRARY DESTINATION ${PROJECT_NAME}/bin/64bit
    RUNTIME DESTINATION ${PROJECT_NAME}/bin/64bit)
endif()
