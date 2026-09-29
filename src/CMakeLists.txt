# SPDX-License-Identifier: Apache-2.0
ADD_SUBDIRECTORY(catalog)
ADD_SUBDIRECTORY(platform)

ADD_SUBDIRECTORY(launcher)

ADD_LIBRARY(capmgr-logging STATIC common/logging.cc)
TARGET_INCLUDE_DIRECTORIES(capmgr-logging PUBLIC "${PROJECT_SOURCE_DIR}/src")
TARGET_COMPILE_DEFINITIONS(capmgr-logging PUBLIC LOG_TAG="CAPMGR")
IF(CAPMGR_BUILD_AMD_MODULE)
  TARGET_COMPILE_DEFINITIONS(capmgr-logging PRIVATE CAPMGR_HAVE_DLOG)
  TARGET_LINK_LIBRARIES(capmgr-logging PRIVATE PkgConfig::DLOG)
ENDIF()

SET(CAPMGR_ADAPTER_SOURCES
  "${PROJECT_SOURCE_DIR}/src/pkgmgr-plugin/parser.cc"
  "${PROJECT_SOURCE_DIR}/src/amd-module/action_import.cc"
  "${PROJECT_SOURCE_DIR}/src/amd-module/catalog_service.cc"
  "${PROJECT_SOURCE_DIR}/src/amd-module/module_config.cc"
  "${PROJECT_SOURCE_DIR}/src/amd-module/module_task.cc"
  ${CAPMGR_LAUNCHER_SOURCES}
)
ADD_LIBRARY(capmgr-adapters STATIC ${CAPMGR_ADAPTER_SOURCES})
TARGET_LINK_LIBRARIES(capmgr-adapters PUBLIC capmgr-catalog capmgr-logging Threads::Threads)

ADD_SUBDIRECTORY(api)
ADD_SUBDIRECTORY(amd-module)
