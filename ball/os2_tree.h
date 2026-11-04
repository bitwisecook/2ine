/* Public domain. No rights reserved. Wholly AI-authored. */
/* Hierarchical media choices and exact planned-source selection. */
typedef struct ExtractFileSelection
{
    int explicit;
    const char **sources;
    size_t count;
} ExtractFileSelection;
static int extract_source_order(const void *first, const void *second)
{
    return strcmp(*(const char *const *)first, *(const char *const *)second);
}
static int extract_tree_file_source_order(const void *first, const void *second)
{
    return strcmp(field(*(cJSON *const *)first, "source"),
                  field(*(cJSON *const *)second, "source"));
}
static int extract_tree_path_order(const char *first, const char *second)
{
    while (*first && *second)
    {
        int a = toupper((unsigned char)*first++), b = toupper((unsigned char)*second++);
        if (a != b)
            return a - b;
    }
    return (unsigned char)*first - (unsigned char)*second;
}
static int extract_tree_file_destination_order(const void *first, const void *second)
{
    const cJSON *a = *(cJSON *const *)first, *b = *(cJSON *const *)second;
    int path = extract_tree_path_order(field(a, "path"), field(b, "path"));
    if (path)
        return path;
    double priority = get(b, "priority")->valuedouble - get(a, "priority")->valuedouble;
    return priority > 0 ? 1 : priority < 0 ? -1 : extract_tree_file_source_order(first, second);
}
static void extract_tree_prepare(Context *c, cJSON *files)
{
    int count = cJSON_GetArraySize(files), at = 0, kept = 0;
    cJSON **ordered = (cJSON **)keep(c, calloc((size_t)count + 1, sizeof *ordered));
    if (!ordered)
        return;
    cJSON *file, *previous = NULL;
    cJSON_ArrayForEach(file, files) ordered[at++] = file;
    qsort(ordered, (size_t)count, sizeof *ordered, extract_tree_file_source_order);
    for (int i = 0; i < count; i++)
        if (previous && !strcmp(field(previous, "source"), field(ordered[i], "source")))
            cJSON_Delete(cJSON_DetachItemViaPointer(files, ordered[i]));
        else
            previous = ordered[kept++] = ordered[i];
    qsort(ordered, (size_t)kept, sizeof *ordered, extract_tree_file_destination_order);
    previous = NULL;
    double priority = 0;
    for (int i = 0; i < kept; i++)
    {
        file = ordered[i];
        if (!previous || extract_tree_path_order(field(previous, "path"), field(file, "path")))
            priority = get(file, "priority")->valuedouble;
        put(file, "preferred", cJSON_CreateBool(get(file, "priority")->valuedouble == priority));
        previous = file;
    }
}
static int extract_selection(Context *c, const cJSON *request, const cJSON *plan,
                             ExtractFileSelection *selection)
{
    const cJSON *requested = get(request, "fileSelection");
    if (!requested)
        return 1;
    selection->explicit = 1;
    if (!array_strings(c, requested))
        return 0;
    int count = cJSON_GetArraySize(requested), planned = cJSON_GetArraySize(get(plan, "files"));
    if (count > 100000)
        return fail(c, "The file selection exceeds 100000 entries");
    const char **available = (const char **)keep(c, calloc((size_t)planned + 1, sizeof *available));
    selection->sources =
        (const char **)keep(c, calloc((size_t)count + 1, sizeof *selection->sources));
    if (!available || !selection->sources)
        return 0;
    int at = 0;
    const cJSON *item;
    cJSON_ArrayForEach(item, get(plan, "files")) available[at++] = field(item, "source");
    qsort(available, (size_t)planned, sizeof *available, extract_source_order);
    cJSON_ArrayForEach(item, requested)
    {
        const char *source = str(item);
        if (!bsearch(&source, available, (size_t)planned, sizeof *available, extract_source_order))
            return fail(c, "A selected file is not in the current media plan: %s", source);
        selection->sources[selection->count++] = source;
    }
    qsort(selection->sources, selection->count, sizeof *selection->sources, extract_source_order);
    return 1;
}
static int extract_file_selected(const ExtractFileSelection *selection, const int *categories,
                                 const cJSON *file)
{
    if (selection->explicit)
    {
        const char *source = field(file, "source");
        return bsearch(&source, selection->sources, selection->count, sizeof *selection->sources,
                       extract_source_order) != NULL;
    }
    int category = extract_category(field(file, "category"));
    return categories[EXTRACT_FULL] || (category >= 0 && categories[category]);
}
static cJSON *extract_tree_branch(cJSON *parent, const char *id, const char *title,
                                  const char *category)
{
    cJSON *children = get(parent, "children"), *item;
    cJSON_ArrayForEach(item, children) if (!strcmp(field(item, "id"), id)) return item;
    cJSON *branch = cJSON_CreateObject();
    cJSON_AddStringToObject(branch, "id", id);
    cJSON_AddStringToObject(branch, "title", title);
    cJSON_AddStringToObject(branch, "kind", "group");
    cJSON_AddStringToObject(branch, "category", category);
    cJSON_AddArrayToObject(branch, "children");
    cJSON_AddItemToArray(children, branch);
    return branch;
}
static void extract_tree_provider(cJSON *group, const char *id, const char *title,
                                  const char *selection, const char *setting, int native_selected,
                                  int choice_supported)
{
    cJSON *provider = cJSON_CreateObject();
    cJSON_AddStringToObject(provider, "title", title);
    cJSON_AddStringToObject(provider, "selection", selection);
    cJSON_AddStringToObject(provider, "setting", setting);
    cJSON_AddStringToObject(provider, "group", field(group, "category"));
    cJSON_AddBoolToObject(provider, "nativeSelected", native_selected);
    cJSON_AddBoolToObject(provider, "choiceSupported", choice_supported);
    cJSON_AddItemToObject(group, "provider", provider);
    cJSON *link = cJSON_Duplicate(provider, 1);
    cJSON_AddStringToObject(link, "id", id);
    cJSON_AddStringToObject(link, "kind", "setting");
    cJSON_AddBoolToObject(link, "selectable", 0);
    cJSON_AddBoolToObject(link, "disabled", 0);
    cJSON_AddItemToArray(get(group, "children"), link);
}
static int extract_tree_has_files(const cJSON *node)
{
    if (equal_case(field(node, "kind"), "file"))
        return 1;
    const cJSON *child;
    cJSON_ArrayForEach(child, get(node, "children")) if (extract_tree_has_files(child)) return 1;
    return 0;
}
static void extract_tree_disable(cJSON *node, int disabled)
{
    int setting = equal_case(field(node, "kind"), "setting");
    put(node, "disabled",
        cJSON_CreateBool(!setting && (disabled || cJSON_IsTrue(get(node, "disabled")))));
    if (!setting)
        put(node, "selectable", cJSON_CreateBool(!cJSON_IsTrue(get(node, "disabled"))));
    cJSON *child;
    cJSON_ArrayForEach(child, get(node, "children")) extract_tree_disable(child, disabled);
}
static void extract_tree_provider_policy(cJSON *group)
{
    cJSON *provider = get(group, "provider");
    int available = extract_tree_has_files(group);
    cJSON_AddBoolToObject(provider, "originalAvailable", available);
    cJSON *child;
    cJSON_ArrayForEach(child,
                       get(group, "children")) if (equal_case(field(child, "kind"), "setting"))
        cJSON_AddBoolToObject(child, "originalAvailable", available);
    extract_tree_disable(group, cJSON_IsTrue(get(provider, "nativeSelected")));
}
static cJSON *extract_tree_directories(Context *c, cJSON *parent, const char *path,
                                       const char *category)
{
    const char *name_space = field(parent, "id");
    const char *end = strrchr(path, '\\');
    if (!end || strlen(path) < 3 || path[1] != ':' || path[2] != '\\')
        return parent;
    for (const char *part = path; part <= end;)
    {
        const char *slash = part == path ? path + 2 : strchr(part, '\\');
        if (!slash || slash > end)
            break;
        size_t length = (size_t)(slash - path) + (part == path ? 1 : 0);
        char *directory = format(c, "%.*s", (int)length, path);
        char *title = format(c, "%.*s", (int)(slash - part) + (part == path ? 1 : 0), part);
        if (!directory || !title)
            return parent;
        for (char *at = directory; *at; at++)
            *at = (char)toupper((unsigned char)*at);
        char *id = format(c, "%s.directory.%s", name_space, directory);
        if (!id)
            return parent;
        parent = extract_tree_branch(parent, id, title, category);
        put(parent, "kind", cJSON_CreateString("directory"));
        put(parent, "path", cJSON_CreateString(directory));
        put(parent, "selectable", cJSON_CreateBool(1));
        put(parent, "disabled", cJSON_CreateBool(0));
        part = slash + 1;
    }
    return parent;
}
static cJSON *extract_tree(Context *c, cJSON *files)
{
    extract_tree_prepare(c, files);
    cJSON *root = extract_group(EXTRACT_FULL);
    put(root, "id", cJSON_CreateString("all"));
    cJSON_AddStringToObject(root, "kind", "group");
    cJSON_AddStringToObject(root, "category", "full");
    cJSON *children = cJSON_AddArrayToObject(root, "children");
    cJSON *groups[EXTRACT_COUNT] = {0};
    for (int i = 0; i < EXTRACT_COUNT; i++)
        if (i != EXTRACT_FULL)
        {
            groups[i] = extract_group(i);
            cJSON_AddStringToObject(groups[i], "kind", "group");
            cJSON_AddStringToObject(groups[i], "category", extract_categories[i]);
            cJSON_AddArrayToObject(groups[i], "children");
            cJSON_AddItemToArray(children, groups[i]);
        }
    groups[EXTRACT_FULL] = extract_tree_branch(root, "full.other", "Other OS/2 Files", "full");
    const char *opengl = field(get(c->manifest, "system"), "opengl");
    if (!opengl || !*opengl)
        opengl = "mesa";
    extract_tree_provider(groups[EXTRACT_OPENGL], "opengl.provider", "Selected OpenGL Provider",
                          opengl, "graphics.opengl", equal_case(opengl, "mesa"), 1);
    extract_tree_provider(groups[EXTRACT_PRINTERS], "printers.provider",
                          "Printing Provided by 2ine", "2ine", "printing", 1, 0);
    extract_tree_provider(groups[EXTRACT_MULTIMEDIA], "multimedia.provider",
                          "Audio and Video Provided by 2ine", "2ine", "graphics", 1, 0);
    const cJSON *file;
    cJSON_ArrayForEach(file, files)
    {
        if (cJSON_IsTrue(get(file, "provided")))
            continue;
        int category = extract_category(field(file, "category"));
        if (category < 0)
            category = EXTRACT_FULL;
        cJSON *parent = groups[category];
        const char *path = field(file, "path"), *base = strrchr(path, '\\');
        base = base ? base + 1 : path;
        const char *id = NULL, *title = NULL;
        if (category == EXTRACT_FONTS)
        {
            if (extract_extension(base, ".FON") || extract_extension(base, ".FNT"))
                id = "fonts.bitmap", title = "Bitmap Fonts";
            else if (extract_extension(base, ".TTF") || extract_extension(base, ".TTC"))
                id = "fonts.truetype", title = "TrueType Fonts";
            else if (extract_extension(base, ".OTF"))
                id = "fonts.opentype", title = "OpenType Fonts";
            else if (extract_extension(base, ".PFB") || extract_extension(base, ".PFA"))
                id = "fonts.postscript", title = "PostScript Fonts";
            else
                id = "fonts.metrics", title = "Font Metrics";
        }
        else if (category == EXTRACT_HELP)
        {
            if (equal_case(base, "HELPMGR.DLL") || equal_case(base, "HPMGRMRI.DLL"))
                id = "help.engine", title = "Help Manager and Resources";
            else if (extract_extension(base, ".EXE"))
                id = "help.viewers", title = "Help and Book Viewers";
            else if (extract_extension(base, ".INF"))
                id = "help.references", title = "Reference Books (INF)";
            else
                id = "help.books", title = "Help Books (HLP)";
        }
        else if (category == EXTRACT_SOM)
        {
            if (extract_extension(base, ".DLL"))
                id = "som.libraries", title = "SOM Libraries";
            else if (extract_extension(base, ".EXE"))
                id = "som.services", title = "DSOM Service Programs";
            else if (extract_extension(base, ".MSG"))
                id = "som.messages", title = "SOM Messages";
            else
                id = "som.repositories", title = "SOM Repositories";
        }
        else if (category == EXTRACT_ASSETS)
        {
            if (extract_video(base))
                id = "assets.video", title = "Sample Videos";
            else if (extract_extension(base, ".WAV") || extract_extension(base, ".MID"))
                id = "assets.sounds", title = "Sounds and Music";
            else
                id = "assets.images", title = "Images and Desktop Artwork";
        }
        else if (category == EXTRACT_LEGACY)
        {
            if (strstr(path, "WINOS2") || strstr(path, "winos2"))
                id = "legacy.windows", title = "Win-OS/2 Files";
            else
                id = "legacy.dos", title = "DOS Compatibility Files";
        }
        if (id)
            parent = extract_tree_branch(parent, id, title, extract_categories[category]);
        parent = extract_tree_directories(c, parent, path, extract_categories[category]);
        cJSON *leaf = cJSON_Duplicate(file, 1);
        cJSON_AddStringToObject(leaf, "id", field(file, "source"));
        cJSON_AddStringToObject(leaf, "title", base);
        cJSON_AddStringToObject(leaf, "kind", "file");
        put(leaf, "disabled",
            cJSON_CreateBool(category != EXTRACT_OPENGL && cJSON_IsTrue(get(file, "disabled"))));
        cJSON_AddItemToArray(get(parent, "children"), leaf);
    }
    extract_tree_provider_policy(groups[EXTRACT_OPENGL]);
    extract_tree_provider_policy(groups[EXTRACT_PRINTERS]);
    extract_tree_provider_policy(groups[EXTRACT_MULTIMEDIA]);
    return root;
}
