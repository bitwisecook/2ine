/* Public domain. No rights reserved. Wholly AI-authored. */
/* Module services supplied by 2ine. Optional original SOM and HELPMGR are not
   substitutes for empty/fallback native modules and remain media dependencies. */
static int os2_module_matches(const char *name, const char *module)
{
    size_t length = strlen(name);
    if (length > 4 && equal_case(name + length - 4, ".dll"))
        length -= 4;
    if (strlen(module) != length)
        return 0;
    for (size_t i = 0; i < length; i++)
        if (toupper((unsigned char)name[i]) != (unsigned char)module[i])
            return 0;
    return 1;
}
static int os2_module_disabled(Context *c, const char *name)
{
    const cJSON *settings = get(c->manifest, "system");
    const char *provider = c->opengl_policy ? c->opengl_policy : field(settings, "opengl");
    if (os2_module_matches(name, "OPENGL") || os2_module_matches(name, "PGL"))
        return provider && equal_case(provider, "off");
    if (os2_module_matches(name, "DIVE"))
    {
        const cJSON *value = get(settings, "dive");
        const char *text = str(value);
        return cJSON_IsFalse(value) ||
               (text &&
                (equal_case(text, "false") || equal_case(text, "off") || equal_case(text, "no") ||
                 equal_case(text, "n") || equal_case(text, "f") || !strcmp(text, "0")));
    }
    return 0;
}
static int os2_module_provided(Context *c, const char *name)
{
    static const char *modules[] = {"DOSCALLS", "DOSCALL1", "MSG",      "NLS",     "QUECALLS",
                                    "VIOCALLS", "KBDCALLS", "SESMGR",   "PMWIN",   "PMSHAPI",
                                    "PMWP",     "PMGPI",    "TCPIP32",  "PMWINX",  "PMCTLS",
                                    "MDM",      "MMIO",     "PMSPL",    "PMDRAG",  "DIVE",
                                    "PMGRE",    "SO32DLL",  "TCP32DLL", "PMMERGE", NULL};
    if (!name)
        return 0;
    if (os2_module_disabled(c, name))
        return 1; /* Disabled services must never be restored from media. */
    if (os2_module_matches(name, "OPENGL") || os2_module_matches(name, "PGL"))
    {
        const char *provider =
            c->opengl_policy ? c->opengl_policy : field(get(c->manifest, "system"), "opengl");
        return !provider || equal_case(provider, "mesa");
    }
    size_t length = strlen(name);
    if (length > 4 && equal_case(name + length - 4, ".dll"))
        length -= 4;
    for (int i = 0; modules[i]; i++)
    {
        if (strlen(modules[i]) != length)
            continue;
        size_t at = 0;
        while (at < length && toupper((unsigned char)name[at]) == modules[i][at])
            at++;
        if (at == length)
            return 1;
    }
    return 0;
}
