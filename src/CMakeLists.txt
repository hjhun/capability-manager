# SPDX-License-Identifier: Apache-2.0
ADD_SUBDIRECTORY(catalog)
ADD_SUBDIRECTORY(platform)

ADD_SUBDIRECTORY(launcher)

SET(CAPMGR_ADAPTER_SOURCES
  "${PROJECT_SOURCE_DIR}/src/pkgmgr-plugin/parser.cc"
  "${PROJECT_SOURCE_DIR}/src/amd-module/action_import.cc"
  ${CAPMGR_LAUNCHER_SOURCES}
)
ADD_LIBRARY(capmgr-adapters STATIC ${CAPMGR_ADAPTER_SOURCES})
TARGET_LINK_LIBRARIES(capmgr-adapters PUBLIC capmgr-catalog Threads::Threads)

ADD_SUBDIRECTORY(api)
