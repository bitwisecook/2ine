/* Public domain. No rights reserved. Wholly AI-authored. */
/* Digital-video device table, time formats, the video window and MCI_OPEN. */
#include "os2/mm/mmpm/mci/svmc/video.h"
#include "emulator/features.h"

LxDgvDevice lx_dgv_devices[VIDEO_DEVICES];
LxHostMonitor *lx_dgv_monitor;
static USHORT next_id = VIDEO_FIRST_ID;
static LxHostOnce once = LX_HOST_ONCE_INIT;

static void initialize(void)
{
    lx_dgv_monitor = lx_host_monitor_create();
}

int lx_dgv_initialize(void)
{
    lx_host_once(&once, initialize);
    return lx_dgv_monitor != NULL;
}

void lx_dgv_reset(LxDgvDevice *device)
{
    lx_host_monitor_lock(lx_dgv_monitor);
    memset(device, 0, sizeof *device);
    lx_host_monitor_unlock(lx_dgv_monitor);
}

LxDgvDevice *lx_dgv_find(USHORT id)
{
    for (unsigned i = 0; i < VIDEO_DEVICES; i++)
        if (lx_dgv_devices[i].id == id && id && !lx_dgv_devices[i].closing)
            return &lx_dgv_devices[i];
    return NULL;
}

uint32_t lx_dgv_position_units(LxDgvDevice *d, int64_t milliseconds)
{
    if (milliseconds < 0)
        return 0;
    uint64_t value =
        d->format == MCI_FORMAT_FRAMES
            ? (uint64_t)milliseconds * d->info.rate_numerator / (1000ull * d->info.rate_denominator)
            : (uint64_t)milliseconds;
    return value > UINT32_MAX ? UINT32_MAX : (uint32_t)value;
}

int64_t lx_dgv_milliseconds(LxDgvDevice *d, ULONG value)
{
    uint64_t result = value;
    if (d->format == MCI_FORMAT_FRAMES)
    {
        result *= 1000;
        if (result > UINT64_MAX / d->info.rate_denominator)
            return -1;
        result = result * d->info.rate_denominator / d->info.rate_numerator;
    }
    return result > UINT32_MAX ? -1 : (int64_t)result;
}

int lx_dgv_video_name(const char *name)
{
    const char *dot = name ? strrchr(name, '.') : NULL;
    static const char *extensions[] = {".avi",  ".mov", ".mpg", ".mpeg", ".mp4", ".mkv",
                                       ".webm", ".gif", ".fli", ".flc",  NULL};
    for (unsigned i = 0; dot && extensions[i]; i++)
        if (!strcasecmp(dot, extensions[i]))
            return 1;
    return 0;
}

int lx_dgv_set_window(LxDgvDevice *d, HWND window)
{
    void *target = lx_pm_video_target(window);
    if (!target)
        return 0;
    lx_pm_video_release(d->target);
    d->target = target;
    d->window = window;
    RECTL bounds;
    if (WinQueryWindowRect(window, &bounds))
        d->destination = bounds;
    return 1;
}

int lx_dgv_create_window(LxDgvDevice *d)
{
    HAB hab = WinQueryAnchorBlock(HWND_DESKTOP);
    if (!hab)
        return 0;
    WinRegisterClass(hab, "2ineDigitalVideo", WinDefWindowProc, CS_SIZEREDRAW, 0);
    ULONG flags = FCF_TITLEBAR | FCF_SYSMENU | FCF_DLGBORDER;
    HWND client = 0;
    d->frame = WinCreateStdWindow(HWND_DESKTOP, WS_VISIBLE, &flags, "2ineDigitalVideo",
                                  "Digital Video", WS_VISIBLE, 0, d->id, &client);
    if (!d->frame)
        return 0;
    RECTL rect = {0, 0, (LONG)d->info.width, (LONG)d->info.height};
    WinCalcFrameRect(d->frame, &rect, FALSE);
    WinSetWindowPos(d->frame, HWND_TOP, 100, 100, rect.xRight - rect.xLeft,
                    rect.yTop - rect.yBottom, SWP_MOVE | SWP_SIZE | SWP_SHOW);
    if (lx_dgv_set_window(d, client))
        return 1;
    WinDestroyWindow(d->frame);
    d->frame = 0;
    return 0;
}

ULONG lx_dgv_load_element(ULONG address, LxVideoDecoder **decoder, LxVideoInfo *info)
{
    char *name = lx_execution_copy_string(GProcessState.execution, address, 4096);
    if (!name)
        return MCIERR_INVALID_BUFFER;
    uint32_t error = 0;
    char *path = GProcessState.makeUnixPath(name, &error);
    free(name);
    if (!path)
        return MCIERR_FILE_NOT_FOUND;
    *decoder = lx_video_decoder_open_audio(path, info, lxRuntimeFeatureEnabled(LX_FEATURE_PCM));
    free(path);
    return *decoder ? 0 : MCIERR_DEVICE_NOT_READY;
}

ULONG lx_dgv_open(ULONG flags, unsigned char *p)
{
    if (flags & ~((ULONG)MCI_NOTIFY | MCI_WAIT | MCI_OPEN_ELEMENT | MCI_OPEN_TYPE_ID |
                  MCI_OPEN_ALIAS | MCI_OPEN_SHAREABLE | MCI_READONLY | MCI_DGV_OPEN_PARENT))
        return MCIERR_UNSUPPORTED_FUNCTION;
    LxDgvDevice *d = NULL;
    for (unsigned i = 0; i < VIDEO_DEVICES; i++)
        if (!lx_dgv_devices[i].id)
        {
            d = &lx_dgv_devices[i];
            break;
        }
    if (!d || !next_id)
        return MCIERR_OUT_OF_MEMORY;
    LxVideoInfo info = {.rate_numerator = 25, .rate_denominator = 1};
    LxVideoDecoder *decoder = NULL;
    if (flags & MCI_OPEN_ELEMENT)
    {
        ULONG error = lx_dgv_load_element(lx_guest_load_u32(p + MCI_OPEN_PARMS_ELEMENT_NAME),
                                          &decoder, &info);
        if (error)
            return error;
    }
    lx_dgv_reset(d);
    d->id = next_id++;
    d->decoder = decoder;
    d->info = info;
    d->mode = decoder ? MCI_MODE_STOP : MCI_MODE_NOT_READY;
    d->format = MCI_FORMAT_MILLISECONDS;
    d->volume = 1;
    d->until = -1;
    d->destination = (RECTL){0, 0, (LONG)info.width, (LONG)info.height};
    if ((flags & MCI_DGV_OPEN_PARENT) &&
        !lx_dgv_set_window(d, lx_guest_load_u32(p + MCI_DGV_OPEN_PARMS_PARENT)))
    {
        lx_video_decoder_close(decoder);
        lx_dgv_reset(d);
        return MCIERR_DEVICE_NOT_READY;
    }
    if (flags & MCI_OPEN_ALIAS)
    {
        char *alias = lx_execution_copy_string(GProcessState.execution,
                                               lx_guest_load_u32(p + MCI_OPEN_PARMS_ALIAS), 128);
        if (!alias || !*alias)
        {
            free(alias);
            lx_video_decoder_close(decoder);
            lx_pm_video_release(d->target);
            lx_dgv_reset(d);
            return MCIERR_INVALID_BUFFER;
        }
        snprintf(d->alias, sizeof d->alias, "%s", alias);
        free(alias);
    }
    lx_guest_store_u16(p + MCI_OPEN_PARMS_DEVICE_ID, d->id);
    return 0;
}
