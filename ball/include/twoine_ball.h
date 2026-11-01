/* Public domain. No rights reserved. Wholly AI-authored. */
#ifndef TWOINE_BALL_H
#define TWOINE_BALL_H
#ifdef _WIN32
#ifdef TWOINE_BALL_BUILD
#define TB_API __declspec(dllexport)
#else
#define TB_API
#endif
#else
#define TB_API __attribute__((visibility("default")))
#endif
#ifdef __cplusplus
extern "C"
{
#endif
    /* UTF-8 JSON in/out. Synchronous, reentrant, no UI and no subprocesses.
 * Call on a worker thread/actor. Success returns 0 and an allocated result;
 * failure returns 2 and an allocated diagnostic. Free either with tb_free.
 * Independent balls may be used concurrently. Serialize writes to the same
 * ball; CONFIG.SYS saves additionally compare their expected original bytes.
 */
    TB_API int tb_request(const char *request_json, char **result_json, char **error_utf8);
    TB_API void tb_free(void *allocation);
    TB_API unsigned tb_abi_version(void);
#ifdef __cplusplus
}
#endif
#endif
