/* Public domain. No rights reserved. Wholly AI-authored. */
#include "formats/executable/os2_executable.h"
#include "formats/endian.h"
#include "formats/executable/lx.h"
#include "formats/executable/ne.h"
#include <limits.h>
#include <stddef.h>
#include <string.h>

#define LX_FIELD(exe, field) ((exe)->header + offsetof(LxHeader, field))
#define NE_FIELD(exe, field) ((exe)->header + offsetof(NeHeader, field))

static int read_at(const TbExecutable *exe, uint64_t offset, void *data, size_t size)
{
    return offset <= exe->size && size <= exe->size - offset &&
           exe->read(exe->context, offset, data, size);
}

int tb_executable_open(TbExecutable *exe, TbExecutableRead read, void *context, uint64_t size)
{
    memset(exe, 0, sizeof *exe);
    exe->read = read;
    exe->context = context;
    exe->size = size;
    unsigned char prefix[TB_MZ_HEADER_SIZE] = {0};
    size_t initial = size < sizeof prefix ? (size_t)size : sizeof prefix;
    if (!read_at(exe, 0, prefix, initial))
        return 0;
    if (initial >= 2 && !memcmp(prefix, "MZ", 2))
    {
        exe->dos_stub = 1;
        if (initial < TB_MZ_HEADER_SIZE || !tb_le32(prefix + TB_MZ_NEW_HEADER))
        {
            exe->format = TB_EXECUTABLE_DOS;
            return 1;
        }
        exe->header_offset = tb_le32(prefix + TB_MZ_NEW_HEADER);
    }
    uint64_t available = exe->header_offset <= size ? size - exe->header_offset : 0;
    size_t wanted = available < sizeof exe->header ? (size_t)available : sizeof exe->header;
    if (wanted < 2 || !read_at(exe, exe->header_offset, exe->header, wanted))
        return 1;
    exe->header_size = wanted;
    if (!memcmp(exe->header, "LX", 2))
        exe->format = TB_EXECUTABLE_LX;
    else if (!memcmp(exe->header, "LE", 2))
        exe->format = TB_EXECUTABLE_LE;
    else if (!memcmp(exe->header, "NE", 2))
        exe->format = TB_EXECUTABLE_NE;
    else if (wanted >= 4 && !memcmp(exe->header, "PE\0\0", 4))
        exe->format = TB_EXECUTABLE_PE;
    return 1;
}

static int memory_read(void *context, uint64_t offset, void *data, size_t size)
{
    memcpy(data, (const uint8_t *)context + offset, size);
    return 1;
}

int tb_executable_open_memory(TbExecutable *exe, const uint8_t *bytes, size_t size)
{
    return tb_executable_open(exe, memory_read, (void *)bytes, size);
}

static int file_read(void *context, uint64_t offset, void *data, size_t size)
{
    FILE *file = context;
    return offset <= LONG_MAX && !fseek(file, (long)offset, SEEK_SET) &&
           fread(data, 1, size, file) == size;
}

int tb_executable_open_file(TbExecutable *exe, FILE *file, uint64_t size)
{
    return tb_executable_open(exe, file_read, file, size);
}

int tb_executable_linear_header(const TbExecutable *exe)
{
    return (exe->format == TB_EXECUTABLE_LX || exe->format == TB_EXECUTABLE_LE) &&
           exe->header_size == TB_LX_HEADER_SIZE && !exe->header[offsetof(LxHeader, byte_order)] &&
           !exe->header[offsetof(LxHeader, word_order)];
}

unsigned tb_executable_target_os(const TbExecutable *exe)
{
    if (exe->format == TB_EXECUTABLE_NE)
        return exe->header_size > offsetof(NeHeader, exe_type) ? *NE_FIELD(exe, exe_type) : 0;
    if (exe->format == TB_EXECUTABLE_LX || exe->format == TB_EXECUTABLE_LE)
        return exe->header_size >= offsetof(LxHeader, os_type) + 2 ? tb_le16(LX_FIELD(exe, os_type))
                                                                   : 0;
    return 0;
}

uint32_t tb_executable_module_flags(const TbExecutable *exe)
{
    if (exe->format == TB_EXECUTABLE_NE)
        return exe->header_size >= offsetof(NeHeader, module_flags) + 2
                   ? tb_le16(NE_FIELD(exe, module_flags))
                   : 0;
    if (exe->format == TB_EXECUTABLE_LX || exe->format == TB_EXECUTABLE_LE)
        return exe->header_size >= offsetof(LxHeader, module_flags) + 2
                   ? tb_le16(LX_FIELD(exe, module_flags))
                   : 0;
    return 0;
}

/* The LX import module table runs up to the import procedure table, or to the
   end of the file when that offset is zero. */
static TbExecutableStatus linear_imports(const TbExecutable *exe, TbExecutableImport visit,
                                         void *context)
{
    uint64_t start = exe->header_offset, length = exe->size;
    uint64_t at = start + tb_le32(LX_FIELD(exe, import_module_table_offset));
    uint32_t count = tb_le32(LX_FIELD(exe, num_import_mod_entries));
    uint32_t procedures = tb_le32(LX_FIELD(exe, import_proc_table_offset));
    uint64_t end = procedures ? start + procedures : length;
    if (count > 65535 || at < start || at > length || (count && (end < at || end > length)))
        return TB_EXECUTABLE_IMPORTS_OUTSIDE;
    for (uint32_t i = 0; i < count; i++)
    {
        unsigned char size = 0;
        char name[256];
        if (at >= end || !read_at(exe, at++, &size, 1) || !size || size > end - at ||
            !read_at(exe, at, name, size) || memchr(name, 0, size))
            return TB_EXECUTABLE_TRUNCATED_IMPORT;
        name[size] = 0;
        at += size;
        if (!visit(context, name))
            return TB_EXECUTABLE_STOPPED;
    }
    return TB_EXECUTABLE_OK;
}

/* NE module references are 16-bit offsets into the imported-names table. */
static TbExecutableStatus segmented_imports(const TbExecutable *exe, TbExecutableImport visit,
                                            void *context)
{
    uint64_t start = exe->header_offset, length = exe->size;
    uint32_t count = tb_le16(NE_FIELD(exe, num_module_ref_table_entries));
    uint64_t references = start + tb_le16(NE_FIELD(exe, module_reference_table_offset));
    uint64_t names = start + tb_le16(NE_FIELD(exe, imported_names_table_offset));
    if (references < start || references > length || (uint64_t)count * 2 > length - references ||
        names < start || names > length)
        return TB_EXECUTABLE_IMPORTS_OUTSIDE;
    for (uint32_t i = 0; i < count; i++)
    {
        unsigned char reference[2], size = 0;
        char name[256];
        if (!read_at(exe, references + i * 2, reference, sizeof reference))
            return TB_EXECUTABLE_TRUNCATED_REFERENCE;
        uint64_t at = names + tb_le16(reference);
        if (!read_at(exe, at, &size, 1) || !size || !read_at(exe, at + 1, name, size) ||
            memchr(name, 0, size))
            return TB_EXECUTABLE_TRUNCATED_IMPORT;
        name[size] = 0;
        if (!visit(context, name))
            return TB_EXECUTABLE_STOPPED;
    }
    return TB_EXECUTABLE_OK;
}

TbExecutableStatus tb_executable_imports(const TbExecutable *exe, TbExecutableImport visit,
                                         void *context)
{
    if (exe->format == TB_EXECUTABLE_NE)
        return exe->header_size < TB_NE_HEADER_SIZE ? TB_EXECUTABLE_TRUNCATED_HEADER
                                                    : segmented_imports(exe, visit, context);
    if (exe->format == TB_EXECUTABLE_LX || exe->format == TB_EXECUTABLE_LE)
        return !tb_executable_linear_header(exe) ? TB_EXECUTABLE_TRUNCATED_HEADER
                                                 : linear_imports(exe, visit, context);
    return TB_EXECUTABLE_OK;
}
