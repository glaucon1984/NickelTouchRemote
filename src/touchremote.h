// NickelTouchRemote: page turning with touch-screen style Bluetooth remotes.
#ifndef TOUCHREMOTE_H
#define TOUCHREMOTE_H

#include <QDBusMessage>
#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

#include "gesture.h"

class QDBusPendingCallWatcher;
class QDBusServiceWatcher;
class QScreen;
class QFileSystemWatcher;
class QSocketNotifier;
class QTimer;

namespace touchremote {

enum Action {
    ActionNone = 0,
    ActionNext,
    ActionPrev,
    ActionSwap,   // exchange the next/previous actions until the next swap
    ActionInvert  // turn the screen upside down (and back)
};

const char *actionName(Action action);
Action parseAction(const QString &text);

struct Settings {
    Action tap, swipeUp, swipeDown, swipeLeft, swipeRight;
    QHash<int, Action> keys;  // evdev key code -> action
    bool swapWhenInverted;    // negate next/prev while the screen is upside down
    bool grabTouchDevices;    // take touch-style remotes away from Nickel
    bool grabOtherDevices;    // take every other Bluetooth input device too
    bool viewMethods;         // call Nickel's ReadingView instead of sending keys
    bool powerActivity;       // report activity to Nickel's PowerManager
    bool autoConnect;         // connect the last remote ourselves when Bluetooth is on
    bool verbose;             // log every raw input event

    Settings();
    // Returns false and leaves the defaults when the file does not exist.
    bool load(const QString &file);
    Action forGesture(Gesture gesture) const;
};

class Controller;

// One opened /dev/input/eventN node of a Bluetooth device.
class Device : public QObject, private GestureTranslator::Sink {
    Q_OBJECT
public:
    Device(Controller *owner, const QString &path, int fd, const QString &name, bool touch, bool grabbed);
    ~Device();
    QString path() const { return devicePath; }
    QString name() const { return deviceName; }
    bool isTouch() const { return touch; }

private slots:
    void readable();

private:
    void gesture(Gesture gesture);
    void key(uint16_t code, int32_t value);

    Controller *owner;
    QString devicePath;
    int fd;
    QString deviceName;
    bool touch;
    QSocketNotifier *notifier;
    GestureTranslator translator;
};

class Controller : public QObject {
    Q_OBJECT
public:
    Controller();
    const Settings &settings() const { return config; }
    void perform(Action action, const QString &source);
    void deviceLost(Device *device);

public slots:
    // Runs once the Qt event loop of Nickel's main thread is available.
    void start();

private slots:
    void inputDirectoryChanged(const QString &path);
    void scanInputDevices();
    void bindUnclaimedHidDevices();
    void bindTick();
    void fallbackTick();
    void bluetoothSignal(const QDBusMessage &message);
    void serviceOwnerChanged(const QString &service, const QString &previous, const QString &owner);
    void adapterQueryFinished(QDBusPendingCallWatcher *watcher);
    void dumpLog();
    void connectTick();
    void connectFinished(QDBusPendingCallWatcher *watcher);
    void wakeTick();

private:
    void reloadSettings();
    void loadKnownIds();
    void rememberHidId(const QString &id);
    bool writeNewId(const QString &id);
    void startBindBurst();
    void setPowered(bool value);
    void queryAdapter();
    bool sendPageKey(int direction);
    void invertScreen();
    void turnScreen(QScreen *screen, Qt::ScreenOrientation orientation);
    bool turnNickelPage(int direction);
    void heartbeat();
    void reportActivity();
    void armDumpLog();
    void loadRemotes();
    void rememberRemote(const QString &path);
    bool remoteOpen() const;
    void startConnectBurst(int attemptsDone);
    void connectNow(const QString &path, const char *reason);

    Settings config;
    QList<Device *> devices;
    QSet<QString> knownIds;
    QString service;
    QFileSystemWatcher *watcher;
    QTimer *bindTimer;
    QTimer *fallbackTimer;
    QTimer *dumpTimer;
    QTimer *connectTimer;
    QTimer *wakeTimer;
    QStringList remotes;         // Device1 paths of remotes seen connected, most recent last
    QString lastConnectedPath;   // most recent Device1 that reported Connected
    QElapsedTimer lastConnectedAt;
    QElapsedTimer lastRemoteOpenedAt;  // the daemon may report Connected after the node appears
    QElapsedTimer lastAttempt;
    int connectAttempts;
    long wakeClock;
    int bindTicks;
    bool started;
    bool powered;
    bool swapped;
    QElapsedTimer lastHeartbeat;
};

}

#endif
