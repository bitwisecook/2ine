/* Public domain. No rights reserved. Wholly AI-authored. */
/* A controlled native-cursor backend, never the user's desktop/cursor. */
#include <SDL3/SDL.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "%s:%d %s\n", __FILE__, __LINE__, #x);                                 \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
typedef struct FakeCursor
{
    struct FakeCursor *next;
} FakeCursor;
static FakeCursor *Cursors;
unsigned Created, Selected, Destroyed;
static SDL_Cursor *create_cursor(SDL_SystemCursor shape)
{
    (void)shape;
    CHECK(pthread_main_np());
    FakeCursor *p = calloc(1, sizeof *p);
    CHECK(p);
    p->next = Cursors;
    Cursors = p;
    Created++;
    return (SDL_Cursor *)p;
}
static bool select_cursor(SDL_Cursor *cursor)
{
    CHECK(pthread_main_np());
    FakeCursor *p = Cursors;
    while (p && (SDL_Cursor *)p != cursor)
        p = p->next;
    CHECK(p);
    Selected++;
    return true;
}
static void destroy_cursor(SDL_Cursor *cursor)
{
    CHECK(pthread_main_np());
    FakeCursor **p = &Cursors;
    while (*p && (SDL_Cursor *)*p != cursor)
        p = &(*p)->next;
    CHECK(*p);
    FakeCursor *old = *p;
    *p = old->next;
    free(old);
    Destroyed++;
}
__attribute__((used)) static const struct
{
    const void *replacement, *original;
} Hooks[] __attribute__((section("__DATA,__interpose"))) = {
    {(void *)create_cursor, (void *)SDL_CreateSystemCursor},
    {(void *)select_cursor, (void *)SDL_SetCursor},
    {(void *)destroy_cursor, (void *)SDL_DestroyCursor}};

void cursor_test_drop_native_objects(void)
{
    while (Cursors)
        destroy_cursor((SDL_Cursor *)Cursors);
}
int cursor_test_objects_empty(void)
{
    return Cursors == NULL;
}
