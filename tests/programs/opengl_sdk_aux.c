/* Public domain. No rights reserved. Wholly AI-authored. */
/* Minimal AUX window setup for the SDK SIMPLE.C drawing fixture. */
#define INCL_WIN
#define INCL_DOS
#include <os2.h>
#include <GL/gl.h>
#include <GL/pgl.h>
#include <stdio.h>
#include <stdlib.h>
static HAB anchor;
static HMQ queue;
static HWND window;
static HGC context;
static int width = 500, height = 500;
static void require(int value)
{
    if (!value)
    {
        puts("SDK OpenGL fixture failed");
        exit(1);
    }
}
void APIENTRY auxInitDisplayMode(GLenum mode)
{
    require(mode == 0); /* AUX_SINGLE | AUX_RGB */
}
void APIENTRY auxInitPosition(int x, int y, int w, int h)
{
    (void)x;
    (void)y;
    width = w;
    height = h;
}
GLenum APIENTRY auxInitWindow(char *title)
{
    int attributes[] = {PGL_RGBA, PGL_DEPTH_SIZE, 24, 0};
    PVISUALCONFIG config;
    anchor = WinInitialize(0);
    queue = WinCreateMsgQueue(anchor, 0);
    require(anchor && queue);
    window = WinCreateWindow(HWND_DESKTOP, WC_FRAME, title, WS_VISIBLE, 0, 0, width, height,
                             HWND_DESKTOP, HWND_TOP, 1, NULL, NULL);
    require(window != NULLHANDLE);
    config = pglChooseConfig(anchor, attributes);
    require(config != NULL);
    context = pglCreateContext(anchor, config, 0, TRUE);
    require(context && pglMakeCurrent(anchor, context, window));
    return GL_TRUE;
}
extern int sdk_simple_main(int argc, char **argv);
int main(int argc, char **argv)
{
    unsigned char center[4] = {0}, corner[4] = {0};
    sdk_simple_main(argc, argv);
    glReadPixels(width / 2, height / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, center);
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, corner);
    require(center[0] == 255 && center[1] == 255 && center[2] == 255);
    require(corner[0] == 0 && corner[1] == 0 && corner[2] == 0);
    require(glGetError() == GL_NO_ERROR);
    require(pglMakeCurrent(anchor, 0, 0));
    require(pglDestroyContext(anchor, context));
    require(WinDestroyWindow(window));
    require(WinDestroyMsgQueue(queue));
    require(WinTerminate(anchor));
    puts("SDK SIMPLE.C rendered white rectangle on black with Mesa");
    return 0;
}
