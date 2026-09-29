# SPDX-License-Identifier: Apache-2.0
SET(CAPMGR_WORKER_SPAWN_SOURCE "${PROJECT_SOURCE_DIR}/src/launcher/worker_spawn.cc")

FILE(WRITE "${PROJECT_BINARY_DIR}/invalid-code-image" "not an ELF image\n")

FILE(WRITE "${PROJECT_BINARY_DIR}/invalid-worker-image"
  "invalid executable format\n"
)
FILE(CHMOD "${PROJECT_BINARY_DIR}/invalid-worker-image"
  PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
)

ADD_SUBDIRECTORY(fixtures)
ADD_SUBDIRECTORY(unit)
ADD_SUBDIRECTORY(integration)

# CMake3.20 test properties stay in the directory of registration.
ADD_TEST(NAME capmgr-code-image COMMAND capmgr-code-image-tests)
ADD_TEST(NAME capmgr-survivor-hold COMMAND capmgr-survivor-hold-tests)
ADD_TEST(NAME capmgr-survivor-publication COMMAND capmgr-survivor-publication-tests)
ADD_TEST(NAME capmgr-reference-route COMMAND capmgr-reference-route-tests)
ADD_TEST(NAME capmgr-read-policy-context COMMAND capmgr-read-policy-context-tests)
ADD_TEST(NAME capmgr-recovery-reference COMMAND capmgr-recovery-reference-tests)
ADD_TEST(NAME capmgr-unit COMMAND capmgr-unit-tests)

ADD_EXECUTABLE(capmgr-c-consumer c_consumer.c)
TARGET_LINK_LIBRARIES(capmgr-c-consumer PRIVATE capmgr)
ADD_TEST(NAME capmgr-c-consumer COMMAND capmgr-c-consumer)
ADD_TEST(NAME capmgr-adapters COMMAND capmgr-adapter-tests)
FOREACH(SIGNAL_CASE normal ignored-parent nocldwait-parent exec-failure invalid-executable
    fault-mask fault-defaults fault-flags fault-init fault-spawn timeout cancel
    cancel-exec-failure cancel-spawn-failure)
  ADD_TEST(NAME "capmgr-runner-signal-${SIGNAL_CASE}"
    COMMAND capmgr-runner-signal-driver "${SIGNAL_CASE}"
  )
  SET_TESTS_PROPERTIES("capmgr-runner-signal-${SIGNAL_CASE}" PROPERTIES
    WORKING_DIRECTORY "${PROJECT_BINARY_DIR}"
    TIMEOUT 8
  )
ENDFOREACH()
ADD_TEST(NAME capmgr-peer COMMAND capmgr-peer-tests)
IF(CAPMGR_REQUIRE_CYNARA)
  SET_TESTS_PROPERTIES(capmgr-peer PROPERTIES
    ENVIRONMENT "CAPMGR_REQUIRE_PEER_TESTS=1"
  )
ENDIF()
ADD_TEST(NAME capmgr-worker-spawn COMMAND capmgr-worker-spawn-tests)
ADD_TEST(NAME capmgr-worker-spawn-failure COMMAND capmgr-worker-spawn-failure-tests)
ADD_TEST(NAME capmgr-leased-bootstrap-path COMMAND capmgr-leased-bootstrap-path-tests)

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
    capmgr-reference-route-tests
    capmgr-leased-bootstrap-path-tests
    capmgr-code-image-tests
    capmgr-read-policy-context-tests
    capmgr-recovery-reference-tests
    capmgr-unit-tests
    capmgr-c-consumer
    capmgr-adapter-tests
    capmgr-runner-signal-driver
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
