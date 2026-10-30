#ifndef _LINUX_POWER_SLEEP_H_
#define _LINUX_POWER_SLEEP_H_

// Posted as SDL_USEREVENT.code after returning from deep sleep so the
// main thread can re-open joysticks and force a redraw.
#define LGPT_SDL_USER_WAKE 0x4C475057 /* 'LGPW' */

// TrimUI Brick / NextUI: watch KEY_POWER on /dev/input and suspend
// (or power off on long press). No-ops on other platforms.
class LinuxPowerSleep {
  public:
    static void Start();
    static void Stop();
    static void Suspend();
    static void PowerOff();
};

#endif
