/* Public domain. No rights reserved. Wholly AI-authored. */
/* Process-level guest execution. No loader objects or host OS types cross this interface. */
#ifndef TWOINE_EXECUTION_H
#define TWOINE_EXECUTION_H
#include <stddef.h>
#include <stdint.h>
#include "host_shared.h"

typedef struct LxExecution LxExecution;
typedef struct LxGuestMapping LxGuestMapping;
/* Packed far pointer: [31..16 selector | 15..0 offset].
   The selector resolves through the backend; it is never a host pointer. */
typedef uint32_t (*LxExecutionApi16)(uint8_t *arguments);
typedef enum LxExecutionAbi32
{
    LX_ABI_STACK32 = 1,
    LX_ABI_EAX32 = 2
} LxExecutionAbi32;
typedef struct LxExecutionApi32
{
    LxExecutionApi16 callback;
    uint16_t argument_bytes;
    uint16_t callee_pop;
    LxExecutionAbi32 convention;
} LxExecutionApi32;
size_t lx_execution_emit_bridge32(LxExecution *execution, uint8_t *output, size_t capacity,
                                  const LxExecutionApi32 *binding);
typedef struct LxGuestBorrow
{
    void *host;
    LxExecution *execution;
    LxGuestMapping *mapping;
} LxGuestBorrow;
/* A borrow pins backing and permissions. Unmap/protect fail while it is live.
   Release every borrow before destroying its execution instance. */
int lx_execution_borrow(LxExecution *execution, uint32_t address, size_t size, unsigned permissions,
                        LxGuestBorrow *borrow);
void lx_execution_release(LxGuestBorrow *borrow);
int lx_execution_borrow_far(LxExecution *execution, uint32_t pointer, size_t size,
                            unsigned permissions, LxGuestBorrow *borrow);
int lx_execution_read(LxExecution *execution, uint32_t address, void *output, size_t size);
int lx_execution_write(LxExecution *execution, uint32_t address, const void *input, size_t size);
/* Snapshot a bounded guest C string into caller-owned host storage. */
char *lx_execution_copy_string(LxExecution *execution, uint32_t address, size_t maximum);
/* Snapshot a fixed count of NUL strings, or a double-NUL terminated list when
   count is zero. The returned allocation always has a trailing extra NUL. */
char *lx_execution_copy_strings(LxExecution *execution, uint32_t address, size_t maximum,
                                unsigned count);
enum
{
    LX_GUEST_READ = 1,
    LX_GUEST_WRITE = 2,
    LX_GUEST_EXECUTE = 4
};
/* Guest mappings use 4-KiB units and never replace occupied ranges. Host backing
   is owned by the mapping. Borrowed host ranges expire at unmap/destroy. */
LxGuestMapping *lx_execution_map(LxExecution *execution, uint32_t address, size_t size,
                                 unsigned permissions, int exact);
/* The execution instance owns each view, independently of the shared object. */
LxGuestMapping *lx_execution_map_shared(LxExecution *execution, LxHostShared *object);
LxGuestMapping *lx_execution_mapping_at(LxExecution *execution, uint32_t address);
uint32_t lx_execution_mapping_address(const LxGuestMapping *mapping);
void *lx_execution_mapping_host(const LxGuestMapping *mapping);
size_t lx_execution_mapping_size(const LxGuestMapping *mapping);
int lx_execution_protect(LxExecution *execution, LxGuestMapping *mapping, unsigned permissions);
int lx_execution_unmap(LxExecution *execution, LxGuestMapping *mapping);
void *lx_execution_memory(LxExecution *execution, uint32_t address, size_t size,
                          unsigned permissions);
int lx_execution_address(LxExecution *execution, const void *host, size_t size,
                         unsigned permissions, uint32_t *address);

/* Atomic access to a writable guest synchronization word, including unaligned
   x86 words. The backend serializes this with guest CPU memory operations. */
int lx_execution_atomic_u32(LxExecution *execution, uint32_t address, int exchange, uint32_t value,
                            uint32_t *previous);

typedef struct LxExecutionEntry
{
    uint32_t entry; /* Linear EIP for USE32; packed selector:offset for USE16. */
    uint32_t stack; /* Linear ESP for USE32; packed selector:offset for USE16. */
    uint32_t module;
    uint32_t command;
    uint32_t environment;
    uint32_t stack_bytes;
    uint16_t data_selector;
    uint16_t data_bytes;
} LxExecutionEntry;

typedef struct LxExecutionBridge
{
    void *host;
    uint32_t address;
    uint32_t segment_base;
    uint32_t size;
    uint16_t selector_index;
} LxExecutionBridge;

typedef struct LxExecutionServices
{
    void *userdata;
    int (*query_executable)(void *userdata, uint32_t address);
    int (*describe_address)(void *userdata, uint32_t address, char *buffer, size_t size);
    void (*program_returned)(void *userdata, uint32_t result);
    uint32_t tib_size;
    uint32_t tib2_offset;
    uint32_t tls_offset;
} LxExecutionServices;

LxExecution *lx_execution_create(const char *backend, const LxExecutionServices *services,
                                 char *error, size_t error_size);
/* Process-wide address of 32 thread-local words. The same guest address aliases
   the current thread's TIB storage in every invocation and host buffer lookup. */
uint32_t lx_execution_tls_window(LxExecution *execution);
void lx_execution_destroy(LxExecution *execution);
const char *lx_execution_backend(const LxExecution *execution);
const char *lx_execution_error(const LxExecution *execution);
/* Resolve mapped guest code or a trusted host API entry using this backend's
   binding convention. Emulation requires a generated guest API entry. */
int lx_execution_entry_address(LxExecution *execution, void *host, uint32_t *address);
void lx_execution_program(LxExecution *execution, const LxExecutionEntry *entry, unsigned bits);
void lx_execution_library(LxExecution *execution, const LxExecutionEntry *entry, int termination);
/* Prepare before creating the host thread, so allocation failures are synchronous.
   The invocation belongs to the caller until destroy, including cancellation. */
void *lx_execution_prepare_thread16(LxExecution *execution, uint32_t entry, uint32_t stack);
void lx_execution_thread16(LxExecution *execution, void *invocation);
void lx_execution_destroy_thread16(LxExecution *execution, void *invocation);
void *lx_execution_prepare_thread32(LxExecution *execution, uint32_t entry, uint32_t stack,
                                    uint32_t argument);
void lx_execution_thread32(LxExecution *execution, void *invocation);
void lx_execution_destroy_thread32(LxExecution *execution, void *invocation);

int lx_execution_end_thread(LxExecution *execution);
void lx_execution_callback32(LxExecution *execution, uint32_t entry, uint32_t argument);
uint16_t lx_execution_attach_tib(LxExecution *execution, uint8_t *tib);
void *lx_execution_current_tib(LxExecution *execution);
void lx_execution_detach_tib(LxExecution *execution, uint16_t token);
void *lx_execution_alloc_segment(LxExecution *execution, uint16_t *selector, int executable);
void lx_execution_free_segment(LxExecution *execution, uint16_t selector);
int lx_execution_code_alias(LxExecution *execution, uint16_t data_selector,
                            uint16_t *code_selector);
int lx_execution_find_selector(LxExecution *execution, uint32_t address, uint16_t *selector,
                               uint16_t *offset, int executable);
void lx_execution_free_selector(LxExecution *execution, uint16_t selector);
void *lx_execution_far_to_host(LxExecution *execution, uint32_t pointer);
uint32_t lx_execution_host_to_far(LxExecution *execution, void *pointer);
void lx_execution_stack_segments(LxExecution *execution, uint32_t address, uint32_t size,
                                 int release);
void lx_execution_near_base(LxExecution *execution, uint32_t address);
uint16_t lx_execution_flat_code_selector(LxExecution *execution);
void lx_execution_prepare_code(LxExecution *execution, uint8_t *code, size_t size);
void *lx_execution_missing_entry(LxExecution *execution, const char *module, const char *entry,
                                 unsigned bits, LxExecutionBridge *bridge);
size_t lx_execution_emit_bridge16(LxExecution *execution, uint8_t *output, size_t capacity,
                                  uint8_t *segment_base, uint16_t selector_index,
                                  LxExecutionApi16 callback, uint16_t argument_bytes);
#endif
