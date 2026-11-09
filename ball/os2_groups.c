/* Public domain. No rights reserved. Wholly AI-authored. */
/* Stable media group identifiers shared by extraction and dependency reports. */
#include "ball/store.h"
#include "ball/os2_groups.h"

const char *extract_categories[EXTRACT_COUNT] = {
    "cmd",        "fonts",   "assets",    "printers",  "help",        "full",   "som",   "runtime",
    "multimedia", "network", "utilities", "languages", "diagnostics", "legacy", "opengl"};

int extract_category(const char *name)
{
    for (int i = 0; i < EXTRACT_COUNT; i++)
        if (equal_case(name, extract_categories[i]))
            return i;
    return -1;
}

cJSON *extract_group(int category)
{
    static const char *titles[EXTRACT_COUNT] = {"CMD and REXX Support",
                                                "Fonts",
                                                "Sounds, Images and Video",
                                                "Printer Support",
                                                "Full Help",
                                                "Full OS/2 File Set",
                                                "SOM/DSOM Object Runtime",
                                                "Unicode and C Libraries",
                                                "Multimedia Files",
                                                "Networking Files",
                                                "Desktop Utilities",
                                                "Languages and Code Pages",
                                                "Diagnostics and Maintenance",
                                                "DOS and Win-OS/2 Files",
                                                "OpenGL Providers"};
    static const char *details[EXTRACT_COUNT] = {
        "Command processor, messages and REXX support.",
        "Original bitmap and outline fonts.",
        "Original sounds, images and sample videos.",
        "Original printer drivers and support files.",
        "Original help manager, viewers, help files and books.",
        "All original OS/2 media files. Files supplied by 2ine are excluded.",
        "Original SOM/DSOM libraries, daemon and server tools, repositories and messages. Requires Unicode and C Runtime.",
        "Original LIBCS, LIBUNI and UCONV runtime libraries.",
        "Original multimedia tools and support files. Files supplied by 2ine are excluded. Some bundled tools require OS/2 services that 2ine does not yet support.",
        "Original networking tools and configuration files. Files supplied by 2ine are excluded. Some bundled tools require OS/2 services that 2ine does not yet support.",
        "Original desktop applications, editors, games and their support files.",
        "Original locale, keyboard and code-page data and language resources.",
        "Original diagnostic, tracing, disk maintenance and symbol files.",
        "Original DOS and Win-OS/2 compatibility files. Installing files does not enable unsupported execution modes.",
        "Original software OpenGL files can be installed alongside Mesa. Choose the active provider in Graphics settings."};
    cJSON *group = cJSON_CreateObject();
    cJSON_AddStringToObject(group, "id", extract_categories[category]);
    cJSON_AddStringToObject(group, "title", titles[category]);
    cJSON_AddStringToObject(group, "detail", details[category]);
    cJSON_AddBoolToObject(group, "defaultSelected", category == EXTRACT_CMD);
    cJSON *requirements = cJSON_AddArrayToObject(group, "requires");
    if (category == EXTRACT_SOM)
        cJSON_AddItemToArray(requirements, cJSON_CreateString("runtime"));
    return group;
}

cJSON *extract_groups(void)
{
    cJSON *groups = cJSON_CreateArray();
    for (int i = 0; i < EXTRACT_COUNT; i++)
        cJSON_AddItemToArray(groups, extract_group(i));
    return groups;
}

/* Exact names observed in the original installation packages. Do not infer
   membership from a SOM* prefix: unrelated application modules may use it. */
int extract_module_group(const char *module)
{
    if (!module)
        return -1;
    char stem[64];
    size_t length = strlen(module);
    if (length >= sizeof stem)
        return -1;
    memcpy(stem, module, length + 1);
    if (length > 4 && equal_case(stem + length - 4, ".DLL"))
        stem[length - 4] = 0;
    static const char *som[] = {"SOM",   "SOMD",   "SOMIR", "SOMU",     "SOMUC", "SOMEM",
                                "SOMTC", "SOMSEC", "SOMS",  "OREXXSOM", NULL};
    static const char *runtime[] = {"LIBCS", "LIBUNI", "UCONV", NULL};
    static const char *help[] = {"HELPMGR", "HPMGRMRI", NULL};
    static const char *network[] = {"NETAPI", "NETAPI32", NULL};
    const char *const *lists[] = {som, runtime, help, network};
    const int categories[] = {EXTRACT_SOM, EXTRACT_RUNTIME, EXTRACT_HELP, EXTRACT_NETWORK};
    for (int list = 0; list < (int)(sizeof lists / sizeof lists[0]); list++)
        for (int i = 0; lists[list][i]; i++)
        {
            if (equal_case(stem, lists[list][i]))
                return categories[list];
        }
    return -1;
}

int extract_som_file(const char *name)
{
    static const char *names[] = {
        "SOMDD.EXE",    "SOMDSVR.EXE",  "SOM.IR",      "SOMD.MSG",    "SOMK.MSG", "SOME_OS2.MSG",
        "SOMDIMPL.TOC", "SOMDIMPL.DAT", "SOMDCLS.TOC", "SOMDCLS.DAT", NULL};
    for (int i = 0; names[i]; i++)
        if (equal_case(name, names[i]))
            return 1;
    return 0;
}
