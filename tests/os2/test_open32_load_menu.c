/* Public domain. No rights reserved. Wholly AI-authored. */
/* Loaded PM resource menus retain their complete queryable hierarchy and ownership. */
#include "os2/include/private/export.h"
#include "os2/include/os2win.h"
#include "os2/include/private/pmwinp.h"
#include "emulator/abi.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#define CHECK(condition)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (!(condition))                                                                          \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition);                        \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
extern ULONG OS2API LoadMenu(ULONG module, PSZ name);
static uint8 resource[512], names[64];
static size_t resource_size, names_size;
static unsigned acquired, released;
static BOOL malformed_menu, malformed_names;

static size_t begin_menu(unsigned count)
{
    size_t offset = resource_size;
    resource_size += 10;
    lx_guest_store_u16(resource + offset + 4, 850);
    lx_guest_store_u16(resource + offset + 6, 4);
    lx_guest_store_u16(resource + offset + 8, count);
    return offset;
}

static void end_menu(size_t offset)
{
    lx_guest_store_u32(resource + offset, (uint32)(resource_size - offset));
}

static void item(unsigned style, unsigned attributes, unsigned id, const char *text)
{
    lx_guest_store_u16(resource + resource_size, style);
    lx_guest_store_u16(resource + resource_size + 2, attributes);
    lx_guest_store_u16(resource + resource_size + 4, id);
    resource_size += 6;
    if (!(style & 4))
    {
        size_t length = strlen(text) + 1;
        memcpy(resource + resource_size, text, length);
        resource_size += length;
    }
}

static void make_resource(void)
{
    resource_size = 0;
    size_t root = begin_menu(2);
    item(0x11, 0, 40, "~File");
    size_t submenu = begin_menu(3);
    item(1, 0x6000, 100, "~Open\tCtrl+O");
    item(4, 0, 0, NULL);
    item(0x11, 0, 41, "~Tools");
    size_t tools = begin_menu(1);
    item(1, 0, 200, "~About");
    end_menu(tools);
    end_menu(submenu);
    item(1, 0, 300, "Caf\x82 & tea");
    end_menu(root);
    lx_guest_store_u32(names, 2);
    lx_guest_store_u32(names + 4, 91);
    lx_guest_store_u32(names + 8, UINT32_C(0x8000005f)); /* RF_ORDINALID | 95 */
    memcpy(names + 12, "MainMenu\0Alternative\0", 21);
    names_size = 33;
}

static uint32 query(uint32 module, uint32 type, uint32 id, uint32 *size)
{
    if (module != 123)
        return 2;
    if (type == 3 && (id == 91 || id == 95))
    {
        *size = (uint32)resource_size - !!malformed_menu;
        return 0;
    }
    if (type == 255 && id == 3)
    {
        *size = (uint32)names_size - (malformed_names ? 2 : 0);
        return 0;
    }
    return 2;
}

static uint32 get(uint32 module, uint32 type, uint32 id, void **output)
{
    uint32 size;
    if (query(module, type, id, &size))
        return 2;
    uint8 *copy = malloc(size);
    CHECK(copy);
    memcpy(copy, type == 3 ? resource : names, size);
    if (type == 3 && malformed_menu)
        lx_guest_store_u32(copy, size);
    *output = copy;
    acquired++;
    return 0;
}

static uint32 release(void *data)
{
    CHECK(data);
    released++;
    free(data);
    return 0;
}

static size_t windows(void)
{
    size_t count = 0;
    for (HWND handle = FIRST_HWND_VALUE; handle < FIRST_HWND_VALUE + 65536; handle++)
        count += WinIsWindow(0, handle) != FALSE;
    return count;
}

static void inspect(ULONG root)
{
    CHECK(root && IsMenu(root));
    CHECK(WinQueryWindow(root, 5) == HWND_OBJECT);
    CHECK(GetMenuItemCount(root) == 2);
    ULONG sub = GetSubMenu(root, 0), tools = GetSubMenu(sub, 2);
    CHECK(IsMenu(sub) && IsMenu(tools));
    CHECK(GetMenuItemCount(sub) == 3 && GetMenuItemID(tools, 0) == 200);
    CHECK(GetMenuState(root, 100, 0) == 9);
    char text[64];
    CHECK(GetMenuString(root, 100, text, sizeof text, 0) == 12);
    CHECK(!strcmp(text, "&Open\tCtrl+O"));
    CHECK(GetMenuString(root, 300, text, sizeof text, 0) == 11);
    CHECK(!strcmp(text, "Caf\xe9 && tea"));
    Window *window = lx_pm_menu_window(sub);
    CHECK(window && strchr(window->menu->items[0].text, 8));
    CHECK(DestroyMenu(root));
    CHECK(!IsMenu(root) && !IsMenu(sub) && !IsMenu(tools));
    CHECK(!WinIsWindow(0, root) && !WinIsWindow(0, sub) && !WinIsWindow(0, tools));
    CHECK(acquired == released);
}

int main(void)
{
    CHECK(SDL_WasInit(SDL_INIT_VIDEO) == 0);
    CHECK(SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy"));
    CHECK(SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software"));
    make_resource();
    LxModule *previous_main = GProcessState.main_module;
    LxModule main_module = {0};
    main_module.handle = 123;
    GProcessState.main_module = &main_module;
    __typeof__(GProcessState.queryResourceSize) previous_query = GProcessState.queryResourceSize;
    __typeof__(GProcessState.getResource) previous_get = GProcessState.getResource;
    __typeof__(GProcessState.freeResource) previous_release = GProcessState.freeResource;
    GProcessState.queryResourceSize = query;
    GProcessState.getResource = get;
    GProcessState.freeResource = release;
    CHECK(GetDesktopWindow() == HWND_DESKTOP);
    CHECK(!strcmp(SDL_GetCurrentVideoDriver(), "dummy"));
    size_t before = windows();
    inspect(LoadMenu(123, (PSZ)(uintptr_t)91));
    CHECK(windows() == before);
    inspect(LoadMenu(0, "#91"));
    inspect(LoadMenu(UINT32_MAX, "mAiNmEnU"));
    inspect(LoadMenu(123, "ALTERNATIVE"));
    CHECK(windows() == before);
    ULONG root = LoadMenu(123, (PSZ)(uintptr_t)91), sub = GetSubMenu(root, 0);
    ULONG tools = GetSubMenu(sub, 2);
    CHECK(RemoveMenu(root, 0, 0x400) && DestroyMenu(root));
    CHECK(IsMenu(sub) && IsMenu(tools));
    CHECK(DestroyMenu(sub) && !IsMenu(tools));
    root = LoadMenu(123, (PSZ)(uintptr_t)91);
    sub = GetSubMenu(root, 0);
    tools = GetSubMenu(sub, 2);
    CHECK(DeleteMenu(root, 0, 0x400));
    CHECK(!IsMenu(sub) && !IsMenu(tools));
    CHECK(DestroyMenu(root));
    malformed_menu = TRUE;
    CHECK(!LoadMenu(123, (PSZ)(uintptr_t)91));
    CHECK(windows() == before && acquired == released);
    malformed_menu = FALSE;
    malformed_names = TRUE;
    CHECK(!LoadMenu(123, "Alternative"));
    CHECK(windows() == before && acquired == released);
    malformed_names = FALSE;
    CHECK(!LoadMenu(123, "missing") && !LoadMenu(999, "MainMenu"));
    CHECK(!LoadMenu(123, NULL));
    CHECK(windows() == before && acquired == released);
    GProcessState.queryResourceSize = previous_query;
    GProcessState.getResource = previous_get;
    GProcessState.freeResource = previous_release;
    GProcessState.main_module = previous_main;
    puts(
        "Loaded menus preserve object parent, names, nested state/text and complete ownership cleanup");
    return 0;
}
