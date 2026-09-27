# SPDX-License-Identifier: Apache-2.0
ADD_EXECUTABLE(capmgr-package-tool package_tool.cc)
TARGET_LINK_LIBRARIES(capmgr-package-tool PRIVATE capmgr-adapters)
INSTALL(TARGETS capmgr-package-tool
  RUNTIME DESTINATION "${CMAKE_INSTALL_LIBEXECDIR}/capmgr"
)

IF(BUILD_TESTING)
  ADD_EXECUTABLE(capmgr-catalog-benchmark catalog_benchmark.cc)
  TARGET_LINK_LIBRARIES(capmgr-catalog-benchmark PRIVATE capmgr-catalog)
ENDIF()
