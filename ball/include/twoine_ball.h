/* Public domain. No rights reserved. Wholly AI-authored. */
/* The ball engine's public C interface: one JSON request in, one JSON result out. */
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
 * Each independent ball may be used concurrently. Serialize writes to the same
 * ball; CONFIG.SYS saves additionally compare their expected original bytes.
 * Component install/remove jobs enforce a cooperative per-ball mutation lock.
 * os2-component-state and cancellation may run concurrently with those jobs;
 * their progress reads never wait for the mutation lock.
 */
#define TB_REQUEST_SUCCESS 0
#define TB_REQUEST_FAILURE 2
    TB_API int tb_request(const char *request_json, char **result_json, char **error_utf8);
    TB_API void tb_free(void *allocation);
    TB_API unsigned tb_abi_version(void);
#ifdef __cplusplus
}
#endif
#endif
