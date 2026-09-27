/* SPDX-License-Identifier: Apache-2.0 */
#ifndef CAPMGR_H_
#define CAPMGR_H_
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#if defined(__GNUC__)
#define CAPMGR_API __attribute__((visibility("default")))
#else
#define CAPMGR_API
#endif
typedef struct capmgr_client* capmgr_client_h;
typedef struct capmgr_search_results* capmgr_search_results_h;
typedef uint64_t capmgr_request_token_t;
typedef int32_t capmgr_kind_t;
#define CAPMGR_KIND_ALL ((capmgr_kind_t)0)
#define CAPMGR_KIND_SKILL ((capmgr_kind_t)1)
#define CAPMGR_KIND_APP_SKILL ((capmgr_kind_t)2)
#define CAPMGR_KIND_CLI ((capmgr_kind_t)3)
#define CAPMGR_KIND_ACTION ((capmgr_kind_t)4)
#define CAPMGR_OK 0
#define CAPMGR_ERROR_INVALID_ARGUMENT (-1)
#define CAPMGR_ERROR_PERMISSION_DENIED (-2)
#define CAPMGR_ERROR_DATABASE (-3)
#define CAPMGR_ERROR_NOT_FOUND (-4)
#define CAPMGR_ERROR_CONFLICT (-5)
#define CAPMGR_ERROR_NOT_SUPPORTED (-6)
#define CAPMGR_ERROR_BUSY (-7)
#define CAPMGR_ERROR_LIMIT (-8)
#define CAPMGR_ERROR_IO (-9)
#define CAPMGR_ERROR_OUT_OF_MEMORY (-10)
/* JSON pointers are borrowed for callback duration. Copy to retain. */
typedef bool (*capmgr_foreach_cb)(const char* summary_json, void* user_data);
typedef void (*capmgr_result_cb)(capmgr_request_token_t token,
    const char* response_json, bool is_event, void* user_data);
typedef void (*capmgr_changed_cb)(uint64_t revision, void* user_data);
/* No resource mounts. On any failure *client is NULL. */
CAPMGR_API int capmgr_client_create(capmgr_client_h* client);
/* BUSY during callbacks leaves handle open with no partial destruction.
 * IO retains the handle with admission/callback dispatch closed; keep callback
 * user_data alive and retry destroy. No worker state is freed while live.
 * Successful destroy ends all callback lifetime. NULL is invalid. */
CAPMGR_API int capmgr_client_destroy(capmgr_client_h client);
/* Caller serializes calls on one handle. Foreach callback must not re-enter it.
 * Returning false stops enumeration successfully (OK). */
CAPMGR_API int capmgr_client_foreach_capability(capmgr_client_h client,
    capmgr_kind_t kind, capmgr_foreach_cb callback, void* user_data);
/* On failure *results is NULL. */
CAPMGR_API int capmgr_client_search_capabilities(capmgr_client_h client,
    const char* query, capmgr_kind_t kind, capmgr_search_results_h* results);
/* NULL is a no-op. Invalidates all item pointers; independent of client lifetime. */
CAPMGR_API void capmgr_search_results_free(capmgr_search_results_h results);
/* Additional accessors; count/item output cleared on failure. Out of range:
 * NOT_FOUND. Item borrowed until results_free, including after client destroy. */
CAPMGR_API int capmgr_search_results_count(capmgr_search_results_h results,
    size_t* count);
CAPMGR_API int capmgr_search_results_item(capmgr_search_results_h results,
    size_t index, const char** summary_json);
/* Allocated UTF-8 JSON, released using free(). On failure *detail_json is NULL. */
CAPMGR_API int capmgr_client_get_capability(capmgr_client_h client,
    const char* id, char** detail_json);
/* Admission failure clears token to 0 and produces no callback. Tokens never
 * wrap/reuse during this client lifetime; exhaustion returns LIMIT. */
CAPMGR_API int capmgr_client_execute(capmgr_client_h client,
    const char* request_json, capmgr_result_cb callback, void* user_data,
    capmgr_request_token_t* token);
/* Token is client-local, not a JSON ID. May be called from result callbacks;
 * this is the exception to caller-side serialization. */
CAPMGR_API int capmgr_client_cancel(capmgr_client_h client,
    capmgr_request_token_t token);
CAPMGR_API int capmgr_client_remount_resources(capmgr_client_h client,
    const char* destination_path);
/* Additional event API. NULL unregisters. Replacement and removal return BUSY
 * if any callback is active, including calls from callback context, leaving the
 * old registration intact. After OK no callback uses the old user_data; it may
 * be freed. BUSY performs no change and the old user_data must remain alive. */
CAPMGR_API int capmgr_client_set_changed_callback(capmgr_client_h client,
    capmgr_changed_cb callback, void* user_data);
#ifdef __cplusplus
}
#endif
#endif
