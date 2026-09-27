# SPDX-License-Identifier: Apache-2.0
SET(CAPMGR_CATALOG_SOURCES
  database.cc
  catalog.cc
  read_lease.cc
  read_grant.cc
)
ADD_LIBRARY(capmgr-catalog STATIC ${CAPMGR_CATALOG_SOURCES})
TARGET_INCLUDE_DIRECTORIES(capmgr-catalog PUBLIC "${PROJECT_SOURCE_DIR}/src")
TARGET_LINK_LIBRARIES(capmgr-catalog PUBLIC
  SQLite::SQLite3
  nlohmann_json::nlohmann_json
)
SET_TARGET_PROPERTIES(capmgr-catalog PROPERTIES CXX_VISIBILITY_PRESET hidden)
