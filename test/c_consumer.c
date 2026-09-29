/*
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
/* SPDX-License-Identifier: Apache-2.0 */

#include <capmgr.h>
#include <stdio.h>

#define CHECK(condition)                                      \
  do {                                                        \
    if (!(condition)) {                                       \
      fprintf(stderr, "check failed at line %d\n", __LINE__); \
      return EXIT_FAILURE;                                    \
    }                                                         \
  } while (0)
#include <stdlib.h>

_Static_assert(sizeof(capmgr_request_token_t) == 8, "token width");
_Static_assert(sizeof(capmgr_kind_t) == 4, "kind width");
_Static_assert(sizeof(capmgr_error_e) == 4, "default error enum width");
_Static_assert(CAPMGR_KIND_ACTION == 4 && CAPMGR_ERROR_OUT_OF_MEMORY == -10,
               "public enum values");
int main(void) {
  capmgr_client_h client = NULL;
  CHECK(capmgr_client_create(NULL) == CAPMGR_ERROR_INVALID_ARGUMENT);
  CHECK(capmgr_client_create(&client) == CAPMGR_ERROR_PERMISSION_DENIED);
  CHECK(client == NULL);
  CHECK(capmgr_client_destroy(NULL) == CAPMGR_ERROR_INVALID_ARGUMENT);
  capmgr_search_results_free(NULL);
  size_t count = 99;
  CHECK(capmgr_search_results_count(NULL, &count) ==
        CAPMGR_ERROR_INVALID_ARGUMENT);
  CHECK(count == 0);
  const char* item = NULL;
  CHECK(capmgr_search_results_item(NULL, 0, &item) ==
        CAPMGR_ERROR_INVALID_ARGUMENT);
  CHECK(capmgr_client_foreach_capability(NULL, 0, NULL, NULL) ==
        CAPMGR_ERROR_INVALID_ARGUMENT);
  capmgr_search_results_h results = NULL;
  CHECK(capmgr_client_search_capabilities(NULL, "x", 0, &results) ==
        CAPMGR_ERROR_INVALID_ARGUMENT);
  char* detail = NULL;
  CHECK(capmgr_client_get_capability(NULL, "x", &detail) ==
        CAPMGR_ERROR_INVALID_ARGUMENT);
  capmgr_request_token_t token = 99;
  CHECK(capmgr_client_execute(NULL, "{}", NULL, NULL, &token) ==
        CAPMGR_ERROR_INVALID_ARGUMENT);
  CHECK(token == 0);
  CHECK(capmgr_client_cancel(NULL, 1) == CAPMGR_ERROR_INVALID_ARGUMENT);
  CHECK(capmgr_client_remount_resources(NULL, "/tmp") ==
        CAPMGR_ERROR_INVALID_ARGUMENT);
  CHECK(capmgr_client_set_changed_callback(NULL, NULL, NULL) ==
        CAPMGR_ERROR_INVALID_ARGUMENT);
  return EXIT_SUCCESS;
}
