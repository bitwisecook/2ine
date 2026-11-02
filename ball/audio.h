/* Public domain. No rights reserved. Wholly AI-authored. */
/* Private, portable bank installation and per-ball MIDI preferences. */
#include "media.h"
static int plan_copy(Context *, cJSON *, const char *, const char *, unsigned);
static const char *audio_profiles[][2] = {{"gm", "General MIDI"},
                                          {"gm2", "General MIDI 2"},
                                          {"gs", "Roland GS"},
                                          {"xg", "Yamaha XG"},
                                          {"mt32", "Roland MT-32 channel layout"},
                                          {NULL, NULL}};

static cJSON *audio_defaults(void)
{
    cJSON *settings = cJSON_CreateObject();
    cJSON_AddStringToObject(settings, "profile", "gm");
    cJSON_AddStringToObject(settings, "bank", "default");
    cJSON *channels = cJSON_AddArrayToObject(settings, "channels");
    for (int i = 0; i < 16; i++)
    {
        cJSON *channel = cJSON_CreateObject();
        cJSON_AddNumberToObject(channel, "target", i + 1);
        cJSON_AddStringToObject(channel, "bank", "");
        cJSON_AddNumberToObject(channel, "program", -1);
        cJSON_AddNumberToObject(channel, "bankNumber", -1);
        cJSON_AddNumberToObject(channel, "percussion", -1);
        cJSON_AddBoolToObject(channel, "muted", 0);
        cJSON_AddItemToArray(channels, channel);
    }
    return settings;
}
static const char *audio_format(const char *path)
{
    const char *dot = strrchr(path, '.');
    if (!dot)
        return NULL;
    const char *formats[] = {"sf2", "sf3", "sfz", "dls", NULL};
    for (int i = 0; formats[i]; i++)
        if (equal_case(dot + 1, formats[i]))
            return formats[i];
    return NULL;
}
static uint32_t audio_u32(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static int audio_header(Context *c, const char *path, const char *kind)
{
    if (!tb_is_file(path) || tb_is_link(path))
        return fail(c, "A sound bank must be a regular file");
    FILE *file = tb_fopen(path, "rb");
    if (!file)
        return fail(c, "Cannot read sound bank: %s", path);
    int64_t size = 0;
    int mode;
    unsigned char header[12];
    int ok = tb_file_info(file, &size, &mode) && size > 0 && size <= INT64_C(2147483648);
    if (ok && strcmp(kind, "sfz"))
        ok = fread(header, 1, 12, file) == 12 && !memcmp(header, "RIFF", 4) &&
             !memcmp(header + 8, !strcmp(kind, "dls") ? "DLS " : "sfbk", 4) &&
             audio_u32(header + 4) >= 4 && (uint64_t)audio_u32(header + 4) + 8 <= (uint64_t)size;
    fclose(file);
    return ok ? 1 : fail(c, "Invalid, empty or oversized %s sound bank", kind);
}
static const cJSON *audio_bank(Context *c, const char *id)
{
    cJSON *bank;
    cJSON_ArrayForEach(
        bank, get(c->manifest, "soundBanks")) if (field(bank, "id") &&
                                                  !strcmp(field(bank, "id"), id)) return bank;
    return NULL;
}
static char *audio_bank_path(Context *c, const char *id, const char *build)
{
    if (!id || !*id || !strcmp(id, "default"))
        return join(c, build, "soundfonts/FluidR3_GM.sf2");
    const cJSON *bank = audio_bank(c, id);
    const char *relative = bank ? field(bank, "file") : NULL;
    if (!relative || strncmp(relative, "soundfonts/", 11) || strpbrk(relative, "\\:\r\n"))
    {
        fail(c, "Installed sound bank is unavailable: %s", id);
        return NULL;
    }
    for (const char *part = relative; *part;)
    {
        const char *end = strchr(part, '/');
        size_t length = end ? (size_t)(end - part) : strlen(part);
        if (!length || (length == 1 && part[0] == '.') ||
            (length == 2 && part[0] == '.' && part[1] == '.'))
        {
            fail(c, "Invalid installed sound-bank path");
            return NULL;
        }
        if (!end)
            break;
        part = end + 1;
    }
    return join(c, c->ball, relative);
}
static int audio_validate(Context *c, const cJSON *settings);
static cJSON *audio_snapshot(Context *c)
{
    if (get(c->manifest, "soundBanks") && !cJSON_IsArray(get(c->manifest, "soundBanks")))
    {
        fail(c, "Invalid sound-bank library");
        return NULL;
    }
    const cJSON *saved = get(c->manifest, "audio");
    if (saved && !audio_validate(c, saved))
        return NULL;
    cJSON *result = cJSON_CreateObject();
    cJSON_AddItemToObject(result, "settings", saved ? cJSON_Duplicate(saved, 1) : audio_defaults());
    cJSON *banks = cJSON_AddArrayToObject(result, "banks");
    cJSON *builtin = cJSON_CreateObject();
    cJSON_AddStringToObject(builtin, "id", "default");
    cJSON_AddStringToObject(builtin, "name", "FluidR3 GM 3.1 (bundled)");
    cJSON_AddStringToObject(builtin, "format", "sf2");
    cJSON_AddStringToObject(builtin, "author", "Frank Wen and contributors");
    cJSON_AddStringToObject(builtin, "license", "MIT");
    cJSON_AddItemToArray(banks, builtin);
    cJSON *bank;
    cJSON_ArrayForEach(bank, get(c->manifest, "soundBanks"))
        cJSON_AddItemToArray(banks, cJSON_Duplicate(bank, 1));
    cJSON *profiles = cJSON_AddArrayToObject(result, "profiles");
    for (int i = 0; audio_profiles[i][0]; i++)
    {
        cJSON *profile = cJSON_CreateObject();
        cJSON_AddStringToObject(profile, "id", audio_profiles[i][0]);
        cJSON_AddStringToObject(profile, "name", audio_profiles[i][1]);
        cJSON *defaults = audio_defaults();
        cJSON *channels = cJSON_DetachItemFromObjectCaseSensitive(defaults, "channels");
        cJSON_Delete(defaults);
        if (!strcmp(audio_profiles[i][0], "mt32"))
        {
            for (int channel = 0; channel < 16; channel++)
            {
                cJSON *mapping = cJSON_GetArrayItem(channels, channel);
                if (channel >= 1 && channel <= 8)
                    put(mapping, "target", cJSON_CreateNumber(channel));
                put(mapping, "muted", cJSON_CreateBool(channel == 0 || channel > 9));
            }
        }
        cJSON_AddItemToObject(profile, "channels", channels);
        cJSON_AddItemToArray(profiles, profile);
    }
    return result;
}
static int audio_number(Context *c, const cJSON *object, const char *key, int min, int max)
{
    const cJSON *value = get(object, key);
    if (!cJSON_IsNumber(value) || !isfinite(value->valuedouble) ||
        value->valuedouble != value->valueint || value->valueint < min || value->valueint > max)
        return fail(c, "Invalid MIDI channel %s", key);
    return 1;
}
static int audio_validate(Context *c, const cJSON *settings)
{
    const char *profile = field(settings, "profile"), *bank = field(settings, "bank");
    int known = 0;
    for (int i = 0; profile && audio_profiles[i][0]; i++)
        if (!strcmp(profile, audio_profiles[i][0]))
            known = 1;
    if (!known || !bank || (strcmp(bank, "default") && !audio_bank(c, bank)))
        return fail(c, "Choose a MIDI mapping and an installed sound bank");
    const cJSON *channels = get(settings, "channels");
    if (!cJSON_IsArray(channels) || cJSON_GetArraySize(channels) != 16)
        return fail(c, "A MIDI mapping must contain all 16 channels");
    cJSON *channel;
    cJSON_ArrayForEach(channel, channels)
    {
        const char *id = field(channel, "bank");
        if (!id || (*id && strcmp(id, "default") && !audio_bank(c, id)) ||
            !cJSON_IsBool(get(channel, "muted")))
            return fail(c, "Invalid sound bank or mute setting in MIDI channel");
        if (!audio_number(c, channel, "target", 1, 16) ||
            !audio_number(c, channel, "program", -1, 127) ||
            !audio_number(c, channel, "bankNumber", -1, 16383) ||
            !audio_number(c, channel, "percussion", -1, 1))
            return 0;
    }
    return 1;
}
static cJSON *audio_save(Context *c, const cJSON *request)
{
    const cJSON *settings = get(request, "settings");
    if (!audio_validate(c, settings))
        return NULL;
    const cJSON *old = get(c->manifest, "audio");
    cJSON *merged = old ? cJSON_Duplicate(old, 1) : audio_defaults();
    cJSON *entry;
    cJSON_ArrayForEach(entry, settings)
    {
        if (strcmp(entry->string, "channels"))
            put(merged, entry->string, cJSON_Duplicate(entry, 1));
    }
    cJSON *channels = cJSON_CreateArray();
    for (int i = 0; i < 16; i++)
    {
        const cJSON *previous = old ? cJSON_GetArrayItem(get(old, "channels"), i) : NULL;
        cJSON *channel = previous ? cJSON_Duplicate(previous, 1) : cJSON_CreateObject();
        cJSON_ArrayForEach(entry, cJSON_GetArrayItem(get(settings, "channels"), i))
            put(channel, entry->string, cJSON_Duplicate(entry, 1));
        cJSON_AddItemToArray(channels, channel);
    }
    put(merged, "channels", channels);
    put(c->manifest, "audio", merged);
    return save_manifest(c) ? audio_snapshot(c) : NULL;
}
static int audio_config(Context *c, const char *build, cJSON *env)
{
    cJSON *defaults = NULL;
    const cJSON *settings = get(c->manifest, "audio");
    if (!settings)
        settings = defaults = audio_defaults();
    if (!audio_validate(c, settings))
    {
        cJSON_Delete(defaults);
        return 0;
    }
    char *bank = audio_bank_path(c, field(settings, "bank"), build);
    if (bank && strpbrk(bank, "\r\n"))
        fail(c, "A sound-bank path must fit on one line");
    char *text = bank ? format(c, "schema=2\nsoundfont=%s\nmidi_profile=%s\n", bank,
                               field(settings, "profile"))
                      : NULL;
    const cJSON *channels = get(settings, "channels");
    for (int i = 0; text && i < 16; i++)
    {
        const cJSON *channel = cJSON_GetArrayItem(channels, i);
        const char *id = field(channel, "bank");
        char *path = *id ? audio_bank_path(c, id, build) : bank;
        if (!path || strpbrk(path, "\r\n"))
        {
            if (!c->error)
                fail(c, "A sound-bank path must fit on one line");
            break;
        }
        text = format(c, "%schannel_%d=%d,%d,%d,%d,%d\nchannel_%d_soundfont=%s\n", text, i + 1,
                      get(channel, "target")->valueint, get(channel, "program")->valueint,
                      get(channel, "bankNumber")->valueint, get(channel, "percussion")->valueint,
                      cJSON_IsTrue(get(channel, "muted")), i + 1, path);
    }
    cJSON_Delete(defaults);
    char *path = join(c, c->ball, "audio.conf");
    if (c->error || !text || !tb_atomic_write(path, text, strlen(text)))
        return c->error ? 0 : fail(c, "Cannot write ball audio configuration");
    put(env, "TWOINE_AUDIO_CONFIG", cJSON_CreateString(path));
    put(env, "TWOINE_SOUNDFONT", cJSON_CreateString(bank));
    return 1;
}
static int audio_tree_hash(Context *c, const char *path, const char *relative, SHA256_CTX *state,
                           unsigned depth, unsigned *count, uint64_t *bytes)
{
    if (depth > 64 || ++*count > 10000 || tb_is_link(path))
        return fail(
            c, "Sound-bank folders must contain at most 10000 regular files/folders and no links");
    sha256_update(state, (const BYTE *)relative, strlen(relative) + 1);
    if (tb_is_dir(path))
    {
        size_t n = 0;
        char **names = tb_names(path, &n);
        if (!names && n == (size_t)-1)
            return fail(c, "Cannot read sound-bank folder");
        if (n > 1)
            qsort(names, n, sizeof *names, compare_names);
        int ok = 1;
        for (size_t i = 0; i < n && ok; i++)
            ok = audio_tree_hash(c, join(c, path, names[i]), join(c, relative, names[i]), state,
                                 depth + 1, count, bytes);
        tb_names_free(names, n);
        return ok;
    }
    if (!tb_is_file(path))
        return fail(c, "Sound-bank folders cannot contain special files");
    FILE *file = tb_fopen(path, "rb");
    int64_t size = -1;
    int mode;
    int ok = file && tb_file_info(file, &size, &mode) && size >= 0;
    if (file)
        fclose(file);
    if (!ok || (*bytes += (uint64_t)size) > UINT64_C(2147483648))
        return fail(c, "Sound-bank folder exceeds the 2 GiB limit or contains a special file");
    unsigned char hash[32];
    if (!digest_file(c, path, hash))
        return 0;
    sha256_update(state, hash, sizeof hash);
    return 1;
}
static cJSON *audio_import(Context *c, const cJSON *request)
{
    const char *raw = field(request, "path");
    if (!raw || tb_is_link(raw))
    {
        fail(c, "Choose a regular sound bank file");
        return NULL;
    }
    char *source = resolve_path(c, raw, 1);
    const char *kind = source ? audio_format(source) : NULL;
    if (!kind || !audio_header(c, source, kind))
    {
        if (!c->error)
            fail(c, "Choose an SF2, SF3, SFZ or DLS bank");
        return NULL;
    }
    unsigned char digest[32];
    if (!digest_file(c, source, digest))
        return NULL;
    char *file_hash = hex(c, digest);
    char *parent = keep(c, strdup(source));
    char *slash = strrchr(parent, '/'), *backslash = strrchr(parent, '\\');
    if (backslash && (!slash || backslash > slash))
        slash = backslash;
    if (!slash)
    {
        fail(c, "Cannot find the bank's containing folder");
        return NULL;
    }
    const char *name = source + (slash - parent) + 1;
    *slash = 0;
    char *folder = field(request, "folder") ? resolve_path(c, field(request, "folder"), 1) : NULL;
    if (field(request, "folder") && (!folder || !tb_is_dir(folder) || !inside(source, folder)))
    {
        fail(c, "Choose a containing sound-bank folder");
        return NULL;
    }
    if (!folder && !strcmp(kind, "sfz"))
        folder = parent;
    if (folder)
    {
        if (inside(c->ball, folder))
        {
            fail(c, "Choose an instrument folder that does not contain this ball");
            return NULL;
        }
        SHA256_CTX state;
        sha256_init(&state);
        unsigned count = 0;
        uint64_t bytes = 0;
        if (!audio_tree_hash(c, folder, "", &state, 0, &count, &bytes))
            return NULL;
        sha256_final(&state, digest);
    }
    char *id = hex(c, digest);
    if (audio_bank(c, id))
        return audio_snapshot(c);
    if (strpbrk(name, "\r\n:"))
    {
        fail(c, "Sound bank filename contains unsupported characters");
        return NULL;
    }
    char *directory = join(c, c->ball, "soundfonts");
    if (!tb_mkdirs(directory))
    {
        fail(c, "Cannot create the ball's sound-bank library");
        return NULL;
    }
    char *work = keep(c, tb_archive_workspace(directory));
    if (!work)
    {
        fail(c, "Cannot create a sound-bank import workspace");
        return NULL;
    }
    cJSON *plan = cJSON_CreateArray();
    /* SFZ references samples/includes relative to the instrument folder. */
    const char *input = folder ? folder : source;
    char *target = join(c, work, folder ? "content" : name);
    int ok = plan_copy(c, plan, input, target, 0);
    uint64_t total = 0;
    cJSON *entry;
    cJSON_ArrayForEach(entry, plan)
    {
        if (!ok)
            break;
        const char *src = field(entry, "source"), *dst = field(entry, "destination");
        if (!cJSON_IsTrue(get(entry, "directory")))
        {
            FILE *file = tb_fopen(src, "rb");
            int64_t size = -1;
            int mode;
            ok = file && tb_file_info(file, &size, &mode) && size >= 0;
            if (file)
                fclose(file);
            if (ok)
                total += (uint64_t)size;
            ok = ok && total <= UINT64_C(2147483648);
        }
        if (ok)
            ok =
                !tb_is_link(src) &&
                (cJSON_IsTrue(get(entry, "directory")) ? tb_mkdir_new(dst) : tb_copy_new(src, dst));
    }
    cJSON_Delete(plan);
    char *installed = join(c, directory, id);
    if (ok)
        ok = !tb_is_dir(installed) && tb_replace(work, installed);
    if (!ok)
    {
        tb_remove_tree(work);
        if (!c->error)
            fail(c, "Cannot install bank folder (2 GiB limit); original files were kept");
        return NULL;
    }
    cJSON *record = cJSON_CreateObject();
    cJSON_AddStringToObject(record, "id", id);
    cJSON_AddStringToObject(record, "name", name);
    cJSON_AddStringToObject(record, "format", kind);
    cJSON_AddStringToObject(record, "sha256", file_hash);
    cJSON_AddStringToObject(record, "contentSHA256", id);
    cJSON_AddStringToObject(record, "file",
                            format(c, "soundfonts/%s/%s%s", id, folder ? "content/" : "",
                                   folder ? source + strlen(folder) + 1 : name));
    cJSON_AddStringToObject(record, "license", "User supplied; retains its original license");
    cJSON *banks = get(c->manifest, "soundBanks");
    if (!banks)
        banks = cJSON_AddArrayToObject(c->manifest, "soundBanks");
    cJSON_AddItemToArray(banks, record);
    if (!save_manifest(c))
    {
        tb_remove_tree(installed);
        return NULL;
    }
    return audio_snapshot(c);
}

static int audio_extract(Context *c, const char *source, const char *destination, cJSON *banks,
                         unsigned depth, unsigned *files, uint64_t *bytes)
{
    if (depth > 64 || !tb_mkdirs(destination))
        return fail(c, "Cannot create sound-bank extraction folder");
    size_t count = 0;
    TbMediaEntry *entries = tb_media_entries(source, &count);
    if (!entries && count == (size_t)-1)
        return fail(c, "Cannot read sound-bank archive");
    int ok = 1;
    for (size_t i = 0; i < count && ok; i++)
    {
        const char *name = entries[i].name;
        if (++*files > 10000 || !*name || !strcmp(name, ".") || !strcmp(name, "..") ||
            strpbrk(name, "/\\:\r\n"))
        {
            ok = fail(c, "Invalid or oversized sound-bank archive");
            break;
        }
        char *member = keep(c, tb_media_child(source, name));
        char *path = join(c, destination, name);
        if (!member)
        {
            ok = fail(c, "Cannot resolve sound-bank archive member");
            break;
        }
        if (entries[i].directory)
            ok = audio_extract(c, member, path, banks, depth + 1, files, bytes);
        else
        {
            FILE *input = tb_media_fopen(member);
            FILE *output = !tb_is_file(path) ? tb_fopen(path, "wb") : NULL;
            unsigned char buffer[8192];
            size_t n;
            ok = input && output;
            while (ok && (n = fread(buffer, 1, sizeof buffer, input)))
            {
                *bytes += n;
                ok = *bytes <= UINT64_C(2147483648) && fwrite(buffer, 1, n, output) == n;
            }
            if (input)
            {
                ok = ok && !ferror(input);
                fclose(input);
            }
            if (output && fclose(output))
                ok = 0;
            if (ok && audio_format(name))
                cJSON_AddItemToArray(banks, cJSON_CreateString(path));
            if (!ok)
                fail(c,
                     "Cannot extract bank (2 GiB limit); links and special files are unsupported");
        }
    }
    tb_media_entries_free(entries, count);
    return ok;
}
static cJSON *audio_import_archive(Context *c, const cJSON *request)
{
    const char *raw = field(request, "path");
    const char *dot = raw ? strrchr(raw, '.') : NULL;
    if (!dot || !equal_case(dot, ".zip"))
        return audio_import(c, request);
    if (!tb_is_file(raw) || tb_is_link(raw))
    {
        fail(c, "Choose a regular ZIP file");
        return NULL;
    }
    char *directory = join(c, c->ball, "soundfonts");
    if (!tb_mkdirs(directory))
    {
        fail(c, "Cannot create sound-bank library");
        return NULL;
    }
    char *work = keep(c, tb_archive_workspace(directory));
    char *source = keep(c, tb_media_root(raw));
    if (!work || !source)
    {
        if (work)
            tb_remove_tree(work);
        fail(c, "Cannot open sound-bank ZIP");
        return NULL;
    }
    cJSON *banks = cJSON_CreateArray(), *result = NULL;
    unsigned files = 0;
    uint64_t bytes = 0;
    if (audio_extract(c, source, work, banks, 0, &files, &bytes))
    {
        if (cJSON_GetArraySize(banks) != 1)
            fail(c, "ZIP contains %d banks. Extract it and choose the desired instrument file.",
                 cJSON_GetArraySize(banks));
        else
        {
            cJSON *import = cJSON_CreateObject();
            cJSON_AddStringToObject(import, "path", str(cJSON_GetArrayItem(banks, 0)));
            cJSON_AddStringToObject(import, "folder", work);
            result = audio_import(c, import);
            cJSON_Delete(import);
        }
    }
    cJSON_Delete(banks);
    tb_remove_tree(work);
    return result;
}
