#include "LinuxPowerSleep.h"

#ifdef PLATFORM_TG5040

#include "System/Console/Trace.h"

#include <SDL2/SDL.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

static volatile int g_running = 0;
static volatile int g_in_suspend = 0;
static pthread_t g_thread;
static unsigned long long g_ignore_until_ms = 0;

static unsigned long long nowMs() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (unsigned long long)tv.tv_sec * 1000ULL +
           (unsigned long long)tv.tv_usec / 1000ULL;
}

static void drainInput(int fd) {
    struct input_event ev;
    while (read(fd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
    }
}

static int keyPowerDown(int fd) {
    unsigned char map[(KEY_MAX + 7) / 8];
    memset(map, 0, sizeof(map));
    if (ioctl(fd, EVIOCGKEY(sizeof(map)), map) < 0) {
        return 0;
    }
    return (map[KEY_POWER / 8] & (1 << (KEY_POWER % 8))) != 0;
}

// After wake the same KEY_POWER press/release that woke the kernel is still
// in the queue (or still held). Wait it out so we do not immediately re-sleep.
static void settlePowerButton(int fd) {
    drainInput(fd);
    unsigned long long deadline = nowMs() + 3000ULL;
    while (g_running && keyPowerDown(fd) && nowMs() < deadline) {
        struct input_event ev;
        if (read(fd, &ev, sizeof(ev)) != (ssize_t)sizeof(ev)) {
            usleep(20000);
        }
    }
    drainInput(fd);
}

static void notifyMainWake() {
    SDL_Event e;
    SDL_zero(e);
    e.type = SDL_USEREVENT;
    e.user.code = LGPT_SDL_USER_WAKE;
    SDL_PushEvent(&e);
}

// Returns 0 if a NextUI/MinUI suspend helper was invoked. The helper may
// exit non-zero after a successful sleep (known false negative on tg5040),
// so callers must not retry with a raw echo-mem in that case.
static int trySuspendHelper() {
    static const char *absolute[] = {
        "/mnt/SDCARD/.system/tg5040/bin/suspend",
        "/mnt/SDCARD/.system/tg3040/bin/suspend",
        NULL,
    };
    for (int i = 0; absolute[i]; ++i) {
        if (access(absolute[i], X_OK) != 0) {
            continue;
        }
        Trace::Log("POWER", "calling %s", absolute[i]);
        int rc = system(absolute[i]);
        Trace::Log("POWER", "%s returned %d", absolute[i], rc);
        return 0;
    }

    Trace::Log("POWER", "calling suspend (PATH)");
    int rc = system("suspend");
    if (rc == -1) {
        return -1;
    }
    if (WIFEXITED(rc) && WEXITSTATUS(rc) == 127) {
        return -1;
    }
    Trace::Log("POWER", "suspend returned %d", rc);
    return 0;
}

static int writeMemState() {
    int fd = open("/sys/power/state", O_WRONLY);
    if (fd < 0) {
        Trace::Log("POWER", "open /sys/power/state: %s", strerror(errno));
        return -1;
    }
    Trace::Log("POWER", "writing mem to /sys/power/state");
    ssize_t n = write(fd, "mem", 3);
    int err = (n < 0) ? errno : 0;
    close(fd);
    if (n < 0) {
        Trace::Log("POWER", "write mem: %s", strerror(err));
        return -1;
    }
    return 0;
}

void LinuxPowerSleep::Suspend() {
    if (g_in_suspend) {
        return;
    }
    if (nowMs() < g_ignore_until_ms) {
        Trace::Log("POWER", "suspend ignored (cooldown)");
        return;
    }
    g_in_suspend = 1;
    Trace::Log("POWER", "suspend");
    SDL_PauseAudio(1);
    sync();
    if (trySuspendHelper() != 0) {
        writeMemState();
    }
    // Ignore the wake press/release; main thread re-inits input/redraw.
    g_ignore_until_ms = nowMs() + 2500ULL;
    notifyMainWake();
    SDL_PauseAudio(0);
    g_in_suspend = 0;
}

void LinuxPowerSleep::PowerOff() {
    if (g_in_suspend) {
        return;
    }
    Trace::Log("POWER", "poweroff");
    SDL_PauseAudio(1);
    sync();
    if (system("poweroff") != 0) {
        system("shutdown -h now");
    }
}

static int openPowerFd() {
    DIR *dir = opendir("/dev/input");
    if (!dir) {
        return -1;
    }
    int found = -1;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (strncmp(ent->d_name, "event", 5) != 0) {
            continue;
        }
        char path[256];
        snprintf(path, sizeof(path), "/dev/input/%s", ent->d_name);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) {
            continue;
        }
        unsigned long bits[(KEY_MAX + 63) / 64];
        memset(bits, 0, sizeof(bits));
        if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(bits)), bits) < 0) {
            close(fd);
            continue;
        }
        unsigned long mask = 1UL << (KEY_POWER % 64);
        if (bits[KEY_POWER / 64] & mask) {
            Trace::Log("POWER", "watching %s for KEY_POWER", path);
            found = fd;
            break;
        }
        close(fd);
    }
    closedir(dir);
    return found;
}

static void *powerThread(void *) {
    int fd = -1;
    while (g_running && fd < 0) {
        fd = openPowerFd();
        if (fd < 0) {
            usleep(500000);
        }
    }
    if (fd < 0) {
        return NULL;
    }

    int pressed = 0;
    unsigned long long pressMs = 0;

    while (g_running) {
        struct input_event ev;
        ssize_t n = read(fd, &ev, sizeof(ev));
        if (n == (ssize_t)sizeof(ev)) {
            if (ev.type != EV_KEY || ev.code != KEY_POWER) {
                continue;
            }
            if (ev.value == 1) {
                if (nowMs() < g_ignore_until_ms) {
                    continue;
                }
                pressed = 1;
                pressMs = (unsigned long long)ev.time.tv_sec * 1000ULL +
                          (unsigned long long)ev.time.tv_usec / 1000ULL;
            } else if (ev.value == 0 && pressed) {
                pressed = 0;
                if (nowMs() < g_ignore_until_ms) {
                    continue;
                }
                unsigned long long releaseMs =
                    (unsigned long long)ev.time.tv_sec * 1000ULL +
                    (unsigned long long)ev.time.tv_usec / 1000ULL;
                if (releaseMs - pressMs >= 2000ULL) {
                    LinuxPowerSleep::PowerOff();
                } else {
                    LinuxPowerSleep::Suspend();
                    settlePowerButton(fd);
                    pressed = 0;
                }
            }
        } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            // Input node went away across suspend; reopen.
            close(fd);
            fd = -1;
            while (g_running && fd < 0) {
                fd = openPowerFd();
                if (fd < 0) {
                    usleep(200000);
                }
            }
            pressed = 0;
        } else {
            usleep(20000);
        }
    }
    if (fd >= 0) {
        close(fd);
    }
    return NULL;
}

void LinuxPowerSleep::Start() {
    if (getenv("LGPT_NO_NATIVE_SLEEP")) {
        Trace::Log("POWER", "native sleep disabled (external power control)");
        return;
    }
    if (g_running) {
        return;
    }
    g_running = 1;
    if (pthread_create(&g_thread, NULL, powerThread, NULL) != 0) {
        g_running = 0;
        Trace::Log("POWER", "failed to start power thread");
        return;
    }
    pthread_detach(g_thread);
    Trace::Log("POWER", "native KEY_POWER watcher started");
}

void LinuxPowerSleep::Stop() { g_running = 0; }

#else

void LinuxPowerSleep::Start() {}
void LinuxPowerSleep::Stop() {}
void LinuxPowerSleep::Suspend() {}
void LinuxPowerSleep::PowerOff() {}

#endif
