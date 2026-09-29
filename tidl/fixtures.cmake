# SPDX-License-Identifier: Apache-2.0
# Included in the transport directory; retain target-local build actions.
  IF(BUILD_TESTING)
    ADD_EXECUTABLE(capmgr-tidl-probe
      "${PROJECT_SOURCE_DIR}/test/integration/tidl_probe.cc"
    )
    TARGET_LINK_LIBRARIES(capmgr-tidl-probe PRIVATE
      capmgr-tidl-transport
      PkgConfig::GLIB
    )
    TARGET_COMPILE_OPTIONS(capmgr-tidl-probe PRIVATE -Wno-unused-parameter)
    ADD_EXECUTABLE(capmgr-read-transport-probe
      "${PROJECT_SOURCE_DIR}/test/integration/read_transport_probe.cc"
      ${CAPMGR_API_SOURCES}
    )
    TARGET_INCLUDE_DIRECTORIES(capmgr-read-transport-probe PRIVATE
      "${PROJECT_SOURCE_DIR}/src"
      "${PROJECT_SOURCE_DIR}/src/api"
    )
    TARGET_LINK_LIBRARIES(capmgr-read-transport-probe PRIVATE
      capmgr-read-transport
      capmgr-adapters
    )
    TARGET_COMPILE_OPTIONS(capmgr-read-transport-probe PRIVATE -Wno-unused-parameter)
    ADD_CUSTOM_COMMAND(TARGET capmgr-read-transport-probe POST_BUILD
      COMMAND chmod 0755 $<TARGET_FILE:capmgr-read-transport-probe>
      VERBATIM
    )
    ADD_DEPENDENCIES(check capmgr-tidl-probe capmgr-read-transport-probe)
    IF(CYNARA_FOUND)
      # The minimal launcher must not link platform startup dependencies.
      ADD_LIBRARY(capmgr-read-policy-native-fixture SHARED
        "${PROJECT_SOURCE_DIR}/test/integration/read_policy_tidl_native.cc"
      )
      TARGET_LINK_LIBRARIES(capmgr-read-policy-native-fixture PRIVATE
        capmgr-tidl-transport
        nlohmann_json::nlohmann_json
        PkgConfig::GLIB
      )
      TARGET_COMPILE_OPTIONS(capmgr-read-policy-native-fixture PRIVATE
        -Wno-unused-parameter
      )
      ADD_EXECUTABLE(capmgr-read-policy-tidl-probe
        "${PROJECT_SOURCE_DIR}/test/integration/read_policy_tidl_probe.cc"
        "${PROJECT_SOURCE_DIR}/src/launcher/owned_children.cc"
      )
      TARGET_INCLUDE_DIRECTORIES(capmgr-read-policy-tidl-probe PRIVATE
        "${PROJECT_SOURCE_DIR}/src"
      )
      TARGET_LINK_LIBRARIES(capmgr-read-policy-tidl-probe PRIVATE
        nlohmann_json::nlohmann_json
        Threads::Threads
        ${CMAKE_DL_LIBS}
      )
      TARGET_COMPILE_DEFINITIONS(capmgr-read-policy-tidl-probe PRIVATE
        CAPMGR_REAL_POLICY_NATIVE_IMAGE="$<TARGET_FILE:capmgr-read-policy-native-fixture>"
      )
      ADD_DEPENDENCIES(capmgr-read-policy-tidl-probe
        capmgr-read-policy-native-fixture
      )
      FOREACH(POLICY_FIXTURE_TARGET
          capmgr-read-policy-native-fixture capmgr-read-policy-tidl-probe)
        ADD_CUSTOM_COMMAND(TARGET ${POLICY_FIXTURE_TARGET} POST_BUILD
          COMMAND chmod 0755 $<TARGET_FILE:${POLICY_FIXTURE_TARGET}>
          VERBATIM
        )
      ENDFOREACH()
      # Separate inert-reference survivor images: explicit build, never installed
      # or selected by CTest/check. Existing real-policy modes stay unchanged.
      SET(CAPMGR_REFERENCE_MODULE_SOURCES
        "${PROJECT_SOURCE_DIR}/test/integration/read_policy_reference_native.cc"
      )
      PKG_CHECK_MODULES(CAPMGR_REFERENCE_AUL REQUIRED IMPORTED_TARGET aul)
      ADD_LIBRARY(capmgr-reference-native-fixture SHARED EXCLUDE_FROM_ALL
        ${CAPMGR_REFERENCE_MODULE_SOURCES}
      )
      TARGET_LINK_LIBRARIES(capmgr-reference-native-fixture PRIVATE
        capmgr-tidl-transport
        PkgConfig::CAPMGR_REFERENCE_AUL
        nlohmann_json::nlohmann_json
        PkgConfig::GLIB
      )
      TARGET_COMPILE_OPTIONS(capmgr-reference-native-fixture PRIVATE
        -Wno-unused-parameter
      )
      SET(CAPMGR_REFERENCE_LAUNCHER_SOURCES
        "${PROJECT_SOURCE_DIR}/test/integration/read_policy_reference_module_probe.cc"
      )
      ADD_EXECUTABLE(capmgr-reference-module-probe EXCLUDE_FROM_ALL
        ${CAPMGR_REFERENCE_LAUNCHER_SOURCES}
      )
      TARGET_LINK_LIBRARIES(capmgr-reference-module-probe PRIVATE
        nlohmann_json::nlohmann_json
        Threads::Threads
        ${CMAKE_DL_LIBS}
      )
      TARGET_COMPILE_DEFINITIONS(capmgr-reference-module-probe PRIVATE
        CAPMGR_REFERENCE_NATIVE_IMAGE="$<TARGET_FILE:capmgr-reference-native-fixture>"
      )
      ADD_DEPENDENCIES(capmgr-reference-module-probe capmgr-reference-native-fixture)
      FOREACH(CAPMGR_REFERENCE_TARGET
          capmgr-reference-native-fixture capmgr-reference-module-probe)
        ADD_CUSTOM_COMMAND(TARGET ${CAPMGR_REFERENCE_TARGET} POST_BUILD
          COMMAND chmod 0755 $<TARGET_FILE:${CAPMGR_REFERENCE_TARGET}>
          VERBATIM
        )
      ENDFOREACH()
      ADD_DEPENDENCIES(check capmgr-read-policy-tidl-probe)
    ENDIF()
  ENDIF()
