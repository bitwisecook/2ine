/* Public domain. No rights reserved. Wholly AI-authored. */
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE 1
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "hal/common/desktop_cycle.h"
#include <assert.h>
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <unistd.h>
static void remove_fixture(const char *directory)
{
    DIR *items = opendir(directory);
    assert(items);
    struct dirent *entry;
    while ((entry = readdir(items)))
    {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
            continue;
        char path[2048];
        snprintf(path, sizeof path, "%s/%s", directory, entry->d_name);
        struct stat info;
        assert(!lstat(path, &info));
        if (S_ISDIR(info.st_mode))
            remove_fixture(path);
        else
            assert(!unlink(path));
    }
    closedir(items);
    assert(!rmdir(directory));
}
int main(void)
{
    LxDesktopCycleWindow windows[] = {{7, 30, 3}, {9, 10, 1}, {9, 20, 2}}, target;
    assert(lx_desktop_cycle_next(windows, 3, 9, 1, 0, &target) && target.window == 2);
    assert(lx_desktop_cycle_next(windows, 3, 9, 1, 1, &target) && target.window == 3);
    assert(lx_desktop_cycle_next(windows, 3, 7, 3, 0, &target) && target.window == 1);
    assert(lx_desktop_cycle_next(windows, 3, 9, 2, 1, &target) && target.window == 1);
    assert(lx_desktop_cycle_next(windows, 3, 99, 99, 1, &target) && target.window == 3);
    assert(!lx_desktop_cycle_next(windows, 1, 7, 3, 0, &target));
    char directory[] = "/tmp/2ine-window-cycle-XXXXXX";
    assert(mkdtemp(directory));
    LxDesktopCycle *a = lx_desktop_cycle_open(directory, "app"),
                   *other = lx_desktop_cycle_open(directory, "different-app");
    assert(a && other && !lx_desktop_cycle_open(directory, "../escape"));
    uint32_t local[] = {10, 20}, alien[] = {99}, pid = 0;
    assert(lx_desktop_cycle_publish(a, local, 2, 0));
    assert(!lx_desktop_cycle_publish(a, local, 257, 0));
    assert(lx_desktop_cycle_publish(other, alien, 1, 0));
    assert(lx_desktop_cycle_focus(a, 10));
    assert(lx_desktop_cycle_request(a, 10, 0, &pid) == 1 && pid == (uint32_t)getpid());
    assert(lx_desktop_cycle_receive(a) == 20 && lx_desktop_cycle_receive(other) == 0);
    assert(lx_desktop_cycle_keyup(a) && !lx_desktop_cycle_keyup(a));
    assert(lx_desktop_cycle_request(a, 20, 0, &pid) == 1 && lx_desktop_cycle_receive(a) == 10);
    assert(lx_desktop_cycle_request(a, 10, 1, &pid) == 1 && lx_desktop_cycle_receive(a) == 20);
    assert(lx_desktop_cycle_publish(a, local, 2, 1));
    assert(lx_desktop_cycle_request(a, 20, 0, &pid) == 2);
    assert(lx_desktop_cycle_keyup(a));
    assert(lx_desktop_cycle_publish(a, local, 2, 0));
    int ready[2], command[2];
    assert(!pipe(ready) && !pipe(command));
    pid_t child = fork();
    assert(child >= 0);
    if (!child)
    {
        close(ready[0]);
        close(command[1]);
        LxDesktopCycle *b = lx_desktop_cycle_open(directory, "app");
        uint32_t owned[] = {30};
        assert(b && lx_desktop_cycle_publish(b, owned, 1, 0));
        char signal = 'R';
        assert(write(ready[1], &signal, 1) == 1);
        while (read(command[0], &signal, 1) == 1)
        {
            uint32_t received;
            if (signal == 'M' || signal == 'U')
            {
                assert(lx_desktop_cycle_publish(b, owned, 1, signal == 'M'));
                received = 0;
            }
            else
                received = lx_desktop_cycle_receive(b);
            assert(write(ready[1], &received, sizeof received) == sizeof received);
        }
        _exit(0);
    }
    close(ready[1]);
    close(command[0]);
    char signal;
    assert(read(ready[0], &signal, 1) == 1);
    uint32_t received;
    assert(write(command[1], "M", 1) == 1);
    assert(read(ready[0], &received, sizeof received) == sizeof received);
    assert(lx_desktop_cycle_request(a, 20, 0, &pid) == 2); /* A peer's modal blocks us too. */
    assert(lx_desktop_cycle_keyup(a));
    assert(write(command[1], "U", 1) == 1);
    assert(read(ready[0], &received, sizeof received) == sizeof received);
    assert(lx_desktop_cycle_focus(a, 20));
    assert(lx_desktop_cycle_request(a, 20, 0, &pid) == 1 && pid == (uint32_t)child);
    /* Rapid successive gestures supersede the older peer request. */
    assert(lx_desktop_cycle_request(a, 20, 0, &pid) == 1 && pid == (uint32_t)getpid());
    assert(write(command[1], "R", 1) == 1);
    assert(read(ready[0], &received, sizeof received) == sizeof received && received == 0);
    assert(lx_desktop_cycle_receive(a) == 10);
    assert(lx_desktop_cycle_focus(a, 20));
    assert(lx_desktop_cycle_request(a, 20, 0, &pid) == 1 && pid == (uint32_t)child);
    assert(write(command[1], "R", 1) == 1);
    assert(read(ready[0], &received, sizeof received) == sizeof received && received == 30);
    assert(!kill(child, SIGKILL));
    int status;
    assert(waitpid(child, &status, 0) == child && WIFSIGNALED(status));
    assert(lx_desktop_cycle_focus(a, 20));
    assert(lx_desktop_cycle_request(a, 20, 0, &pid) == 1 && pid == (uint32_t)getpid());
    assert(lx_desktop_cycle_receive(a) == 10); /* Crashed member is absent, wrap locally. */
    uint32_t one = 20;
    assert(lx_desktop_cycle_publish(a, &one, 1, 0));
    assert(lx_desktop_cycle_focus(a, one));
    assert(lx_desktop_cycle_request(a, one, 0, &pid) == 0);
    char guard_path[2048];
    snprintf(guard_path, sizeof guard_path, "%s/desktop-app.windows/guard", directory);
    int guard = open(guard_path, O_RDWR);
    assert(guard >= 0 && !flock(guard, LOCK_EX | LOCK_NB));
    assert(lx_desktop_cycle_request(a, one, 0, &pid) == -1); /* Never wait on a busy peer. */
    assert(!flock(guard, LOCK_UN));
    close(guard);
    close(ready[0]);
    close(command[1]);
    lx_desktop_cycle_close(a);
    lx_desktop_cycle_close(other);
    remove_fixture(directory);
    puts("Session window cycling: forward/reverse/wrap, modality, IPC and crash isolation passed");
    return 0;
}
