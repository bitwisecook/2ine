/* Public domain. No rights reserved. Wholly AI-authored. */
/* POSIX stdio names for the Microsoft C runtime, so storage code uses one
   spelling for 64-bit file offsets. */
#ifndef TB_STORAGE_HOST_STDIO_H
#define TB_STORAGE_HOST_STDIO_H
#ifdef _WIN32
#define fseeko _fseeki64
#define ftello _ftelli64
#define strdup _strdup
#endif
#endif
