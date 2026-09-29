# SPDX-License-Identifier: Apache-2.0
IF(TIDLC_EXECUTABLE)
  ADD_CUSTOM_TARGET(capmgr-tidl ALL
    COMMAND "${CMAKE_COMMAND}" -E env
      "TIDLC=${TIDLC_EXECUTABLE}"
      "PYTHON3=${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/generate.sh" "${CAPMGR_GENERATED_DIR}"
    BYPRODUCTS
      "${CAPMGR_GENERATED_DIR}/capability_manager_stub.cc"
      "${CAPMGR_GENERATED_DIR}/capability_manager_stub.h"
      "${CAPMGR_GENERATED_DIR}/capability_manager_proxy.cc"
      "${CAPMGR_GENERATED_DIR}/capability_manager_proxy.h"
    DEPENDS
      capability_manager.tidl
      generate.sh
      bind_channels.py
      check_method_ids.py
    VERBATIM
  )
  IF(TARGET check)
    ADD_DEPENDENCIES(check capmgr-tidl)
  ENDIF()
ENDIF()

IF(CAPMGR_BUILD_TIDL_TRANSPORT)
  ADD_LIBRARY(capmgr-tidl-transport STATIC
    "${CAPMGR_GENERATED_DIR}/capability_manager_stub.cc"
    "${CAPMGR_GENERATED_DIR}/capability_manager_proxy.cc"
  )
  ADD_DEPENDENCIES(capmgr-tidl-transport capmgr-tidl)
  TARGET_INCLUDE_DIRECTORIES(capmgr-tidl-transport PUBLIC "${CAPMGR_GENERATED_DIR}")
  TARGET_LINK_LIBRARIES(capmgr-tidl-transport PUBLIC
    capmgr-platform
    PkgConfig::RPC_PORT
    Threads::Threads
    ${CMAKE_DL_LIBS}
  )
  # Vendor-generated callback defaults intentionally have unused parameters.
  TARGET_COMPILE_OPTIONS(capmgr-tidl-transport PRIVATE -Wno-unused-parameter)
  ADD_LIBRARY(capmgr-read-transport STATIC
    "${PROJECT_SOURCE_DIR}/src/platform/tidl_read_channel.cc"
    "${PROJECT_SOURCE_DIR}/src/platform/tidl_read_service.cc"
  )
  TARGET_LINK_LIBRARIES(capmgr-read-transport PUBLIC
    capmgr-tidl-transport
    capmgr-catalog
    PkgConfig::GLIB
  )
  TARGET_COMPILE_OPTIONS(capmgr-read-transport PRIVATE -Wno-unused-parameter)
  IF(TARGET check)
    ADD_DEPENDENCIES(check capmgr-tidl-transport capmgr-read-transport)
  ENDIF()

  INCLUDE("${CMAKE_CURRENT_SOURCE_DIR}/fixtures.cmake")
ENDIF()
