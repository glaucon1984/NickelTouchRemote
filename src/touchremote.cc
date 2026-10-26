#include "touchremote.h"

#include <NickelHook.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <QApplication>
#include <QCoreApplication>
#include <QDBusArgument>
#include <QDBusObjectPath>
#include <QDBusConnection>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QDBusConnectionInterface>
#include <QDBusVariant>
#include <QEventLoop>
#include <QFile>
#include <QFileSystemWatcher>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QScreen>
#include <QSettings>
#include <QSocketNotifier>
#include <QTimer>
#include <QWidget>
#include <QWindow>
#include <qpa/qwindowsysteminterface.h>

#include "nickel.h"

#ifndef TR_CONFIG_DIR
#define TR_CONFIG_DIR "/mnt/onboard/.adds/touchremote"
#endif

#ifndef BUS_BLUETOOTH
#define BUS_BLUETOOTH 0x05
#endif

namespace touchremote {
namespace {

const char *const inputDirectory = "/dev/input";
const char *const hidDevices = "/sys/bus/hid/devices";
const char *const hidGenericNewId = "/sys/bus/hid/drivers/hid-generic/new_id";
const char *const configFile = TR_CONFIG_DIR "/config.ini";
const char *const knownIdsFile = TR_CONFIG_DIR "/devices";
const char *const dumpLogFlag = TR_CONFIG_DIR "/dumplog";
const char *const remotesFile = TR_CONFIG_DIR "/remotes";
const char *const adapterPath = "/org/bluez/hci0";

const int bindBurstTicks = 25;         // seconds of 1 s bind attempts after a connection
const int fallbackIntervalMs = 10000;  // slow re-check while Bluetooth is powered
const int heartbeatIntervalMs = 5000;
const int dumpDelayMs = 60000;
// Direct connection attempts: a Connect to an absent device occupies the
// daemon for about 15 s, so space attempts out, quickly at first (right after
// Bluetooth comes on or the Kobo wakes, when the ring is usually switched on
// too) and slowly afterwards. Nickel's own inactivity timeout ends the loop by
// powering Bluetooth off.
const int connectBurstAttempts = 5;
const int connectBurstIntervalMs = 20000;
const int connectSlowIntervalMs = 60000;
const int connectMinSpacingMs = 15000;
const int wakeTickMs = 5000;
const int wakeGapSeconds = 30;

const unsigned longBits = 8 * sizeof(unsigned long);

inline bool testBit(const unsigned long *bits, unsigned bit) {
    return (bits[bit / longBits] >> (bit % longBits)) & 1;
}

#define BITS_FOR(max) (((max) + 8 * sizeof(unsigned long)) / (8 * sizeof(unsigned long)))

}

const char *actionName(Action action) {
    switch (action) {
    case ActionNext: return "next";
    case ActionPrev: return "prev";
    case ActionSwap: return "swap";
    case ActionInvert: return "invert";
    default: return "none";
    }
}

Action parseAction(const QString &text) {
    const QString value = text.trimmed().toLower();
    if (value == "next" || value == "next_page")
        return ActionNext;
    if (value == "prev" || value == "previous" || value == "prev_page")
        return ActionPrev;
    if (value == "swap")
        return ActionSwap;
    if (value == "invert" || value == "rotate")
        return ActionInvert;
    if (!value.isEmpty() && value != "none")
        nh_log("touchremote: unknown action '%s', using none", value.toUtf8().constData());
    return ActionNone;
}

// Defaults match the WX02 ("wxlj-02") ring: the lower button swipes towards
// larger Y, the upper one towards smaller Y, the middle one taps.
Settings::Settings()
    : tap(ActionSwap), swipeUp(ActionPrev), swipeDown(ActionNext), swipeLeft(ActionNone),
      swipeRight(ActionNone), swapWhenInverted(true), grabTouchDevices(true), grabOtherDevices(false),
      viewMethods(false), powerActivity(true), autoConnect(true), wifiHoldSeconds(30), verbose(false) {}

bool Settings::load(const QString &file) {
    *this = Settings();
    if (!QFile::exists(file))
        return false;
    QSettings settings(file, QSettings::IniFormat);
    settings.beginGroup("gestures");
    tap = parseAction(settings.value("tap", "swap").toString());
    swipeUp = parseAction(settings.value("swipe_up", "prev").toString());
    swipeDown = parseAction(settings.value("swipe_down", "next").toString());
    swipeLeft = parseAction(settings.value("swipe_left", "none").toString());
    swipeRight = parseAction(settings.value("swipe_right", "none").toString());
    settings.endGroup();
    settings.beginGroup("keys");
    foreach (const QString &name, settings.childKeys()) {
        bool ok = false;
        const int code = name.toInt(&ok, 0);
        if (!ok || code < 0 || code > KEY_MAX) {
            nh_log("touchremote: ignoring key '%s' in config", name.toUtf8().constData());
            continue;
        }
        keys.insert(code, parseAction(settings.value(name).toString()));
    }
    settings.endGroup();
    settings.beginGroup("options");
    swapWhenInverted = settings.value("swap_when_inverted", true).toBool();
    grabTouchDevices = settings.value("grab_touch_devices", true).toBool();
    grabOtherDevices = settings.value("grab_other_devices", false).toBool();
    viewMethods = settings.value("inject", "keys").toString().trimmed().toLower() == "view";
    powerActivity = settings.value("power_activity", true).toBool();
    autoConnect = settings.value("auto_connect", true).toBool();
    wifiHoldSeconds = settings.value("wifi_hold", 30).toInt();
    if (wifiHoldSeconds < 0 || wifiHoldSeconds > 300)
        wifiHoldSeconds = 30;
    verbose = settings.value("verbose", false).toBool();
    settings.endGroup();
    return true;
}

Action Settings::forGesture(Gesture gesture) const {
    switch (gesture) {
    case GestureTap: return tap;
    case GestureSwipeUp: return swipeUp;
    case GestureSwipeDown: return swipeDown;
    case GestureSwipeLeft: return swipeLeft;
    case GestureSwipeRight: return swipeRight;
    default: return ActionNone;
    }
}

// --- Device

Device::Device(Controller *owner, const QString &path, int fd, const QString &name, bool touch, bool grabbed)
    : QObject(owner), owner(owner), devicePath(path), fd(fd), deviceName(name), touch(touch),
      notifier(new QSocketNotifier(fd, QSocketNotifier::Read, this)), translator(*this) {
    connect(notifier, SIGNAL(activated(int)), this, SLOT(readable()));
    nh_log("touchremote: opened %s '%s' (%s%s)", path.toUtf8().constData(), name.toUtf8().constData(),
           touch ? "touch-style" : "keys", grabbed ? ", grabbed" : "");
}

Device::~Device() {
    notifier->setEnabled(false);
    delete notifier;
    notifier = 0;
    if (fd >= 0)
        ::close(fd);
}

void Device::readable() {
    struct input_event event;
    for (;;) {
        const ssize_t n = ::read(fd, &event, sizeof(event));
        if (n == (ssize_t)sizeof(event)) {
            if (owner->settings().verbose)
                nh_log("touchremote: %s event type=%u code=%u value=%d", devicePath.toUtf8().constData(),
                       (unsigned)event.type, (unsigned)event.code, (int)event.value);
            RawEvent raw = {event.type, event.code, event.value};
            translator.feed(raw);
            continue;
        }
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0 && errno == EAGAIN)
            return;
        nh_log("touchremote: lost %s (%s)", devicePath.toUtf8().constData(), n < 0 ? strerror(errno) : "eof");
        owner->deviceLost(this);  // deletes this later; do not touch members afterwards
        return;
    }
}

void Device::gesture(Gesture gesture) {
    const Action action = owner->settings().forGesture(gesture);
    nh_log("touchremote: %s %s -> %s", deviceName.toUtf8().constData(), gestureName(gesture), actionName(action));
    owner->perform(action, deviceName);
}

void Device::key(uint16_t code, int32_t value) {
    if (value != 1)  // act on presses only; releases and repeats are ignored
        return;
    const QHash<int, Action> &keys = owner->settings().keys;
    const Action action = keys.value(code, ActionNone);
    if (action == ActionNone) {
        if (owner->settings().verbose || !keys.isEmpty())
            nh_log("touchremote: %s key %u (unmapped)", deviceName.toUtf8().constData(), (unsigned)code);
        return;
    }
    nh_log("touchremote: %s key %u -> %s", deviceName.toUtf8().constData(), (unsigned)code, actionName(action));
    owner->perform(action, deviceName);
}

// --- Controller

Controller::Controller()
    : QObject(0), watcher(0), bindTimer(0), fallbackTimer(0), dumpTimer(0), connectTimer(0), wakeTimer(0),
      connectAttempts(0), wakeClock(0),
      bindTicks(0), started(false), powered(false), swapped(false) {
    lastHeartbeat.invalidate();
    lastConnectedAt.invalidate();
    lastRemoteOpenedAt.invalidate();
    lastAttempt.invalidate();
    wifiHold.invalidate();
    // Firmware 4 on MediaTek devices runs Kobo's bluedroid bridge under its own
    // bus name; other devices and firmware 5+ use BlueZ's. Both speak the
    // org.bluez interfaces.
    service = qgetenv("PLATFORM") == "mt8113t-ntx" ? QStringLiteral("com.kobo.mtk.bluedroid")
                                                   : QStringLiteral("org.bluez");
}

void Controller::start() {
    if (started)
        return;
    started = true;
    nh_log("touchremote: starting (version %s)", NH_VERSION);
    ::mkdir(TR_CONFIG_DIR, 0755);
    reloadSettings();
    loadKnownIds();
    loadRemotes();

    watcher = new QFileSystemWatcher(this);
    watcher->addPath(QString::fromLatin1(inputDirectory));
    connect(watcher, SIGNAL(directoryChanged(QString)), this, SLOT(inputDirectoryChanged(QString)));

    bindTimer = new QTimer(this);
    bindTimer->setInterval(1000);
    connect(bindTimer, SIGNAL(timeout()), this, SLOT(bindTick()));
    fallbackTimer = new QTimer(this);
    fallbackTimer->setInterval(fallbackIntervalMs);
    connect(fallbackTimer, SIGNAL(timeout()), this, SLOT(fallbackTick()));
    dumpTimer = new QTimer(this);
    dumpTimer->setSingleShot(true);
    dumpTimer->setInterval(dumpDelayMs);
    connect(dumpTimer, SIGNAL(timeout()), this, SLOT(dumpLog()));
    connectTimer = new QTimer(this);
    connectTimer->setSingleShot(true);
    connect(connectTimer, SIGNAL(timeout()), this, SLOT(connectTick()));
    // Qt's monotonic clock stops during suspend; the wall clock jumping ahead
    // between two ticks means the Kobo just woke up.
    wakeTimer = new QTimer(this);
    wakeTimer->setInterval(wakeTickMs);
    connect(wakeTimer, SIGNAL(timeout()), this, SLOT(wakeTick()));
    wakeClock = long(time(0));
    wakeTimer->start();

    // Only ever observe the Bluetooth service. A method call to a name that
    // is not running makes D-Bus launch the daemon, and a daemon started that
    // way, before Nickel powers the radio, leaves Nickel with a dead adapter.
    // Match rules and the service watcher never activate anything.
    QDBusConnection bus = QDBusConnection::systemBus();
    bus.connect(service, QString(), QStringLiteral("org.freedesktop.DBus.Properties"),
                QStringLiteral("PropertiesChanged"), this, SLOT(bluetoothSignal(QDBusMessage)));
    bus.connect(service, QString(), QStringLiteral("org.freedesktop.DBus.ObjectManager"),
                QStringLiteral("InterfacesAdded"), this, SLOT(bluetoothSignal(QDBusMessage)));
    QDBusServiceWatcher *serviceWatcher =
        new QDBusServiceWatcher(service, bus, QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(serviceWatcher, SIGNAL(serviceOwnerChanged(QString,QString,QString)), this,
            SLOT(serviceOwnerChanged(QString,QString,QString)));
    const bool running = bus.interface() && bus.interface()->isServiceRegistered(service);
    nh_log("touchremote: Bluetooth service %s is %s", service.toLatin1().constData(),
           running ? "running" : "not running");
    if (running)
        queryAdapter();

    bindUnclaimedHidDevices();
    scanInputDevices();
}

// Keeps the current settings when the file is unreachable, as during a USB
// connection, and the defaults when it was never created.
void Controller::reloadSettings() {
    Settings fresh;
    if (fresh.load(QString::fromLatin1(configFile)))
        config = fresh;
    nh_log("touchremote: config tap=%s up=%s down=%s left=%s right=%s keys=%d swap_when_inverted=%d grab=%d/%d "
           "inject=%s",
           actionName(config.tap), actionName(config.swipeUp), actionName(config.swipeDown),
           actionName(config.swipeLeft), actionName(config.swipeRight), config.keys.size(),
           config.swapWhenInverted, config.grabTouchDevices, config.grabOtherDevices,
           config.viewMethods ? "view" : "keys");
}

// Dynamic hid-generic IDs only last until reboot. Re-register every device we
// ever bound so a known remote gets its input node from the kernel on its own.
void Controller::loadKnownIds() {
    QFile file(QString::fromLatin1(knownIdsFile));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;
    foreach (const QByteArray &line, file.readAll().split('\n')) {
        const QString id = QString::fromLatin1(line).trimmed();
        unsigned bus, vendor, product;
        if (sscanf(id.toLatin1().constData(), "%x %x %x", &bus, &vendor, &product) != 3)
            continue;
        knownIds.insert(id);
        if (writeNewId(id))
            nh_log("touchremote: registered known device %s with hid-generic", id.toLatin1().constData());
    }
}

void Controller::rememberHidId(const QString &id) {
    if (knownIds.contains(id))
        return;
    knownIds.insert(id);
    QFile file(QString::fromLatin1(knownIdsFile));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        nh_log("touchremote: cannot write %s", knownIdsFile);
        return;
    }
    foreach (const QString &known, knownIds)
        file.write((known + "\n").toLatin1());
    file.close();
}

bool Controller::writeNewId(const QString &id) {
    const int fd = ::open(hidGenericNewId, O_WRONLY | O_CLOEXEC);
    if (fd < 0) {
        nh_log("touchremote: cannot open %s (%s)", hidGenericNewId, strerror(errno));
        return false;
    }
    const QByteArray text = id.toLatin1();
    const bool ok = ::write(fd, text.constData(), text.size()) == text.size();
    if (!ok)
        nh_log("touchremote: writing '%s' to hid-generic failed (%s)", text.constData(), strerror(errno));
    ::close(fd);
    return ok;
}

// The Kobo kernel tags a descriptor with a digitizer contact ID as multitouch
// and leaves it to hid-multitouch, which the kernel does not include. Hand such
// driverless Bluetooth HID devices to hid-generic, whose dynamic IDs match any
// HID group; that creates the /dev/input node.
void Controller::bindUnclaimedHidDevices() {
    DIR *dir = opendir(hidDevices);
    if (!dir)
        return;
    while (struct dirent *entry = readdir(dir)) {
        unsigned bus, vendor, product;
        if (sscanf(entry->d_name, "%4x:%4x:%4x.", &bus, &vendor, &product) != 3 || bus != BUS_BLUETOOTH)
            continue;
        char link[256];
        snprintf(link, sizeof(link), "%s/%s/driver", hidDevices, entry->d_name);
        struct stat st;
        if (lstat(link, &st) == 0)
            continue;  // a driver already owns it
        const QString id = QString::fromLatin1("%1 %2 %3")
                               .arg(bus, 4, 16, QLatin1Char('0'))
                               .arg(vendor, 4, 16, QLatin1Char('0'))
                               .arg(product, 4, 16, QLatin1Char('0'))
                               .toUpper();
        nh_log("touchremote: Bluetooth HID device %s has no driver, binding hid-generic", entry->d_name);
        if (writeNewId(id))
            rememberHidId(id);
    }
    closedir(dir);
}

void Controller::startBindBurst() {
    bindTicks = bindBurstTicks;
    bindUnclaimedHidDevices();
    if (!bindTimer->isActive())
        bindTimer->start();
}

void Controller::bindTick() {
    bindUnclaimedHidDevices();
    if (--bindTicks <= 0)
        bindTimer->stop();
}

void Controller::fallbackTick() {
    bindUnclaimedHidDevices();
    scanInputDevices();
}

void Controller::setPowered(bool value) {
    if (powered == value)
        return;
    powered = value;
    nh_log("touchremote: Bluetooth %s", value ? "on" : "off");
    if (value) {
        startBindBurst();
        fallbackTimer->start();
        startConnectBurst(0);
    } else {
        bindTimer->stop();
        fallbackTimer->stop();
        connectTimer->stop();
    }
}

void Controller::serviceOwnerChanged(const QString &, const QString &, const QString &owner) {
    nh_log("touchremote: Bluetooth service %s", owner.isEmpty() ? "stopped" : "started");
    if (owner.isEmpty())
        setPowered(false);
    else
        queryAdapter();
}

// Called only while the service has an owner, so it cannot activate it.
void Controller::queryAdapter() {
    QDBusMessage request = QDBusMessage::createMethodCall(service, QString::fromLatin1(adapterPath),
                                                          QStringLiteral("org.freedesktop.DBus.Properties"),
                                                          QStringLiteral("Get"));
    request << QStringLiteral("org.bluez.Adapter1") << QStringLiteral("Powered");
    QDBusPendingCallWatcher *watcher =
        new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(request, 1500), this);
    connect(watcher, SIGNAL(finished(QDBusPendingCallWatcher*)), this,
            SLOT(adapterQueryFinished(QDBusPendingCallWatcher*)));
}

void Controller::adapterQueryFinished(QDBusPendingCallWatcher *watcher) {
    const QDBusPendingReply<QDBusVariant> reply = *watcher;
    watcher->deleteLater();
    if (reply.isError())
        return;
    if (reply.value().variant().toBool())
        setPowered(true);
}

void Controller::bluetoothSignal(const QDBusMessage &message) {
    const QList<QVariant> arguments = message.arguments();
    if (message.member() == QLatin1String("InterfacesAdded")) {
        startBindBurst();
        const QString added = arguments.isEmpty() ? QString() : arguments.at(0).value<QDBusObjectPath>().path();
        if (remotes.contains(added) && !remoteOpen())
            connectNow(added, "appeared");
        return;
    }
    if (arguments.size() < 2)
        return;
    const QString interface = arguments.at(0).toString();
    QVariantMap changed;
    if (arguments.at(1).canConvert<QDBusArgument>())
        changed = qdbus_cast<QVariantMap>(arguments.at(1).value<QDBusArgument>());
    else
        changed = arguments.at(1).toMap();
    if (interface == QLatin1String("org.bluez.Adapter1")) {
        nh_log("touchremote: adapter changed: %s", QStringList(changed.keys()).join(",").toLatin1().constData());
        if (changed.contains(QStringLiteral("Powered")))
            setPowered(changed.value(QStringLiteral("Powered")).toBool());
    } else if (interface == QLatin1String("org.bluez.Device1")) {
        const QString path = message.path();
        if (changed.contains(QStringLiteral("Connected"))) {
            if (changed.value(QStringLiteral("Connected")).toBool()) {
                nh_log("touchremote: %s connected", path.toLatin1().constData());
                lastConnectedPath = path;
                lastConnectedAt.start();
                if (remoteOpen() && lastRemoteOpenedAt.isValid() && lastRemoteOpenedAt.elapsed() < 60000)
                    rememberRemote(path);
                reloadSettings();
                startBindBurst();
            } else if (remotes.contains(path)) {
                nh_log("touchremote: %s disconnected", path.toLatin1().constData());
                startConnectBurst(2);
            }
        } else if (changed.contains(QStringLiteral("RSSI")) && remotes.contains(path) && !remoteOpen()) {
            // Nickel's settings page is scanning and sees a known remote.
            connectNow(path, "seen in a scan");
        }
    }
}

void Controller::inputDirectoryChanged(const QString &) {
    scanInputDevices();
}

void Controller::scanInputDevices() {
    DIR *dir = opendir(inputDirectory);
    if (!dir)
        return;
    while (struct dirent *entry = readdir(dir)) {
        if (strncmp(entry->d_name, "event", 5) != 0)
            continue;
        const QString path =
            QString::fromLatin1("%1/%2").arg(QLatin1String(inputDirectory), QLatin1String(entry->d_name));
        bool open = false;
        foreach (Device *device, devices)
            if (device->path() == path)
                open = true;
        if (open)
            continue;
        const int fd = ::open(path.toLatin1().constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0)
            continue;
        struct input_id id;
        memset(&id, 0, sizeof(id));
        if (ioctl(fd, EVIOCGID, &id) < 0 || id.bustype != BUS_BLUETOOTH) {
            ::close(fd);
            continue;
        }
        char name[128] = "";
        if (ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name) < 0)
            name[0] = 0;
        unsigned long evBits[BITS_FOR(EV_MAX)], absBits[BITS_FOR(ABS_MAX)], keyBits[BITS_FOR(KEY_MAX)];
        memset(evBits, 0, sizeof(evBits));
        memset(absBits, 0, sizeof(absBits));
        memset(keyBits, 0, sizeof(keyBits));
        ioctl(fd, EVIOCGBIT(0, sizeof(evBits)), evBits);
        if (testBit(evBits, EV_ABS))
            ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(absBits)), absBits);
        if (testBit(evBits, EV_KEY))
            ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keyBits)), keyBits);
        const bool touch = testBit(evBits, EV_ABS) &&
                           ((testBit(absBits, ABS_X) && testBit(absBits, ABS_Y)) ||
                            (testBit(absBits, ABS_MT_POSITION_X) && testBit(absBits, ABS_MT_POSITION_Y))) &&
                           testBit(keyBits, BTN_TOUCH);
        if (!touch && config.keys.isEmpty() && !config.grabOtherDevices) {
            // A plain keyboard or remote that Nickel handles itself.
            ::close(fd);
            continue;
        }
        const bool wantGrab = touch ? config.grabTouchDevices : config.grabOtherDevices;
        bool grabbed = false;
        if (wantGrab) {
            grabbed = ioctl(fd, EVIOCGRAB, 1) == 0;
            if (!grabbed)
                nh_log("touchremote: cannot grab %s (%s)", path.toLatin1().constData(), strerror(errno));
        }
        devices.append(new Device(this, path, fd, QString::fromUtf8(name), touch, grabbed));
        if (touch) {
            // The Device1 that reported Connected just before is this remote.
            if (lastConnectedAt.isValid() && lastConnectedAt.elapsed() < 60000)
                rememberRemote(lastConnectedPath);
            lastRemoteOpenedAt.start();
            connectTimer->stop();
        }
        armDumpLog();
    }
    closedir(dir);
}

void Controller::deviceLost(Device *device) {
    devices.removeAll(device);
    device->deleteLater();
    if (!remoteOpen())
        startConnectBurst(2);
}

// --- auto-connect

void Controller::loadRemotes() {
    QFile file(QString::fromLatin1(remotesFile));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;
    foreach (const QByteArray &line, file.readAll().split('\n')) {
        const QString path = QString::fromLatin1(line).trimmed();
        if (path.startsWith('/') && !remotes.contains(path))
            remotes.append(path);
    }
    if (!remotes.isEmpty())
        nh_log("touchremote: last remote %s", remotes.last().toLatin1().constData());
}

void Controller::rememberRemote(const QString &path) {
    if (path.isEmpty() || (!remotes.isEmpty() && remotes.last() == path))
        return;
    remotes.removeAll(path);
    remotes.append(path);
    QFile file(QString::fromLatin1(remotesFile));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        nh_log("touchremote: cannot write %s", remotesFile);
        return;
    }
    foreach (const QString &remote, remotes)
        file.write((remote + "\n").toLatin1());
    file.close();
    nh_log("touchremote: remembered remote %s", path.toLatin1().constData());
}

bool Controller::remoteOpen() const {
    foreach (Device *device, devices)
        if (device->isTouch())
            return true;
    return false;
}

// Nickel removes the Wi-Fi driver when Wi-Fi is off, so the interface exists
// only while Wi-Fi is on.
bool Controller::wifiUp() const {
    return ::access("/sys/class/net/wlan0", F_OK) == 0;
}

void Controller::startConnectBurst(int attemptsDone) {
    if (!config.autoConnect || !powered || remotes.isEmpty() || remoteOpen())
        return;
    connectAttempts = attemptsDone;
    wifiHold.invalidate();
    if (!connectTimer->isActive() || connectAttempts == 0)
        connectTimer->start(0);
}

void Controller::connectTick() {
    if (!config.autoConnect || !powered || remotes.isEmpty() || remoteOpen())
        return;
    // Bluetooth and Wi-Fi share the radio. Paging for a remote while Wi-Fi is
    // associating or syncing after a wake can make that transfer fail, so the
    // first attempt of a burst waits while Wi-Fi is up, for a bounded time.
    if (config.wifiHoldSeconds > 0 && connectAttempts == 0 && wifiUp()) {
        if (!wifiHold.isValid()) {
            wifiHold.start();
            nh_log("touchremote: Wi-Fi is up, holding Bluetooth connection attempts for up to %d s",
                   config.wifiHoldSeconds);
        }
        if (wifiHold.elapsed() < qint64(config.wifiHoldSeconds) * 1000) {
            connectTimer->start(5000);
            return;
        }
    }
    if (lastAttempt.isValid() && lastAttempt.elapsed() < connectMinSpacingMs) {
        connectTimer->start(connectMinSpacingMs - int(lastAttempt.elapsed()));
        return;
    }
    ++connectAttempts;
    connectNow(remotes.last(), connectAttempts <= connectBurstAttempts ? "burst" : "slow");
    connectTimer->start(connectAttempts < connectBurstAttempts ? connectBurstIntervalMs : connectSlowIntervalMs);
}

// Ask the daemon to connect a known remote: the most recently used one from
// the timer, or the one a scan just saw. Only while the adapter is powered,
// so the service is running and the call cannot start it.
void Controller::connectNow(const QString &path, const char *reason) {
    if (!config.autoConnect || !powered || path.isEmpty())
        return;
    if (lastAttempt.isValid() && lastAttempt.elapsed() < connectMinSpacingMs)
        return;
    nh_log("touchremote: connecting %s (%s)", path.toLatin1().constData(), reason);
    lastAttempt.start();
    QDBusMessage request =
        QDBusMessage::createMethodCall(service, path, QStringLiteral("org.bluez.Device1"), QStringLiteral("Connect"));
    QDBusPendingCallWatcher *watcher =
        new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(request, 30000), this);
    connect(watcher, SIGNAL(finished(QDBusPendingCallWatcher*)), this,
            SLOT(connectFinished(QDBusPendingCallWatcher*)));
}

void Controller::connectFinished(QDBusPendingCallWatcher *watcher) {
    const QDBusPendingReply<> reply = *watcher;
    watcher->deleteLater();
    if (reply.isError())
        nh_log("touchremote: connect: %s", reply.error().message().toUtf8().constData());
    else
        nh_log("touchremote: connect call returned");
}

void Controller::wakeTick() {
    const long now = long(time(0));
    if (wakeClock && now - wakeClock > wakeGapSeconds) {
        nh_log("touchremote: woke up");
        if (!remoteOpen())
            startConnectBurst(0);
    }
    wakeClock = now;
}

// --- actions

void Controller::perform(Action action, const QString &source) {
    switch (action) {
    case ActionSwap:
        swapped = !swapped;
        nh_log("touchremote: %s swapped page turns %s", source.toUtf8().constData(), swapped ? "on" : "off");
        return;
    case ActionInvert:
        invertScreen();
        heartbeat();
        if (config.powerActivity)
            reportActivity();
        return;
    case ActionNext:
    case ActionPrev: {
        int direction = action == ActionNext ? 1 : -1;
        if (swapped)
            direction = -direction;
        if (config.swapWhenInverted) {
            const QScreen *screen = QGuiApplication::primaryScreen();
            if (screen && (screen->orientation() == Qt::InvertedPortraitOrientation ||
                           screen->orientation() == Qt::InvertedLandscapeOrientation))
                direction = -direction;
        }
        bool done = false;
        if (config.viewMethods)
            done = turnNickelPage(direction);
        if (!done)
            sendPageKey(direction);
        heartbeat();
        if (config.powerActivity)
            reportActivity();
        return;
    }
    default:
        return;
    }
}

// Deliver the arrow key a Bluetooth keyboard would send. Nickel's reader and
// Kobalt both turn pages on Right/Left. The scan code marks the event as
// hardware input for Nickel's Bluetooth classifier (evdev code plus 8, as
// Qt's evdev keyboard handler reports it).
bool Controller::sendPageKey(int direction) {
    QObject *target = QGuiApplication::focusWindow();
    if (!target)
        target = QApplication::focusWidget();
    if (!target)
        target = QApplication::activeWindow();
    if (!target) {
        nh_log("touchremote: no window to receive the key");
        return false;
    }
    const int key = direction > 0 ? Qt::Key_Right : Qt::Key_Left;
    const quint32 scanCode = (direction > 0 ? KEY_RIGHT : KEY_LEFT) + 8;
    QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier, scanCode, 0, 0);
    QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier, scanCode, 0, 0);
    QCoreApplication::sendEvent(target, &press);
    QCoreApplication::sendEvent(target, &release);
    return true;
}

// Turn the screen upside down, or back. With swap_when_inverted on, the
// page-turn directions follow, so the same ring button keeps turning forward
// when the reader is held the other way round.
void Controller::invertScreen() {
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen)
        return;
    Qt::ScreenOrientation current = screen->orientation();
    if (current == Qt::PrimaryOrientation)
        current = screen->primaryOrientation();
    Qt::ScreenOrientation target;
    switch (current) {
    case Qt::PortraitOrientation: target = Qt::InvertedPortraitOrientation; break;
    case Qt::InvertedPortraitOrientation: target = Qt::PortraitOrientation; break;
    case Qt::LandscapeOrientation: target = Qt::InvertedLandscapeOrientation; break;
    case Qt::InvertedLandscapeOrientation: target = Qt::LandscapeOrientation; break;
    default: return;
    }
    nh_log("touchremote: turning the screen from %d to %d", int(current), int(target));
    turnScreen(screen, target);
}

// The same call Nickel's rotation (and Kobalt's) makes: the Kobo platform
// plugin rotates the framebuffer when the screen orientation changes. Qt 5
// drops an orientation outside the screen's update mask, which Nickel narrows
// outside its reader, so widen the mask while the screen is turned and give
// Nickel's mask back once the screen is in an orientation it allowed.
void Controller::turnScreen(QScreen *screen, Qt::ScreenOrientation orientation) {
    if (screen->orientation() == orientation)
        return;
    static Qt::ScreenOrientations nickelMask;
    static bool widened = false;
    if (!widened)
        nickelMask = screen->orientationUpdateMask();
    widened = !(nickelMask & orientation);
    screen->setOrientationUpdateMask(nickelMask | orientation);
    QWindowSystemInterface::handleScreenOrientationChange(screen, orientation);
    QWindowSystemInterface::flushWindowSystemEvents(QEventLoop::ExcludeUserInputEvents);
    if (!widened)
        screen->setOrientationUpdateMask(nickelMask);
}

// Optional: call Nickel's own reading view directly (as kobo-btpt does).
bool Controller::turnNickelPage(int direction) {
    if (!nickel::mainWindowControllerSharedInstance || !nickel::mainWindowControllerCurrentView)
        return false;
    void *controller = nickel::mainWindowControllerSharedInstance();
    if (!controller)
        return false;
    QWidget *view = nickel::mainWindowControllerCurrentView(controller);
    if (!view || view->objectName() != QLatin1String("ReadingView"))
        return false;
    QMetaObject::invokeMethod(view, direction > 0 ? "nextPage" : "prevPage", Qt::QueuedConnection);
    return true;
}

// Nickel switches Bluetooth off after a period without remote input. Post the
// event its own remote handling uses, at most every few seconds.
void Controller::heartbeat() {
    if (!nickel::bluetoothHeartbeatEventType || !QCoreApplication::instance())
        return;
    if (lastHeartbeat.isValid() && lastHeartbeat.elapsed() < heartbeatIntervalMs)
        return;
    const int type = nickel::bluetoothHeartbeatEventType();
    if (type < QEvent::User || type > QEvent::MaxUser)
        return;
    QCoreApplication::postEvent(QCoreApplication::instance(), new QEvent(static_cast<QEvent::Type>(type)));
    lastHeartbeat.start();
}

// Reset Nickel's inactivity timer so reading with the remote alone does not
// put the device to sleep.
void Controller::reportActivity() {
    if (!nickel::timeEventType || !nickel::powerManagerSharedInstance || !nickel::powerManagerFilter)
        return;
    QObject *manager = nickel::powerManagerSharedInstance();
    if (!manager)
        return;
    const int type = nickel::timeEventType();
    if (type < QEvent::User || type > QEvent::MaxUser)
        return;
    QEvent event(static_cast<QEvent::Type>(type));
    nickel::powerManagerFilter(manager, this, &event);
}

// --- debugging

// When the flag file exists while a remote connects, copy the system log to
// the user storage a minute later, so the first presses are included.
void Controller::armDumpLog() {
    if (::access(dumpLogFlag, F_OK) != 0 || dumpTimer->isActive())
        return;
    nh_log("touchremote: log dump in %d s", dumpDelayMs / 1000);
    dumpTimer->start();
}

void Controller::dumpLog() {
    nh_delete_file(dumpLogFlag);
    nh_dump_log();
}

}
