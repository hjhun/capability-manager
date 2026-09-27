# SPDX-License-Identifier: Apache-2.0
ADD_SUBDIRECTORY(catalog)
ADD_SUBDIRECTORY(platform)

SET(CAPMGR_ADAPTER_SOURCES
  pkgmgr-plugin/parser.cc
  amd-module/action_import.cc
  launcher/runner.cc
  launcher/action_exchange.cc
  launcher/owned_children.cc
  launcher/namespace_init.cc
  launcher/broker_journal.cc
  launcher/worker_command.cc
  launcher/worker_loop.cc
  launcher/worker_session.cc
  launcher/worker_spawn.cc
  launcher/worker_catalog.cc
  launcher/worker_supervisor.cc
  launcher/worker_bootstrap.cc
  launcher/worker_result.cc
)
ADD_LIBRARY(capmgr-adapters STATIC ${CAPMGR_ADAPTER_SOURCES})
TARGET_LINK_LIBRARIES(capmgr-adapters PUBLIC capmgr-catalog Threads::Threads)

ADD_SUBDIRECTORY(api)
