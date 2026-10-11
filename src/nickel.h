// Symbols resolved from libnickel at load time. Every one is optional; a
// missing symbol only disables the feature that needs it.
#ifndef TOUCHREMOTE_NICKEL_H
#define TOUCHREMOTE_NICKEL_H

class QObject;
class QWidget;
class QEvent;

namespace touchremote {
namespace nickel {

extern int (*bluetoothHeartbeatEventType)();
extern int (*timeEventType)();
extern QObject *(*powerManagerSharedInstance)();
extern int (*powerManagerFilter)(QObject *, QObject *, QEvent *);
extern void *(*mainWindowControllerSharedInstance)();
extern QWidget *(*mainWindowControllerCurrentView)(void *);

}
}

#endif
