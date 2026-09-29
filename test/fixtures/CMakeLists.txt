# SPDX-License-Identifier: Apache-2.0
# Fixed loader-only fixtures have no platform constructors or authority.
FOREACH(LOADER_VARIANT valid missing)
  SET(LOADER_TARGET "capmgr-code-image-${LOADER_VARIANT}-fixture")
  ADD_LIBRARY(${LOADER_TARGET} SHARED
    "${PROJECT_SOURCE_DIR}/test/fixtures/read_policy_loader_image.cc"
  )
  IF(LOADER_VARIANT STREQUAL "missing")
    TARGET_COMPILE_DEFINITIONS(${LOADER_TARGET} PRIVATE CAPMGR_LOADER_MISSING_ENTRY)
  ENDIF()
ENDFOREACH()
ADD_EXECUTABLE(capmgr-sqlite-lock-probe
  "${PROJECT_SOURCE_DIR}/test/fixtures/sqlite_lock_probe.cc"
)
TARGET_LINK_LIBRARIES(capmgr-sqlite-lock-probe PRIVATE capmgr-catalog)

ADD_EXECUTABLE(capmgr-cli-fixture
  "${PROJECT_SOURCE_DIR}/test/fixtures/cli_fixture.cc"
)
TARGET_LINK_LIBRARIES(capmgr-cli-fixture PRIVATE nlohmann_json::nlohmann_json)

ADD_EXECUTABLE(capmgr-runner-signal-driver
  "${PROJECT_SOURCE_DIR}/test/fixtures/runner_signal_driver.cc"
)
TARGET_LINK_LIBRARIES(capmgr-runner-signal-driver PRIVATE capmgr-adapters)
TARGET_LINK_OPTIONS(capmgr-runner-signal-driver PRIVATE
  "-Wl,--wrap=posix_spawn"
  "-Wl,--wrap=waitpid"
  "-Wl,--wrap=posix_spawnattr_setsigmask"
  "-Wl,--wrap=posix_spawnattr_setsigdefault"
  "-Wl,--wrap=posix_spawnattr_setflags"
  "-Wl,--wrap=posix_spawnattr_setpgroup"
)
ADD_DEPENDENCIES(capmgr-runner-signal-driver capmgr-cli-fixture)

ADD_EXECUTABLE(capmgr-bootstrap-image-fixture
  "${PROJECT_SOURCE_DIR}/test/fixtures/bootstrap_image.cc"
)
TARGET_LINK_LIBRARIES(capmgr-bootstrap-image-fixture PRIVATE capmgr-adapters)
ADD_CUSTOM_COMMAND(TARGET capmgr-bootstrap-image-fixture POST_BUILD
  COMMAND chmod 0755 $<TARGET_FILE:capmgr-bootstrap-image-fixture>
  VERBATIM
)

ADD_EXECUTABLE(capmgr-worker-image-fixture
  "${PROJECT_SOURCE_DIR}/test/fixtures/worker_image.cc"
)
# The target shell may use umask0000; only our generated test image is changed.
ADD_CUSTOM_COMMAND(TARGET capmgr-worker-image-fixture POST_BUILD
  COMMAND chmod 0755 $<TARGET_FILE:capmgr-worker-image-fixture>
  VERBATIM
)

ADD_EXECUTABLE(capmgr-leased-bootstrap-image-fixture EXCLUDE_FROM_ALL
  "${PROJECT_SOURCE_DIR}/test/fixtures/leased_bootstrap_image.cc"
)
TARGET_LINK_LIBRARIES(capmgr-leased-bootstrap-image-fixture PRIVATE capmgr-adapters)
ADD_EXECUTABLE(capmgr-leased-bootstrap-fault-image-fixture EXCLUDE_FROM_ALL
  "${PROJECT_SOURCE_DIR}/test/fixtures/leased_bootstrap_image.cc"
  "${PROJECT_SOURCE_DIR}/test/fixtures/leased_bootstrap_close_fault.cc"
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
