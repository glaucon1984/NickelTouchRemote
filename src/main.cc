// Entry point: NickelHook registration and start-up of the controller.
#include <NickelHook.h>

#include <pthread.h>
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
    5,                                                       // failsafe_delay
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
