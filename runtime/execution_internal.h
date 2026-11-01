/* Public domain. No rights reserved. Wholly AI-authored. */
#ifndef TWOINE_EXECUTION_INTERNAL_H
#define TWOINE_EXECUTION_INTERNAL_H
#include "execution.h"
#include "hal/host_sync.h"

typedef struct LxExecutionOperations
{
    const char *name;
    int (*initialize)(LxExecution *execution);
    void (*destroy)(LxExecution *execution);
    void (*program)(const LxExecutionEntry *entry, unsigned bits);
    void (*library)(const LxExecutionEntry *entry, int termination);
    void *(*prepare_thread16)(uint32_t entry, uint32_t stack);
    void (*thread16)(void *invocation);
    void (*destroy_thread16)(void *invocation);
    void (*callback32)(uint32_t entry, uint32_t argument);
    uint16_t (*attach_tib)(uint8_t *tib);
    void *(*current_tib)(void);
    void (*detach_tib)(uint16_t token);
    void *(*alloc_segment)(uint16_t *selector, int executable);
    void (*free_segment)(uint16_t selector);
    int (*code_alias)(uint16_t data_selector, uint16_t *code_selector);
    int (*find_selector)(uint32_t address, uint16_t *selector, uint16_t *offset, int executable);
    void (*free_selector)(uint16_t selector);
    void *(*far_to_host)(uint32_t pointer);
    uint32_t (*host_to_far)(void *pointer);
    void (*stack_segments)(uint32_t address, uint32_t size, int release);
    void (*near_base)(uint32_t address);
    uint16_t (*flat_code_selector)(void);
    void (*prepare_code)(uint8_t *code, size_t size);
    void *(*missing_entry)(const char *module, const char *entry, unsigned bits,
                           LxExecutionBridge *bridge);
    size_t (*emit_bridge16)(uint8_t *output, size_t capacity, uint8_t *segment_base,
                            uint16_t selector_index, LxExecutionApi16 callback,
                            uint16_t argument_bytes);
    int (*atomic_u32)(LxExecution *, uint32_t, void *, int, uint32_t, uint32_t *);
    int (*native_entry)(LxExecution *, void *, uint32_t *);
    int identity_memory;
    int (*map)(LxExecution *, uint32_t, size_t, unsigned, void *);
    int (*unmap)(LxExecution *, uint32_t, size_t);
    int (*protect)(LxExecution *, uint32_t, size_t, unsigned);
    size_t (*emit_bridge32)(uint8_t *, size_t, const LxExecutionApi32 *);
    void (*copy_out)(LxExecution *, const void *, void *, size_t);
    void *(*prepare_thread32)(uint32_t, uint32_t, uint32_t);
    void (*thread32)(void *);
    void (*destroy_thread32)(void *);
    int (*end_thread)(void);
    int (*copy_in)(LxExecution *, uint32_t, void *, const void *, size_t);
    uint32_t (*callback32_4)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    uint32_t (*api_arguments)(void);
    int (*api_registers32)(LxExecutionRegisters32 *);
} LxExecutionOperations;
struct LxExecution
{
    const LxExecutionOperations *ops;
    LxExecutionServices services;
    void *backend;
    LxGuestMapping *memory;
    uint32_t next_address;
    LxGuestMapping *tls_window;
    uint32_t tls_address;
    LxHostMutex *memory_mutex;
    char error[256];
};
#if defined(TWOINE_NATIVE_LINUX_I386)
extern const LxExecutionOperations lx_execution_native_linux;
#endif
#if defined(TWOINE_WITH_UNICORN)
extern const LxExecutionOperations lx_execution_unicorn;
#endif
int lx_execution_fail(LxExecution *execution, const char *message);
#endif
