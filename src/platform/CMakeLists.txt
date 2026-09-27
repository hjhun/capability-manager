# SPDX-License-Identifier: Apache-2.0
SET(CAPMGR_PLATFORM_SOURCES
  peer.cc
  authorization.cc
  credential_packet.cc
  remount_ticket.cc
  tidl_channels.cc
)
ADD_LIBRARY(capmgr-platform STATIC ${CAPMGR_PLATFORM_SOURCES})
TARGET_INCLUDE_DIRECTORIES(capmgr-platform PUBLIC "${PROJECT_SOURCE_DIR}/src")
IF(CYNARA_FOUND)
  TARGET_COMPILE_DEFINITIONS(capmgr-platform PRIVATE CAPMGR_HAVE_CYNARA)
  TARGET_LINK_LIBRARIES(capmgr-platform PRIVATE PkgConfig::CYNARA)
ENDIF()
