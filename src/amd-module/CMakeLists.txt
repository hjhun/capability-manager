# SPDX-License-Identifier: Apache-2.0
IF(CAPMGR_BUILD_AMD_MODULE)
  SET(CAPMGR_AMD_MODULE_SOURCES module.cc core_platform.cc)
  ADD_LIBRARY(amd-mod-capability-manager SHARED ${CAPMGR_AMD_MODULE_SOURCES})
  TARGET_LINK_LIBRARIES(amd-mod-capability-manager PRIVATE
    capmgr-adapters
    PkgConfig::AMD
    PkgConfig::TZPLATFORM
    PkgConfig::TIZEN_CORE
    PkgConfig::DLOG
  )
  SET_TARGET_PROPERTIES(amd-mod-capability-manager PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
  )
  TARGET_LINK_OPTIONS(amd-mod-capability-manager PRIVATE
    "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/module.map"
  )
  INSTALL(TARGETS amd-mod-capability-manager
    LIBRARY DESTINATION "${AMD_MODULES_DIR}/mod"
  )
  INSTALL(FILES "${PROJECT_SOURCE_DIR}/packaging/amd.json"
    DESTINATION "${CMAKE_INSTALL_SYSCONFDIR}/capmgr"
  )
ENDIF()
