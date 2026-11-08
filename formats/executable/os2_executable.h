/* Public domain. No rights reserved. Wholly AI-authored. */
/* Identifies DOS, NE, LX, LE and PE executables and reads NE/LX import module
   names without loading the image. Every offset is bounded by the file size. */
#ifndef TB_OS2_EXECUTABLE_H
#define TB_OS2_EXECUTABLE_H
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* The DOS header holds the new-header offset (e_lfanew) at byte 60. */
#define TB_MZ_HEADER_SIZE 64u
#define TB_MZ_NEW_HEADER 60u
#define TB_NE_HEADER_SIZE 64u
/* The LX/LE header is 0xC4 bytes; LxHeader stops at its last named field. */
#define TB_LX_HEADER_SIZE 196u
/* NE target operating system and LX/LE os_type values for OS/2. */
#define TB_NE_TARGET_OS2 1u
#define TB_LX_OS_OS2 1u
/* Module flag shared by NE and LX: the module is a library (DLL). */
#define TB_EXECUTABLE_LIBRARY 0x8000u

typedef enum TbExecutableFormat
{
    TB_EXECUTABLE_UNKNOWN,
    TB_EXECUTABLE_DOS,
    TB_EXECUTABLE_NE,
    TB_EXECUTABLE_LX,
    TB_EXECUTABLE_LE,
    TB_EXECUTABLE_PE
} TbExecutableFormat;

typedef enum TbExecutableStatus
{
    TB_EXECUTABLE_OK,
    TB_EXECUTABLE_TRUNCATED_HEADER,
    TB_EXECUTABLE_IMPORTS_OUTSIDE,
    TB_EXECUTABLE_TRUNCATED_REFERENCE,
    TB_EXECUTABLE_TRUNCATED_IMPORT,
    TB_EXECUTABLE_STOPPED
} TbExecutableStatus;

/* Reads exactly size bytes at offset, returning 1 on success. */
typedef int (*TbExecutableRead)(void *context, uint64_t offset, void *data, size_t size);

typedef struct TbExecutable
{
    TbExecutableRead read;
    void *context;
    uint64_t size;
    TbExecutableFormat format;
    int dos_stub;           /* The file starts with an MZ header. */
    uint64_t header_offset; /* NE/LX/LE/PE header; zero without a DOS stub. */
    size_t header_size;     /* Bytes of header available, at most TB_LX_HEADER_SIZE. */
    unsigned char header[TB_LX_HEADER_SIZE];
} TbExecutable;

/* Returns 0 only when the DOS header cannot be read. An MZ file without a
   new-header offset is DOS; an unrecognised signature is UNKNOWN. */
int tb_executable_open(TbExecutable *exe, TbExecutableRead read, void *context, uint64_t size);
int tb_executable_open_memory(TbExecutable *exe, const uint8_t *bytes, size_t size);
/* The stream position is undefined afterwards. */
int tb_executable_open_file(TbExecutable *exe, FILE *file, uint64_t size);

/* A whole LX/LE header in little-endian byte and word order. */
int tb_executable_linear_header(const TbExecutable *exe);
/* NE target OS or LX/LE os_type; zero when the header is too short to hold it. */
unsigned tb_executable_target_os(const TbExecutable *exe);
/* Low 16 bits of the NE/LX/LE module flags (NE flags are 16-bit); zero when
   the header is too short to hold them. */
uint32_t tb_executable_module_flags(const TbExecutable *exe);

/* Module names are NUL-terminated, 1-255 bytes, in import-table order.
   Returning 0 from the visitor stops the walk with TB_EXECUTABLE_STOPPED. */
typedef int (*TbExecutableImport)(void *context, const char *module);
/* Reads the import module table of an OS/2 NE or LX/LE executable. */
TbExecutableStatus tb_executable_imports(const TbExecutable *exe, TbExecutableImport visit,
                                         void *context);
#endif
