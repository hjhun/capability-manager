# SPDX-License-Identifier: Apache-2.0
CMAKE_MINIMUM_REQUIRED(VERSION 3.20)
PROJECT(capability-manager VERSION 0.1.0 LANGUAGES C CXX)

## Language and build layout
SET(CMAKE_CXX_STANDARD 20)
SET(CMAKE_CXX_STANDARD_REQUIRED ON)
SET(CMAKE_CXX_EXTENSIONS OFF)
SET(CMAKE_C_STANDARD 11)
SET(CMAKE_C_STANDARD_REQUIRED ON)
SET(CMAKE_POSITION_INDEPENDENT_CODE ON)
SET(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
SET(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
SET(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")

INCLUDE(GNUInstallDirs)
INCLUDE(CTest)
LIST(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake/Modules")

## Optional platform integration; default host builds remain independent.
OPTION(CAPMGR_REQUIRE_CYNARA "Require socket-based Cynara authorization" OFF)
OPTION(CAPMGR_REQUIRE_TIDL "Require platform TIDL code generation" OFF)
OPTION(CAPMGR_BUILD_TIDL_TRANSPORT "Compile platform TIDL transport adapters" OFF)
OPTION(CAPMGR_BUILD_AMD_MODULE "Build the platform AMD catalog module" OFF)
SET(AMD_MODULES_DIR "${CMAKE_INSTALL_DATADIR}/amd" CACHE PATH "AMD module registration directory")
SET(CAPMGR_JSON_SOURCE "" CACHE PATH "Optional extracted nlohmann/json source")
INCLUDE(CapmgrDependencies)
ADD_COMPILE_OPTIONS(-Wall -Wextra -Werror)

## Shared source list for the public library and private injected test variants.
SET(CAPMGR_API_SOURCES
  "${PROJECT_SOURCE_DIR}/src/api/client.cc"
  "${PROJECT_SOURCE_DIR}/src/api/dispatcher.cc"
  "${PROJECT_SOURCE_DIR}/src/api/managed_operation.cc"
  "${PROJECT_SOURCE_DIR}/src/api/read_admission.cc"
)
SET(CAPMGR_GENERATED_DIR "${PROJECT_BINARY_DIR}/generated")

## Components; target names and root build outputs are part of the tool contract.
ADD_SUBDIRECTORY(src)
ADD_SUBDIRECTORY(tools)
IF(BUILD_TESTING)
  ADD_SUBDIRECTORY(test)
ENDIF()
ADD_SUBDIRECTORY(tidl)
