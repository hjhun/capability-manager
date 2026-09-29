# SPDX-License-Identifier: Apache-2.0
# Fixed loader-only fixtures have no platform constructors or authority.
FOREACH(LOADER_VARIANT valid missing)
  SET(LOADER_TARGET "capmgr-code-image-${LOADER_VARIANT}-fixture")
  ADD_LIBRARY(${LOADER_TARGET} SHARED fixtures/read_policy_loader_image.cc)
  IF(LOADER_VARIANT STREQUAL "missing")
    TARGET_COMPILE_DEFINITIONS(${LOADER_TARGET} PRIVATE CAPMGR_LOADER_MISSING_ENTRY)
  ENDIF()
ENDFOREACH()
FILE(WRITE "${PROJECT_BINARY_DIR}/invalid-code-image" "not an ELF image\n")
ADD_EXECUTABLE(capmgr-code-image-tests unit/read_policy_code_image_test.cc)
TARGET_LINK_LIBRARIES(capmgr-code-image-tests PRIVATE
  GTest::gtest_main
  nlohmann_json::nlohmann_json
  Threads::Threads
  ${CMAKE_DL_LIBS}
)
TARGET_COMPILE_DEFINITIONS(capmgr-code-image-tests PRIVATE
  CAPMGR_LOADER_TEST_IMAGE="$<TARGET_FILE:capmgr-code-image-valid-fixture>"
  CAPMGR_LOADER_NO_ENTRY_IMAGE="$<TARGET_FILE:capmgr-code-image-missing-fixture>"
  CAPMGR_LOADER_INVALID_IMAGE="${PROJECT_BINARY_DIR}/invalid-code-image"
)
ADD_DEPENDENCIES(capmgr-code-image-tests
  capmgr-code-image-valid-fixture capmgr-code-image-missing-fixture
)
ADD_TEST(NAME capmgr-code-image COMMAND capmgr-code-image-tests)

# Pure deadline/topology checks: no role, Drop, module or root invocation.
ADD_EXECUTABLE(capmgr-survivor-hold-tests
  unit/read_policy_survivor_hold_test.cc
)
TARGET_LINK_LIBRARIES(capmgr-survivor-hold-tests PRIVATE GTest::gtest_main)
ADD_TEST(NAME capmgr-survivor-hold COMMAND capmgr-survivor-hold-tests)

# Publication pauses/faults interpose only this ordinary executable.
ADD_EXECUTABLE(capmgr-survivor-publication-tests
  unit/read_policy_survivor_publication_test.cc
)
TARGET_LINK_LIBRARIES(capmgr-survivor-publication-tests PRIVATE
  GTest::gtest_main
  Threads::Threads
)
TARGET_LINK_OPTIONS(capmgr-survivor-publication-tests PRIVATE
  -Wl,--wrap=open
  -Wl,--wrap=write
  -Wl,--wrap=renameat2
)
ADD_TEST(NAME capmgr-survivor-publication COMMAND capmgr-survivor-publication-tests)

ADD_EXECUTABLE(capmgr-read-policy-context-tests unit/read_policy_context_test.cc)
TARGET_LINK_LIBRARIES(capmgr-read-policy-context-tests PRIVATE
  GTest::gtest_main
  nlohmann_json::nlohmann_json
  Threads::Threads
)
ADD_TEST(NAME capmgr-read-policy-context COMMAND capmgr-read-policy-context-tests)

# Explicit fixed-reference own-context image: never installed or run by CTest.
ADD_EXECUTABLE(capmgr-reference-context-probe
  integration/read_policy_reference_context_probe.cc
)
TARGET_LINK_LIBRARIES(capmgr-reference-context-probe PRIVATE
  nlohmann_json::nlohmann_json Threads::Threads
)

# Metadata-only default tests; root reference experiments are separately opt-in.
ADD_EXECUTABLE(capmgr-recovery-reference-tests
  unit/read_policy_recovery_reference_test.cc
)
TARGET_LINK_LIBRARIES(capmgr-recovery-reference-tests PRIVATE
  GTest::gtest_main Threads::Threads
)
ADD_TEST(NAME capmgr-recovery-reference COMMAND capmgr-recovery-reference-tests)

SET(CAPMGR_UNIT_TEST_SOURCES
  unit/database_test.cc
  unit/coordinated_writer_test.cc
  unit/catalog_test.cc
  unit/client_test.cc
  unit/search_test.cc
  unit/dispatcher_test.cc
  unit/client_async_test.cc
  unit/client_managed_test.cc
  unit/read_lease_test.cc
  unit/read_grant_test.cc
  unit/managed_dispatcher_test.cc
)
ADD_EXECUTABLE(capmgr-unit-tests
  ${CAPMGR_UNIT_TEST_SOURCES}
  ${CAPMGR_API_SOURCES}
)
TARGET_INCLUDE_DIRECTORIES(capmgr-unit-tests PRIVATE
  "${PROJECT_SOURCE_DIR}/src"
  "${PROJECT_SOURCE_DIR}/src/api"
)
TARGET_LINK_LIBRARIES(capmgr-unit-tests PRIVATE
  capmgr-adapters
  GTest::gtest_main
  GTest::gmock
  Threads::Threads
)
ADD_EXECUTABLE(capmgr-sqlite-lock-probe fixtures/sqlite_lock_probe.cc)
TARGET_LINK_LIBRARIES(capmgr-sqlite-lock-probe PRIVATE capmgr-catalog)
ADD_DEPENDENCIES(capmgr-unit-tests capmgr-sqlite-lock-probe)
ADD_TEST(NAME capmgr-unit COMMAND capmgr-unit-tests)
# Interpose only in this test executable; production has no failure hook.
TARGET_LINK_OPTIONS(capmgr-unit-tests PRIVATE
  "LINKER:--wrap=sqlite3_file_control"
  "LINKER:--wrap=sqlite3_prepare_v2"
)

ADD_EXECUTABLE(capmgr-c-consumer c_consumer.c)
TARGET_LINK_LIBRARIES(capmgr-c-consumer PRIVATE capmgr)
ADD_TEST(NAME capmgr-c-consumer COMMAND capmgr-c-consumer)

ADD_EXECUTABLE(capmgr-cli-fixture fixtures/cli_fixture.cc)
TARGET_LINK_LIBRARIES(capmgr-cli-fixture PRIVATE nlohmann_json::nlohmann_json)
SET(CAPMGR_ADAPTER_TEST_SOURCES
  unit/parser_test.cc
  unit/action_import_test.cc
  unit/runner_test.cc
  unit/action_exchange_test.cc
  unit/owned_children_test.cc
  unit/broker_journal_test.cc
  unit/worker_command_test.cc
  unit/worker_loop_test.cc
  unit/worker_session_test.cc
  unit/worker_catalog_test.cc
  unit/worker_catalog_lease_test.cc
  unit/leased_worker_loop_test.cc
  unit/worker_supervisor_test.cc
  unit/worker_result_test.cc
)
ADD_EXECUTABLE(capmgr-adapter-tests ${CAPMGR_ADAPTER_TEST_SOURCES})
TARGET_LINK_LIBRARIES(capmgr-adapter-tests PRIVATE
  capmgr-adapters
  GTest::gtest_main
)
ADD_DEPENDENCIES(capmgr-adapter-tests capmgr-cli-fixture)
ADD_DEPENDENCIES(capmgr-adapter-tests capmgr-sqlite-lock-probe)
# Faults belong only to the private reader tests, never a product accessor.
TARGET_LINK_OPTIONS(capmgr-adapter-tests PRIVATE
  "LINKER:--wrap=sqlite3_open_v2"
  "LINKER:--wrap=sqlite3_prepare_v2"
  "LINKER:--wrap=sqlite3_step"
  "LINKER:--wrap=sqlite3_close"
)
ADD_TEST(NAME capmgr-adapters COMMAND capmgr-adapter-tests)

ADD_EXECUTABLE(capmgr-peer-tests
  unit/peer_test.cc
  unit/credential_packet_test.cc
  unit/tidl_channels_test.cc
)
TARGET_LINK_LIBRARIES(capmgr-peer-tests PRIVATE
  capmgr-platform
  capmgr-catalog
  GTest::gtest_main
  GTest::gmock
)
IF(CYNARA_FOUND)
  TARGET_SOURCES(capmgr-peer-tests PRIVATE unit/cynara_socket_test.cc)
  TARGET_LINK_LIBRARIES(capmgr-peer-tests PRIVATE PkgConfig::CYNARA)
  TARGET_LINK_OPTIONS(capmgr-peer-tests PRIVATE
    "LINKER:--wrap=cynara_creds_socket_get_pid"
    "LINKER:--wrap=cynara_creds_socket_get_user"
    "LINKER:--wrap=cynara_creds_socket_get_client"
  )
ENDIF()
ADD_TEST(NAME capmgr-peer COMMAND capmgr-peer-tests)
IF(CAPMGR_REQUIRE_CYNARA)
  SET_TESTS_PROPERTIES(capmgr-peer PROPERTIES
    ENVIRONMENT "CAPMGR_REQUIRE_PEER_TESTS=1"
  )
ENDIF()

## Explicit development probes; privileged workloads are not CTest registrations.
ADD_EXECUTABLE(capmgr-read-policy-role
  integration/read_policy_role.cc
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
    integration/read_policy_role.cc
    fixtures/read_policy_scan_failure.cc
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
ADD_EXECUTABLE(capmgr-namespace-probe integration/namespace_probe.cc)
TARGET_LINK_LIBRARIES(capmgr-namespace-probe PRIVATE capmgr-adapters)
ADD_EXECUTABLE(capmgr-worker-probe integration/worker_probe.cc)
TARGET_LINK_LIBRARIES(capmgr-worker-probe PRIVATE capmgr-adapters)
ADD_EXECUTABLE(capmgr-bootstrap-image-fixture fixtures/bootstrap_image.cc)
TARGET_LINK_LIBRARIES(capmgr-bootstrap-image-fixture PRIVATE capmgr-adapters)
ADD_CUSTOM_COMMAND(TARGET capmgr-bootstrap-image-fixture POST_BUILD
  COMMAND chmod 0755 $<TARGET_FILE:capmgr-bootstrap-image-fixture>
  VERBATIM
)
ADD_EXECUTABLE(capmgr-bootstrap-context-probe integration/bootstrap_context_probe.cc)
TARGET_LINK_LIBRARIES(capmgr-bootstrap-context-probe PRIVATE capmgr-adapters)
SET(CAPMGR_WORKER_SPAWN_SOURCE "${PROJECT_SOURCE_DIR}/src/launcher/worker_spawn.cc")

# Intentional separately compiled variants: never substitute the production path.
ADD_EXECUTABLE(capmgr-bootstrap-probe
  integration/bootstrap_probe.cc
  ${CAPMGR_WORKER_SPAWN_SOURCE}
)
TARGET_COMPILE_DEFINITIONS(capmgr-bootstrap-probe PRIVATE
  CAPMGR_WORKER_IMAGE="$<TARGET_FILE:capmgr-bootstrap-image-fixture>"
)
TARGET_LINK_LIBRARIES(capmgr-bootstrap-probe PRIVATE capmgr-adapters)
ADD_DEPENDENCIES(capmgr-bootstrap-probe capmgr-bootstrap-image-fixture)
ADD_EXECUTABLE(capmgr-installed-bootstrap-probe
  integration/bootstrap_probe.cc
  ${CAPMGR_WORKER_SPAWN_SOURCE}
)
TARGET_COMPILE_DEFINITIONS(capmgr-installed-bootstrap-probe PRIVATE
  CAPMGR_WORKER_IMAGE="${CMAKE_INSTALL_FULL_LIBEXECDIR}/capmgr/capmgr-bootstrap-image-fixture"
)
TARGET_LINK_LIBRARIES(capmgr-installed-bootstrap-probe PRIVATE capmgr-adapters)
ADD_DEPENDENCIES(capmgr-installed-bootstrap-probe capmgr-bootstrap-image-fixture)

ADD_EXECUTABLE(capmgr-worker-image-fixture fixtures/worker_image.cc)
# The target shell may use umask0000; only our generated test image is changed.
ADD_CUSTOM_COMMAND(TARGET capmgr-worker-image-fixture POST_BUILD
  COMMAND chmod 0755 $<TARGET_FILE:capmgr-worker-image-fixture>
  VERBATIM
)
ADD_EXECUTABLE(capmgr-worker-spawn-tests
  unit/worker_spawn_test.cc
  ${CAPMGR_WORKER_SPAWN_SOURCE}
)
TARGET_COMPILE_DEFINITIONS(capmgr-worker-spawn-tests PRIVATE
  CAPMGR_TEST_WORKER_IMAGE
  CAPMGR_WORKER_IMAGE="$<TARGET_FILE:capmgr-worker-image-fixture>"
)
TARGET_LINK_LIBRARIES(capmgr-worker-spawn-tests PRIVATE
  capmgr-adapters
  GTest::gtest_main
)
ADD_DEPENDENCIES(capmgr-worker-spawn-tests capmgr-worker-image-fixture)
ADD_TEST(NAME capmgr-worker-spawn COMMAND capmgr-worker-spawn-tests)

FILE(WRITE "${PROJECT_BINARY_DIR}/invalid-worker-image"
  "invalid executable format\n"
)
FILE(CHMOD "${PROJECT_BINARY_DIR}/invalid-worker-image"
  PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
)
ADD_EXECUTABLE(capmgr-worker-spawn-failure-tests
  unit/worker_spawn_failure_test.cc
  ${CAPMGR_WORKER_SPAWN_SOURCE}
)
TARGET_COMPILE_DEFINITIONS(capmgr-worker-spawn-failure-tests PRIVATE
  CAPMGR_TEST_WORKER_IMAGE
  CAPMGR_WORKER_IMAGE="${PROJECT_BINARY_DIR}/invalid-worker-image"
)
TARGET_LINK_LIBRARIES(capmgr-worker-spawn-failure-tests PRIVATE
  capmgr-adapters
  GTest::gtest_main
)
ADD_TEST(NAME capmgr-worker-spawn-failure COMMAND capmgr-worker-spawn-failure-tests)

ADD_EXECUTABLE(capmgr-leased-bootstrap-path-tests
  unit/leased_bootstrap_paths_test.cc
)
TARGET_LINK_LIBRARIES(capmgr-leased-bootstrap-path-tests PRIVATE
  capmgr-adapters GTest::gtest_main
)
ADD_TEST(NAME capmgr-leased-bootstrap-path COMMAND capmgr-leased-bootstrap-path-tests)

ADD_EXECUTABLE(capmgr-leased-bootstrap-image-fixture EXCLUDE_FROM_ALL
  fixtures/leased_bootstrap_image.cc
)
TARGET_LINK_LIBRARIES(capmgr-leased-bootstrap-image-fixture PRIVATE capmgr-adapters)
ADD_EXECUTABLE(capmgr-leased-bootstrap-fault-image-fixture EXCLUDE_FROM_ALL
  fixtures/leased_bootstrap_image.cc
  fixtures/leased_bootstrap_close_fault.cc
)
TARGET_COMPILE_DEFINITIONS(capmgr-leased-bootstrap-fault-image-fixture PRIVATE
  CAPMGR_LEASED_BOOTSTRAP_CLOSE_FAULT
)
TARGET_LINK_OPTIONS(capmgr-leased-bootstrap-fault-image-fixture PRIVATE
  "-Wl,--wrap=sqlite3_close"
)
TARGET_LINK_LIBRARIES(capmgr-leased-bootstrap-fault-image-fixture PRIVATE
  capmgr-adapters
)

# Separate fixed no-START parent images; neither is installed or registered.
FOREACH(LEASED_VARIANT normal fault)
  SET(LEASED_PROBE "capmgr-leased-bootstrap-${LEASED_VARIANT}-probe")
  IF(LEASED_VARIANT STREQUAL "fault")
    SET(LEASED_IMAGE capmgr-leased-bootstrap-fault-image-fixture)
  ELSE()
    SET(LEASED_IMAGE capmgr-leased-bootstrap-image-fixture)
  ENDIF()
  ADD_EXECUTABLE(${LEASED_PROBE} EXCLUDE_FROM_ALL
    integration/leased_bootstrap_probe.cc
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

ADD_TEST(NAME capmgr-verification-tools
  COMMAND "${Python3_EXECUTABLE}" -m unittest discover
    -s "${CMAKE_CURRENT_SOURCE_DIR}/tools" -p "*_test.py"
)
SET_TESTS_PROPERTIES(capmgr-verification-tools PROPERTIES
  ENVIRONMENT
    "CAPMGR_READ_POLICY_ROLE_IMAGE=$<TARGET_FILE:capmgr-read-policy-role>;CAPMGR_READ_POLICY_TASK_FAILURE_IMAGE=$<TARGET_FILE:capmgr-read-policy-task-scan-failure>;CAPMGR_READ_POLICY_FD_FAILURE_IMAGE=$<TARGET_FILE:capmgr-read-policy-fd-scan-failure>"
)
ADD_TEST(NAME capmgr-offline-package
  COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/integration/package_tool_test.py"
    $<TARGET_FILE:capmgr-package-tool>
)
SET_TESTS_PROPERTIES(
  capmgr-unit capmgr-c-consumer capmgr-adapters capmgr-peer
  capmgr-worker-spawn capmgr-worker-spawn-failure
  capmgr-verification-tools capmgr-offline-package
  PROPERTIES WORKING_DIRECTORY "${PROJECT_BINARY_DIR}"
)
ADD_CUSTOM_TARGET(check
  COMMAND "${CMAKE_CTEST_COMMAND}" --output-on-failure --no-tests=error
  WORKING_DIRECTORY "${PROJECT_BINARY_DIR}"
  DEPENDS
    capmgr-survivor-hold-tests
    capmgr-survivor-publication-tests
    capmgr-leased-bootstrap-path-tests
    capmgr-code-image-tests
    capmgr-read-policy-context-tests
    capmgr-recovery-reference-tests
    capmgr-unit-tests
    capmgr-c-consumer
    capmgr-adapter-tests
    capmgr-peer-tests
    capmgr-catalog-benchmark
    capmgr-package-tool
    capmgr-namespace-probe
    capmgr-worker-probe
    capmgr-worker-spawn-tests
    capmgr-worker-spawn-failure-tests
    capmgr-bootstrap-probe
    capmgr-bootstrap-context-probe
    capmgr-read-policy-role
    capmgr-read-policy-task-scan-failure
    capmgr-read-policy-fd-scan-failure
  USES_TERMINAL
)
