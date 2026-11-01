/* Native Presentation Manager / PGL window and input.
 * Public domain. No rights reserved.
 */
#define INCL_WIN
#define INCL_GPI
#define INCL_DOS
#include <os2.h>
#include "quakedef.h"
#include <GL/pgl.h>
#include "gl_os2.h"
#include <ctype.h>

unsigned short d_8to16table[256];
unsigned d_8to24table[256];
unsigned char d_15to8table[65536];
int texture_mode = GL_LINEAR;
int texture_extension_number = 1;
float gldepthmin, gldepthmax;
cvar_t gl_ztrick = {"gl_ztrick", "0"};
const char *gl_vendor, *gl_renderer, *gl_version, *gl_extensions;
qboolean isPermedia = false, gl_mtexable = false;
void (*qglColorTableEXT)(int, int, int, int, int, const void *);

static HAB hab;
static HMQ queue;
static HWND frame, window;
static HGC context;
static int width = 640, height = 480;
static int mouse_x, mouse_y, active;
static int scan_keys[256];

static int TranslateKey(USHORT flags, USHORT character, USHORT vk, UCHAR scan)
{
    int key = 0;
    if (flags & KC_VIRTUALKEY)
        switch (vk)
        {
            case VK_UP:
                key = K_UPARROW;
                break;
            case VK_DOWN:
                key = K_DOWNARROW;
                break;
            case VK_LEFT:
                key = K_LEFTARROW;
                break;
            case VK_RIGHT:
                key = K_RIGHTARROW;
                break;
            case VK_ESC:
                key = K_ESCAPE;
                break;
            case VK_ENTER:
            case VK_NEWLINE:
                key = K_ENTER;
                break;
            case VK_TAB:
                key = K_TAB;
                break;
            case VK_BACKSPACE:
                key = K_BACKSPACE;
                break;
            case VK_SHIFT:
                key = K_SHIFT;
                break;
            case VK_CTRL:
                key = K_CTRL;
                break;
            case VK_ALT:
            case VK_ALTGRAF:
                key = K_ALT;
                break;
            case VK_INSERT:
                key = K_INS;
                break;
            case VK_DELETE:
                key = K_DEL;
                break;
            case VK_PAGEDOWN:
                key = K_PGDN;
                break;
            case VK_PAGEUP:
                key = K_PGUP;
                break;
            case VK_HOME:
                key = K_HOME;
                break;
            case VK_END:
                key = K_END;
                break;
            case VK_PAUSE:
                key = K_PAUSE;
                break;
            default:
                if (vk >= VK_F1 && vk <= VK_F12)
                    key = K_F1 + vk - VK_F1;
                break;
        }
    if (!key && (flags & KC_CHAR) && character > 0 && character < 128)
        key = tolower(character);
    if (flags & KC_KEYUP)
    {
        if (scan_keys[scan])
            key = scan_keys[scan];
        scan_keys[scan] = 0;
    }
    else if (key)
        scan_keys[scan] = key;
    return key;
}

static MRESULT EXPENTRY WindowProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
    int key, button;
    switch (msg)
    {
        case WM_CLOSE:
            Cbuf_AddText("quit\n");
            return 0;
        case WM_ERASEBACKGROUND:
            return (MRESULT)FALSE;
        case WM_PAINT:
        {
            HPS ps = WinBeginPaint(hwnd, 0, NULL);
            WinEndPaint(ps);
            return 0;
        }
        case WM_FOCUSCHANGE:
        case WM_SETFOCUS:
            active = SHORT1FROMMP(mp2) != 0;
            if (!active)
            {
                Key_ClearStates();
                mouse_x = mouse_y = 0;
            }
            return 0;
        case WM_CHAR:
            key = TranslateKey(SHORT1FROMMP(mp1), SHORT1FROMMP(mp2), SHORT2FROMMP(mp2),
                               CHAR4FROMMP(mp1));
            if (key)
            {
                Key_Event(key, !(SHORT1FROMMP(mp1) & KC_KEYUP));
                if (COM_CheckParm("-inputlog"))
                    Sys_Printf("PM key=%d down=%d dest=%d\n", key, !(SHORT1FROMMP(mp1) & KC_KEYUP),
                               key_dest);
            }
            return 0;
        case WM_BUTTON1DOWN:
        case WM_BUTTON1UP:
        case WM_BUTTON2DOWN:
        case WM_BUTTON2UP:
        case WM_BUTTON3DOWN:
        case WM_BUTTON3UP:
            button = msg == WM_BUTTON1DOWN || msg == WM_BUTTON1UP   ? K_MOUSE1
                     : msg == WM_BUTTON2DOWN || msg == WM_BUTTON2UP ? K_MOUSE2
                                                                    : K_MOUSE3;
            Key_Event(button,
                      msg == WM_BUTTON1DOWN || msg == WM_BUTTON2DOWN || msg == WM_BUTTON3DOWN);
            if (COM_CheckParm("-inputlog"))
                Sys_Printf("PM button=%d msg=%lu\n", button, msg);
            return 0;
        case WM_MOUSEMOVE:
            if (active && key_dest == key_game && !COM_CheckParm("-nomouse"))
            {
                POINTL center;
                mouse_x += (SHORT)SHORT1FROMMP(mp1) - width / 2;
                mouse_y += height / 2 - (SHORT)SHORT2FROMMP(mp1);
                if (COM_CheckParm("-inputlog") && (mouse_x || mouse_y))
                    Sys_Printf("PM mouse dx=%d dy=%d\n", mouse_x, mouse_y);
                center.x = width / 2;
                center.y = height / 2;
                if ((SHORT)SHORT1FROMMP(mp1) != center.x || (SHORT)SHORT2FROMMP(mp1) != center.y)
                {
                    WinMapWindowPoints(hwnd, HWND_DESKTOP, &center, 1);
                    WinSetPointerPos(HWND_DESKTOP, center.x, center.y);
                }
            }
            return 0;
    }
    return WinDefWindowProc(hwnd, msg, mp1, mp2);
}

void OS2_PumpEvents(void)
{
    QMSG msg;
    if (!hab)
        return;
    while (WinPeekMsg(hab, &msg, 0, 0, 0, PM_REMOVE))
    {
        if (msg.msg == WM_QUIT)
            Cbuf_AddText("quit\n");
        else
            WinDispatchMsg(hab, &msg);
    }
}

void VID_SetPalette(unsigned char *palette)
{
    int i;
    for (i = 0; i < 256; ++i)
        d_8to24table[i] =
            palette[3 * i] | (palette[3 * i + 1] << 8) | (palette[3 * i + 2] << 16) | 0xff000000U;
    d_8to24table[255] &= 0x00ffffff;
}
void VID_ShiftPalette(unsigned char *palette) {}
qboolean VID_Is8bit(void)
{
    return false;
}
void D_BeginDirectRect(int x, int y, byte *bitmap, int w, int h) {}
void D_EndDirectRect(int x, int y, int w, int h) {}

void VID_Init(unsigned char *palette)
{
    ULONG flags = FCF_TITLEBAR | FCF_SYSMENU | FCF_MINBUTTON | FCF_BORDER | FCF_TASKLIST;
    int attribs[] = {PGL_RGBA, PGL_DOUBLEBUFFER, PGL_RED_SIZE, 1, PGL_GREEN_SIZE, 1, PGL_BLUE_SIZE,
                     1,        PGL_DEPTH_SIZE,   16,           0};
    PVISUALCONFIG config;
    RECTL rect;
    int i;
    char path[MAX_OSPATH];
    i = COM_CheckParm("-width");
    if (i && i + 1 < com_argc)
        width = atoi(com_argv[i + 1]);
    i = COM_CheckParm("-height");
    if (i && i + 1 < com_argc)
        height = atoi(com_argv[i + 1]);
    if (width < 320 || width > 1600 || height < 200 || height > 1200)
        Sys_Error("Video size outside 320x200 to 1600x1200");
    hab = WinInitialize(0);
    queue = WinCreateMsgQueue(hab, 0);
    if (!hab || !queue || !WinRegisterClass(hab, "GLQuakeOS2", WindowProc, CS_SIZEREDRAW, 0))
        Sys_Error("PM initialization failed");
    frame = WinCreateStdWindow(HWND_DESKTOP, 0, &flags, "GLQuakeOS2", "GLQuake for OS/2", 0, 0, 1,
                               &window);
    if (!frame || !window)
        Sys_Error("PM window creation failed");
    rect.xLeft = rect.yBottom = 0;
    rect.xRight = width;
    rect.yTop = height;
    WinCalcFrameRect(frame, &rect, FALSE);
    WinSetWindowPos(frame, HWND_TOP, 40, 40, rect.xRight - rect.xLeft, rect.yTop - rect.yBottom,
                    SWP_MOVE | SWP_SIZE | SWP_SHOW | SWP_ACTIVATE);
    config = pglChooseConfig(hab, attribs);
    if (!config)
        Sys_Error("No PGL RGB double-buffered depth visual");
    context = pglCreateContext(hab, config, 0, FALSE);
    if (!context || !pglMakeCurrent(hab, context, window))
        Sys_Error("Cannot create PGL context");
    gl_vendor = (const char *)glGetString(GL_VENDOR);
    gl_renderer = (const char *)glGetString(GL_RENDERER);
    gl_version = (const char *)glGetString(GL_VERSION);
    gl_extensions = (const char *)glGetString(GL_EXTENSIONS);
    Con_Printf("GL_VENDOR: %s\nGL_RENDERER: %s\nGL_VERSION: %s\n", gl_vendor, gl_renderer,
               gl_version);
    WinSetFocus(HWND_DESKTOP, window);
    QGL_InitTextures();
    Cvar_RegisterVariable(&gl_ztrick);
    vid.maxwarpwidth = 320;
    vid.maxwarpheight = 200;
    vid.colormap = host_colormap;
    vid.fullbright = 256 - LittleLong(*((int *)vid.colormap + 2048));
    vid.conwidth = width & ~7;
    vid.conheight = height;
    vid.width = vid.conwidth;
    vid.height = vid.conheight;
    vid.aspect = (float)height / width * (320.0 / 240.0);
    vid.numpages = 2;
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glClearColor(0, 0, 0, 0);
    glCullFace(GL_FRONT);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GREATER, 0.666);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glShadeModel(GL_FLAT);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    VID_SetPalette(palette);
    sprintf(path, "%s/glquake", com_gamedir);
    Sys_mkdir(path);
    vid.recalc_refdef = 1;
    Con_SafePrintf("OS/2 PGL video %dx%d initialized\n", width, height);
}

void VID_Shutdown(void)
{
    if (context)
    {
        QGL_ShutdownTextures();
        pglMakeCurrent(hab, 0, 0);
        pglDestroyContext(hab, context);
        context = 0;
    }
    if (frame)
    {
        WinDestroyWindow(frame);
        frame = window = 0;
    }
    if (queue)
    {
        WinDestroyMsgQueue(queue);
        queue = 0;
    }
    if (hab)
    {
        WinTerminate(hab);
        hab = 0;
    }
}
void GL_BeginRendering(int *x, int *y, int *w, int *h)
{
    *x = *y = 0;
    *w = width;
    *h = height;
}
void GL_EndRendering(void)
{
    glFlush();
    pglSwapBuffers(hab, window);
}
void IN_Init(void) {}
void IN_Shutdown(void) {}
void IN_Commands(void) {}
void IN_Move(usercmd_t *cmd)
{
    float mx = mouse_x * sensitivity.value, my = mouse_y * sensitivity.value;
    mouse_x = mouse_y = 0;
    if (in_strafe.state & 1)
        cmd->sidemove += m_side.value * mx;
    else
        cl.viewangles[YAW] -= m_yaw.value * mx;
    if ((in_mlook.state & 1) && !(in_strafe.state & 1))
    {
        V_StopPitchDrift();
        cl.viewangles[PITCH] += m_pitch.value * my;
        if (cl.viewangles[PITCH] > 80)
            cl.viewangles[PITCH] = 80;
        if (cl.viewangles[PITCH] < -70)
            cl.viewangles[PITCH] = -70;
    }
    else
        cmd->forwardmove -= m_forward.value * my;
}
