/* Public domain. No rights reserved. Wholly AI-authored. */
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE 1
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "hal/common/desktop_group.h"
#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct Policy
{
    int regular, refuse_demotion;
} Policy;

static int policy(void *context, int regular)
{
    Policy *state = context;
    if (!regular && state->refuse_demotion)
        return 0;
    state->regular = regular;
    return 1;
}

int main(void)
{
    char directory[] = "/tmp/2ine-desktop-model-XXXXXX";
    assert(mkdtemp(directory));
    Policy first = {0}, second = {0}, helper = {0};
    LxDesktopGroup *a = lx_desktop_group_open(directory, "fixture", 0, policy, &first);
    LxDesktopGroup *b = lx_desktop_group_open(directory, "fixture", 0, policy, &second);
    LxDesktopGroup *aux = lx_desktop_group_open(NULL, NULL, 1, policy, &helper);
    assert(a && b && aux && !first.regular && !second.regular && !helper.regular);
    assert(lx_desktop_group_visible(aux, &helper, 1) && !helper.regular);
    assert(lx_desktop_group_poll(a) && !first.regular); /* No visible GUI: never lead. */
    assert(lx_desktop_group_visible(a, &first, 1) && first.regular);
    assert(lx_desktop_group_visible(a, &second, 1) && first.regular);
    assert(lx_desktop_group_visible(b, &second, 1) && !second.regular);
    assert(lx_desktop_group_visible(a, &first, 0) && first.regular);
    first.refuse_demotion = 1;
    assert(!lx_desktop_group_visible(a, &second, 0) && first.regular);
    assert(lx_desktop_group_poll(b) && !second.regular); /* Failed demotion retains lease. */
    first.refuse_demotion = 0;
    assert(lx_desktop_group_poll(a) && !first.regular);
    assert(lx_desktop_group_poll(b) && second.regular);
    assert(lx_desktop_group_close(a) && lx_desktop_group_close(b) && lx_desktop_group_close(aux));
    int ready[2];
    assert(!pipe(ready));
    pid_t child = fork();
    assert(child >= 0);
    if (!child)
    {
        close(ready[0]);
        Policy state = {0};
        LxDesktopGroup *owner = lx_desktop_group_open(directory, "crash", 0, policy, &state);
        char success = owner && lx_desktop_group_visible(owner, &state, 1) && state.regular;
        assert(write(ready[1], &success, 1) == 1);
        for (;;)
            pause();
    }
    close(ready[1]);
    char success;
    assert(read(ready[0], &success, 1) == 1 && success);
    close(ready[0]);
    second.regular = 0;
    b = lx_desktop_group_open(directory, "crash", 0, policy, &second);
    assert(b && lx_desktop_group_visible(b, &second, 1) && !second.regular);
    assert(!kill(child, SIGKILL));
    int status;
    assert(waitpid(child, &status, 0) == child && WIFSIGNALED(status));
    assert(lx_desktop_group_poll(b) && second.regular);
    assert(lx_desktop_group_close(b));
    assert(!lx_desktop_group_open(directory, "../invalid", 0, policy, &second));
    char path[1024];
    snprintf(path, sizeof path, "%s/desktop-fixture.leader", directory);
    assert(!unlink(path));
    snprintf(path, sizeof path, "%s/desktop-crash.leader", directory);
    assert(!unlink(path));
    assert(!rmdir(directory));
    puts("Desktop GUI leadership, auxiliary exclusion and crash transfer passed");
    return 0;
}
