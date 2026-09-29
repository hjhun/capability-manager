# SPDX-License-Identifier: Apache-2.0
# One parent-owned adapter archive retains the original source order.
SET(CAPMGR_LAUNCHER_SOURCES
  "${PROJECT_SOURCE_DIR}/src/launcher/runner.cc"
  "${PROJECT_SOURCE_DIR}/src/launcher/action_exchange.cc"
  "${PROJECT_SOURCE_DIR}/src/launcher/owned_children.cc"
  "${PROJECT_SOURCE_DIR}/src/launcher/namespace_init.cc"
  "${PROJECT_SOURCE_DIR}/src/launcher/broker_journal.cc"
  "${PROJECT_SOURCE_DIR}/src/launcher/worker_command.cc"
  "${PROJECT_SOURCE_DIR}/src/launcher/worker_loop.cc"
  "${PROJECT_SOURCE_DIR}/src/launcher/worker_session.cc"
  "${PROJECT_SOURCE_DIR}/src/launcher/worker_spawn.cc"
  "${PROJECT_SOURCE_DIR}/src/launcher/worker_catalog.cc"
  "${PROJECT_SOURCE_DIR}/src/launcher/worker_catalog_lease.cc"
  "${PROJECT_SOURCE_DIR}/src/launcher/leased_worker_loop.cc"
  "${PROJECT_SOURCE_DIR}/src/launcher/worker_supervisor.cc"
  "${PROJECT_SOURCE_DIR}/src/launcher/worker_bootstrap.cc"
  "${PROJECT_SOURCE_DIR}/src/launcher/worker_result.cc"
  PARENT_SCOPE
)
