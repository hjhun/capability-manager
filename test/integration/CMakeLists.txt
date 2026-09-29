# SPDX-License-Identifier: Apache-2.0

# Explicit fixed-reference own-context image: never installed or run by CTest.
ADD_EXECUTABLE(capmgr-reference-context-probe
  "${PROJECT_SOURCE_DIR}/test/integration/read_policy_reference_context_probe.cc"
)
TARGET_LINK_LIBRARIES(capmgr-reference-context-probe PRIVATE
  nlohmann_json::nlohmann_json Threads::Threads
)

## Explicit development probes; privileged workloads are not CTest registrations.
ADD_EXECUTABLE(capmgr-read-policy-role
  "${PROJECT_SOURCE_DIR}/test/integration/read_policy_role.cc"
  ${CAPMGR_API_SOURCES}
)
TARGET_INCLUDE_DIRECTORIES(capmgr-read-policy-role PRIVATE
  "${PROJECT_SOURCE_DIR}/src"
  "${PROJECT_SOURCE_DIR}/src/api"
)
TARGET_LINK_LIBRARIES(capmgr-read-policy-role PRIVATE
  capmgr-adapters
  Threads::Threads
)
# Build-only fixture: no install, policy scriptlet or privileged CTest command.
ADD_CUSTOM_COMMAND(TARGET capmgr-read-policy-role POST_BUILD
  COMMAND chmod 0755 $<TARGET_FILE:capmgr-read-policy-role>
  VERBATIM
)
# Enumeration faults are linked only into distinct fixed test images.
FOREACH(SCAN task fd)
  SET(SCAN_TARGET "capmgr-read-policy-${SCAN}-scan-failure")
  ADD_EXECUTABLE(${SCAN_TARGET}
    "${PROJECT_SOURCE_DIR}/test/integration/read_policy_role.cc"
    "${PROJECT_SOURCE_DIR}/test/fixtures/read_policy_scan_failure.cc"
    ${CAPMGR_API_SOURCES}
  )
  TARGET_INCLUDE_DIRECTORIES(${SCAN_TARGET} PRIVATE
    "${PROJECT_SOURCE_DIR}/src"
    "${PROJECT_SOURCE_DIR}/src/api"
  )
  TARGET_LINK_LIBRARIES(${SCAN_TARGET} PRIVATE capmgr-adapters Threads::Threads)
  TARGET_LINK_OPTIONS(${SCAN_TARGET} PRIVATE "LINKER:--wrap=readdir")
  IF(SCAN STREQUAL "task")
    TARGET_COMPILE_DEFINITIONS(${SCAN_TARGET} PRIVATE CAPMGR_FAIL_TASK_SCAN)
  ENDIF()
  ADD_CUSTOM_COMMAND(TARGET ${SCAN_TARGET} POST_BUILD
    COMMAND chmod 0755 $<TARGET_FILE:${SCAN_TARGET}>
    VERBATIM
  )
ENDFOREACH()
ADD_EXECUTABLE(capmgr-namespace-probe
  "${PROJECT_SOURCE_DIR}/test/integration/namespace_probe.cc"
)
TARGET_LINK_LIBRARIES(capmgr-namespace-probe PRIVATE capmgr-adapters)
ADD_EXECUTABLE(capmgr-worker-probe
  "${PROJECT_SOURCE_DIR}/test/integration/worker_probe.cc"
)
TARGET_LINK_LIBRARIES(capmgr-worker-probe PRIVATE capmgr-adapters)
ADD_EXECUTABLE(capmgr-bootstrap-context-probe
  "${PROJECT_SOURCE_DIR}/test/integration/bootstrap_context_probe.cc"
)
TARGET_LINK_LIBRARIES(capmgr-bootstrap-context-probe PRIVATE capmgr-adapters)

# Intentional separately compiled variants: never substitute the production path.
ADD_EXECUTABLE(capmgr-bootstrap-probe
  "${PROJECT_SOURCE_DIR}/test/integration/bootstrap_probe.cc"
  ${CAPMGR_WORKER_SPAWN_SOURCE}
)
TARGET_COMPILE_DEFINITIONS(capmgr-bootstrap-probe PRIVATE
  CAPMGR_WORKER_IMAGE="$<TARGET_FILE:capmgr-bootstrap-image-fixture>"
)
TARGET_LINK_LIBRARIES(capmgr-bootstrap-probe PRIVATE capmgr-adapters)
ADD_DEPENDENCIES(capmgr-bootstrap-probe capmgr-bootstrap-image-fixture)
ADD_EXECUTABLE(capmgr-installed-bootstrap-probe
  "${PROJECT_SOURCE_DIR}/test/integration/bootstrap_probe.cc"
  ${CAPMGR_WORKER_SPAWN_SOURCE}
)
TARGET_COMPILE_DEFINITIONS(capmgr-installed-bootstrap-probe PRIVATE
  CAPMGR_WORKER_IMAGE="${CMAKE_INSTALL_FULL_LIBEXECDIR}/capmgr/capmgr-bootstrap-image-fixture"
)
TARGET_LINK_LIBRARIES(capmgr-installed-bootstrap-probe PRIVATE capmgr-adapters)
ADD_DEPENDENCIES(capmgr-installed-bootstrap-probe capmgr-bootstrap-image-fixture)

# Separate fixed no-START parent images; neither is installed or registered.
FOREACH(LEASED_VARIANT normal fault)
  SET(LEASED_PROBE "capmgr-leased-bootstrap-${LEASED_VARIANT}-probe")
  IF(LEASED_VARIANT STREQUAL "fault")
    SET(LEASED_IMAGE capmgr-leased-bootstrap-fault-image-fixture)
  ELSE()
    SET(LEASED_IMAGE capmgr-leased-bootstrap-image-fixture)
  ENDIF()
  ADD_EXECUTABLE(${LEASED_PROBE} EXCLUDE_FROM_ALL
    "${PROJECT_SOURCE_DIR}/test/integration/leased_bootstrap_probe.cc"
    ${CAPMGR_WORKER_SPAWN_SOURCE}
  )
  TARGET_COMPILE_DEFINITIONS(${LEASED_PROBE} PRIVATE
    CAPMGR_WORKER_IMAGE="$<TARGET_FILE:${LEASED_IMAGE}>"
    CAPMGR_LEASED_BOOTSTRAP_LOCK_PROBE="$<TARGET_FILE:capmgr-sqlite-lock-probe>"
  )
  IF(LEASED_VARIANT STREQUAL "fault")
    TARGET_COMPILE_DEFINITIONS(${LEASED_PROBE} PRIVATE
      CAPMGR_LEASED_BOOTSTRAP_FAULT_PROBE
    )
  ENDIF()
  TARGET_LINK_LIBRARIES(${LEASED_PROBE} PRIVATE capmgr-adapters)
  ADD_DEPENDENCIES(${LEASED_PROBE} ${LEASED_IMAGE} capmgr-sqlite-lock-probe)
ENDFOREACH()
