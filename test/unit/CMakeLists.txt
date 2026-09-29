# SPDX-License-Identifier: Apache-2.0
ADD_EXECUTABLE(capmgr-code-image-tests
  "${PROJECT_SOURCE_DIR}/test/unit/read_policy_code_image_test.cc"
)
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

# Pure deadline/topology checks: no role, Drop, module or root invocation.
ADD_EXECUTABLE(capmgr-survivor-hold-tests
  "${PROJECT_SOURCE_DIR}/test/unit/read_policy_survivor_hold_test.cc"
)
TARGET_LINK_LIBRARIES(capmgr-survivor-hold-tests PRIVATE GTest::gtest_main)

# Publication pauses/faults interpose only this ordinary executable.
ADD_EXECUTABLE(capmgr-survivor-publication-tests
  "${PROJECT_SOURCE_DIR}/test/unit/read_policy_survivor_publication_test.cc"
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

ADD_EXECUTABLE(capmgr-read-policy-context-tests
  "${PROJECT_SOURCE_DIR}/test/unit/read_policy_context_test.cc"
)
TARGET_LINK_LIBRARIES(capmgr-read-policy-context-tests PRIVATE
  GTest::gtest_main
  nlohmann_json::nlohmann_json
  Threads::Threads
)

# Metadata-only default tests; root reference experiments are separately opt-in.
ADD_EXECUTABLE(capmgr-recovery-reference-tests
  "${PROJECT_SOURCE_DIR}/test/unit/read_policy_recovery_reference_test.cc"
)
TARGET_LINK_LIBRARIES(capmgr-recovery-reference-tests PRIVATE
  GTest::gtest_main Threads::Threads
)

SET(CAPMGR_UNIT_TEST_SOURCES
  "${PROJECT_SOURCE_DIR}/test/unit/database_test.cc"
  "${PROJECT_SOURCE_DIR}/test/unit/coordinated_writer_test.cc"
  "${PROJECT_SOURCE_DIR}/test/unit/catalog_test.cc"
  "${PROJECT_SOURCE_DIR}/test/unit/client_test.cc"
  "${PROJECT_SOURCE_DIR}/test/unit/search_test.cc"
  "${PROJECT_SOURCE_DIR}/test/unit/dispatcher_test.cc"
  "${PROJECT_SOURCE_DIR}/test/unit/client_async_test.cc"
  "${PROJECT_SOURCE_DIR}/test/unit/client_managed_test.cc"
  "${PROJECT_SOURCE_DIR}/test/unit/read_lease_test.cc"
  "${PROJECT_SOURCE_DIR}/test/unit/read_grant_test.cc"
  "${PROJECT_SOURCE_DIR}/test/unit/managed_dispatcher_test.cc"
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
ADD_DEPENDENCIES(capmgr-unit-tests capmgr-sqlite-lock-probe)
# Interpose only in this test executable; production has no failure hook.
TARGET_LINK_OPTIONS(capmgr-unit-tests PRIVATE
  "LINKER:--wrap=sqlite3_file_control"
  "LINKER:--wrap=sqlite3_prepare_v2"
)
SET(CAPMGR_ADAPTER_TEST_SOURCES
  "${PROJECT_SOURCE_DIR}/test/unit/parser_test.cc"
  "${PROJECT_SOURCE_DIR}/test/unit/action_import_test.cc"
  "${PROJECT_SOURCE_DIR}/test/unit/amd_module_test.cc"
  "${PROJECT_SOURCE_DIR}/test/unit/runner_test.cc"
  "${PROJECT_SOURCE_DIR}/test/unit/action_exchange_test.cc"
  "${PROJECT_SOURCE_DIR}/test/unit/owned_children_test.cc"
  "${PROJECT_SOURCE_DIR}/test/unit/broker_journal_test.cc"
  "${PROJECT_SOURCE_DIR}/test/unit/worker_command_test.cc"
  "${PROJECT_SOURCE_DIR}/test/unit/worker_loop_test.cc"
  "${PROJECT_SOURCE_DIR}/test/unit/worker_session_test.cc"
  "${PROJECT_SOURCE_DIR}/test/unit/worker_catalog_test.cc"
  "${PROJECT_SOURCE_DIR}/test/unit/worker_catalog_lease_test.cc"
  "${PROJECT_SOURCE_DIR}/test/unit/leased_worker_loop_test.cc"
  "${PROJECT_SOURCE_DIR}/test/unit/worker_supervisor_test.cc"
  "${PROJECT_SOURCE_DIR}/test/unit/worker_result_test.cc"
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

ADD_EXECUTABLE(capmgr-peer-tests
  "${PROJECT_SOURCE_DIR}/test/unit/peer_test.cc"
  "${PROJECT_SOURCE_DIR}/test/unit/credential_packet_test.cc"
  "${PROJECT_SOURCE_DIR}/test/unit/tidl_channels_test.cc"
)
TARGET_LINK_LIBRARIES(capmgr-peer-tests PRIVATE
  capmgr-platform
  capmgr-catalog
  GTest::gtest_main
  GTest::gmock
)
IF(CYNARA_FOUND)
  TARGET_SOURCES(capmgr-peer-tests PRIVATE "${PROJECT_SOURCE_DIR}/test/unit/cynara_socket_test.cc")
  TARGET_LINK_LIBRARIES(capmgr-peer-tests PRIVATE PkgConfig::CYNARA)
  TARGET_LINK_OPTIONS(capmgr-peer-tests PRIVATE
    "LINKER:--wrap=cynara_creds_socket_get_pid"
    "LINKER:--wrap=cynara_creds_socket_get_user"
    "LINKER:--wrap=cynara_creds_socket_get_client"
  )
ENDIF()
ADD_EXECUTABLE(capmgr-worker-spawn-tests
  "${PROJECT_SOURCE_DIR}/test/unit/worker_spawn_test.cc"
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
ADD_EXECUTABLE(capmgr-worker-spawn-failure-tests
  "${PROJECT_SOURCE_DIR}/test/unit/worker_spawn_failure_test.cc"
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

ADD_EXECUTABLE(capmgr-leased-bootstrap-path-tests
  "${PROJECT_SOURCE_DIR}/test/unit/leased_bootstrap_paths_test.cc"
)
TARGET_LINK_LIBRARIES(capmgr-leased-bootstrap-path-tests PRIVATE
  capmgr-adapters GTest::gtest_main
)
