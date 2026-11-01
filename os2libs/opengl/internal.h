/* Public domain. No rights reserved. Wholly AI-authored. */
/* PGL handles own host contexts; all client addresses retain guest width. */
#ifndef TWOINE_OPENGL_INTERNAL_H
#define TWOINE_OPENGL_INTERNAL_H
#include "os2libs/os2native.h"
#include "os2libs/pmwin.h"
#include "guest/abi.h"
#include "guest/handles.h"
#include "hal/opengl.h"
#include "hal/host_sync.h"
#include "hal/gl_constants.h"

#define GL_MAX_TRANSFER (64u * 1024u * 1024u)
typedef struct GlMemory
{
    LxGuestBorrow borrow;
    void *host;
    size_t bytes;
    int commit;
} GlMemory;
typedef struct GlArray
{
    uint32_t address, type;
    int32_t size, stride;
    int enabled;
} GlArray;
enum
{
    GL_ARRAY_VERTEX,
    GL_ARRAY_NORMAL,
    GL_ARRAY_COLOR,
    GL_ARRAY_INDEX,
    GL_ARRAY_TEXTURE,
    GL_ARRAY_EDGE,
    GL_ARRAY_COUNT
};
typedef struct GlArrayFrame
{
    GlArray arrays[GL_ARRAY_COUNT];
    uint32_t mask;
} GlArrayFrame;
typedef struct GlContext
{
    struct GlContext *next;
    uint32_t handle, hab, window, error;
    uint64_t owner;
    unsigned width, height;
    int inside;
    uint32_t attribute_stack[16][3];
    unsigned attribute_depth;
    HPS pm_ps;
    LxHostGlConfig config;
    LxHostGlContext *host;
    const LxHostGlApi *api;
    LxGuestMapping *strings;
    GlArray arrays[GL_ARRAY_COUNT];
    GlArrayFrame array_stack[16];
    unsigned array_depth;
    void *selection, *feedback;
    uint32_t selection_address, feedback_address;
    size_t selection_bytes, feedback_bytes;
    uint32_t render_mode;
} GlContext;
typedef struct GlThread
{
    GlContext *current;
    LxHandleBorrow lease;
    uint32_t error;
} GlThread;
typedef struct GlEntry
{
    const char *name;
    uint32_t (*call)(uint8_t *);
    uint16_t argument_bytes, warp_ordinal, gold_ordinal;
} GlEntry;
extern GlContext *GlContexts;
extern LxHostMonitor *GlLock;
GlThread *gl_thread(void);
GlContext *gl_current(void);
void gl_error(uint32_t error);
uint32_t gl_word(const uint8_t *a, unsigned offset);
float gl_float(const uint8_t *a, unsigned offset);
double gl_double(const uint8_t *a, unsigned offset);
int gl_memory(uint32_t address, size_t count, size_t unit, int output, int nullable,
              GlMemory *memory);
void gl_memory_finish(GlMemory *memory);
size_t gl_count(int32_t count);
size_t gl_query_count(uint32_t pname);
size_t gl_parameter_count(uint32_t pname);
size_t gl_type_size(uint32_t type);
size_t gl_pixel_bytes(GlContext *ctx, int32_t width, int32_t height, uint32_t format, uint32_t type,
                      int pack);
size_t gl_texture_bytes(GlContext *ctx, uint32_t target, int32_t level, uint32_t format,
                        uint32_t type);
size_t gl_pixel_map_count(GlContext *ctx, uint32_t map);
size_t gl_map_count(uint32_t target, int32_t ustride, int32_t uorder, int32_t vstride,
                    int32_t vorder);
size_t gl_map_query_count(GlContext *ctx, uint32_t target, uint32_t query);
size_t gl_list_bytes(int32_t count, uint32_t type);
void gl_fix_query(GlContext *ctx, uint32_t pname, void *output, size_t unit, int floating);
int gl_present(GlContext *ctx, int swap);
int gl_resize_current(GlContext *ctx);
int gl_pgl_initialize(void);
void gl_pgl_shutdown(void);
extern const GlEntry GlPglEntries[];
extern const size_t GlPglEntryCount;
#include "os2libs/generated/opengl-custom.h"
#endif
