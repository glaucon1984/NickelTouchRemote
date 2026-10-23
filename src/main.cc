// Entry point: NickelHook registration and start-up of the controller.
#include <NickelHook.h>

#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <QCoreApplication>
#include <QMetaObject>

#include "nickel.h"
#include "touchremote.h"

namespace touchremote {
namespace nickel {
int (*bluetoothHeartbeatEventType)() = 0;
int (*timeEventType)() = 0;
QObject *(*powerManagerSharedInstance)() = 0;
int (*powerManagerFilter)(QObject *, QObject *, QEvent *) = 0;
void *(*mainWindowControllerSharedInstance)() = 0;
QWidget *(*mainWindowControllerCurrentView)(void *) = 0;
}
}

namespace {

touchremote::Controller *controller = 0;

// NickelHook renames this library to "<name>.failsafe" while a mod starts and
// renames it back from a helper thread a couple of seconds later. If Nickel
// dies inside that window (a kernel panic or a hard power-off right at boot),
// the file keeps the fail-safe name, Qt loads it under that name at the next
// start, and NickelHook then refuses to initialize it: the mod is in memory
// but inert, for every boot to come. Runs before NickelHook's constructor
// (lower priority number) and puts the file right for the next start.
__attribute__((constructor(101))) void healFailsafeCopy() {
    Dl_info info;
    if (!dladdr(reinterpret_cast<void *>(&healFailsafeCopy), &info) || !info.dli_fname)
        return;
    const char *path = info.dli_fname;
    const char suffix[] = ".failsafe";
    const size_t length = strlen(path), suffixLength = sizeof(suffix) - 1;
    char real[4096];
    if (length <= suffixLength || strcmp(path + length - suffixLength, suffix) != 0) {
        // Loaded under the proper name. A fail-safe copy left beside it would be
        // loaded by Qt as well; it is stale (NickelHook's own rename comes later).
        if (length + suffixLength >= sizeof(real))
            return;
        snprintf(real, sizeof(real), "%s%s", path, suffix);
        if (access(real, F_OK) == 0 && unlink(real) == 0) {
            nh_log("touchremote: removed the stale fail-safe copy %s", real);
            sync();
        }
        return;
    }
    if (length - suffixLength >= sizeof(real))
        return;
    memcpy(real, path, length - suffixLength);
    real[length - suffixLength] = 0;
    struct stat st;
    if (stat(real, &st) == 0) {
        // A proper copy exists beside this stale one: remove the stale one.
        if (unlink(path) == 0)
            nh_log("touchremote: removed the stale fail-safe copy %s", path);
    } else if (rename(path, real) == 0) {
        nh_log("touchremote: restored %s from its fail-safe copy; the mod starts at the next restart", real);
    }
    sync();
}

// NickelHook runs the mod's init from the library constructor, which Qt's
// plugin loader triggers on Nickel's main thread. Nickel's QApplication
// normally exists by then; if it does not, wait for it on a helper thread
// and post the start to the controller's thread.
void *waitForApplication(void *) {
    for (int i = 0; i < 600 && !QCoreApplication::instance(); ++i)
        usleep(100 * 1000);
    if (!QCoreApplication::instance()) {
        nh_log("touchremote: no Qt application after 60 s, not starting");
        return 0;
    }
    QMetaObject::invokeMethod(controller, "start", Qt::QueuedConnection);
    return 0;
}

int init() {
    controller = new touchremote::Controller();
    if (QCoreApplication::instance()) {
        QMetaObject::invokeMethod(controller, "start", Qt::QueuedConnection);
        return 0;
    }
    pthread_t thread;
    pthread_attr_t attributes;
    pthread_attr_init(&attributes);
    pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
    const int error = pthread_create(&thread, &attributes, waitForApplication, 0);
    pthread_attr_destroy(&attributes);
    if (error) {
        nh_log("touchremote: cannot create start-up thread (%d)", error);
        return 1;
    }
    return 0;
}

// GCC 4.9 accepts designated initializers in C++ only when every field is
// listed in order, so these tables are positional: see NickelHook.h.
struct nh_info info = {
    "NickelTouchRemote",                                     // name
    "Turn pages with touch-screen style Bluetooth remotes",  // desc
    TR_CONFIG_DIR "/uninstall",                              // uninstall_flag
    0,                                                       // uninstall_xflag
    2,                                                       // failsafe_delay
};

struct nh_hook hooks[] = {
    {0},
};

// name, out, desc, optional
struct nh_dlsym symbols[] = {
    {"_ZN23BluetoothHeartbeatEvent9eventTypeEv", nh_symoutptr(touchremote::nickel::bluetoothHeartbeatEventType),
     "Keep Nickel's Bluetooth radio on while the remote is used", true},
    {"_ZN9TimeEvent9eventTypeEv", nh_symoutptr(touchremote::nickel::timeEventType),
     "Activity event for Nickel's power manager", true},
    {"_ZN12PowerManager14sharedInstanceEv", nh_symoutptr(touchremote::nickel::powerManagerSharedInstance),
     "Nickel's power manager", true},
    {"_ZN12PowerManager6filterEP7QObjectP6QEvent", nh_symoutptr(touchremote::nickel::powerManagerFilter),
     "Reset Nickel's inactivity timer", true},
    {"_ZN20MainWindowController14sharedInstanceEv",
     nh_symoutptr(touchremote::nickel::mainWindowControllerSharedInstance), "Nickel's main window controller", true},
    {"_ZNK20MainWindowController11currentViewEv", nh_symoutptr(touchremote::nickel::mainWindowControllerCurrentView),
     "Nickel's current view, for the optional view page-turn method", true},
    {0},
};

}

NickelHook(
    .init = init,
    .info = &info,
    .hook = hooks,
    .dlsym = symbols,
    .uninstall = 0,
)
