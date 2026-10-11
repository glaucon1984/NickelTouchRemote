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

struct nh_info info = {
    .name = "NickelTouchRemote",
    .desc = "Turn pages with touch-screen style Bluetooth remotes",
    .uninstall_flag = TR_CONFIG_DIR "/uninstall",
    .failsafe_delay = 5,
};

struct nh_hook hooks[] = {
    {0},
};

struct nh_dlsym symbols[] = {
    {.name = "_ZN23BluetoothHeartbeatEvent9eventTypeEv",
     .out = nh_symoutptr(touchremote::nickel::bluetoothHeartbeatEventType),
     .desc = "Keep Nickel's Bluetooth radio on while the remote is used",
     .optional = true},
    {.name = "_ZN9TimeEvent9eventTypeEv",
     .out = nh_symoutptr(touchremote::nickel::timeEventType),
     .desc = "Activity event for Nickel's power manager",
     .optional = true},
    {.name = "_ZN12PowerManager14sharedInstanceEv",
     .out = nh_symoutptr(touchremote::nickel::powerManagerSharedInstance),
     .desc = "Nickel's power manager",
     .optional = true},
    {.name = "_ZN12PowerManager6filterEP7QObjectP6QEvent",
     .out = nh_symoutptr(touchremote::nickel::powerManagerFilter),
     .desc = "Reset Nickel's inactivity timer",
     .optional = true},
    {.name = "_ZN20MainWindowController14sharedInstanceEv",
     .out = nh_symoutptr(touchremote::nickel::mainWindowControllerSharedInstance),
     .desc = "Nickel's main window controller",
     .optional = true},
    {.name = "_ZNK20MainWindowController11currentViewEv",
     .out = nh_symoutptr(touchremote::nickel::mainWindowControllerCurrentView),
     .desc = "Nickel's current view, for the optional view page-turn method",
     .optional = true},
    {0},
};

}

NickelHook(
    .init = init,
    .info = &info,
    .hook = hooks,
    .dlsym = symbols,
)
