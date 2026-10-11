// Host-side tests for the gesture translator, replaying the report sequences a
// WX02 ring sends: one contact per button press, tap for the middle button and
// vertical swipes for the other two. Also covers plain keys outside a contact.
#include "gesture.h"

#include <stdio.h>
#include <stdlib.h>

using namespace touchremote;

namespace {

const uint16_t EV_SYN = 0, EV_KEY = 1, EV_ABS = 3;
const uint16_t ABS_X = 0, ABS_Y = 1;
const uint16_t BTN_TOUCH = 0x14a, BTN_TOOL_FINGER = 0x145;
const uint16_t KEY_POWER = 116, KEY_VOLUMEUP = 115;

int failures = 0;

#define CHECK(condition)                                                              \
    do {                                                                              \
        if (!(condition)) {                                                           \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
            ++failures;                                                               \
        }                                                                             \
    } while (0)

struct Recorder : GestureTranslator::Sink {
    int gestures[6];
    int keys;
    uint16_t lastKey;
    int32_t lastValue;
    Recorder() : keys(0), lastKey(0), lastValue(0) {
        for (int i = 0; i < 6; ++i)
            gestures[i] = 0;
    }
    void gesture(Gesture gesture) { ++gestures[gesture]; }
    void key(uint16_t code, int32_t value) {
        ++keys;
        lastKey = code;
        lastValue = value;
    }
    int total() const {
        int sum = 0;
        for (int i = 0; i < 6; ++i)
            sum += gestures[i];
        return sum;
    }
};

void feed(GestureTranslator &t, uint16_t type, uint16_t code, int32_t value) {
    RawEvent event = {type, code, value};
    t.feed(event);
}

void frame(GestureTranslator &t, int32_t x, int32_t y, int touch = -1) {
    if (touch >= 0) {
        feed(t, EV_KEY, BTN_TOUCH, touch);
        feed(t, EV_KEY, BTN_TOOL_FINGER, touch);
    }
    feed(t, EV_ABS, ABS_X, x);
    feed(t, EV_ABS, ABS_Y, y);
    feed(t, EV_SYN, 0, 0);
}

void release(GestureTranslator &t) {
    feed(t, EV_KEY, BTN_TOUCH, 0);
    feed(t, EV_KEY, BTN_TOOL_FINGER, 0);
    feed(t, EV_SYN, 0, 0);
}

void testTap() {
    Recorder r;
    GestureTranslator t(r);
    frame(t, 500, 500, 1);
    frame(t, 500, 500);
    release(t);
    CHECK(r.gestures[GestureTap] == 1);
    CHECK(r.total() == 1);
    CHECK(r.keys == 0);  // the touch buttons never reach the key sink
}

void testSwipeDownFiresOnFirstClearMovement() {
    Recorder r;
    GestureTranslator t(r);
    frame(t, 500, 300, 1);
    CHECK(r.total() == 0);
    frame(t, 500, 310);  // 10 below the 5% threshold of 500
    CHECK(r.total() == 0);
    frame(t, 500, 340);  // 40 past it
    CHECK(r.gestures[GestureSwipeDown] == 1);
    frame(t, 500, 460);
    frame(t, 500, 600);
    release(t);
    CHECK(r.total() == 1);  // one gesture per contact
}

void testSwipeUpOnRelease() {
    Recorder r;
    GestureTranslator t(r);
    frame(t, 500, 700, 1);
    frame(t, 500, 690);  // never past the threshold while held
    release(t);
    CHECK(r.gestures[GestureTap] == 1);
    frame(t, 500, 700, 1);
    frame(t, 500, 600);
    release(t);
    CHECK(r.gestures[GestureSwipeUp] == 1);
    CHECK(r.total() == 2);
}

void testHorizontalSwipes() {
    Recorder r;
    GestureTranslator t(r);
    frame(t, 200, 500, 1);
    frame(t, 400, 510);
    release(t);
    frame(t, 800, 500, 1);
    frame(t, 500, 480);
    release(t);
    CHECK(r.gestures[GestureSwipeRight] == 1);
    CHECK(r.gestures[GestureSwipeLeft] == 1);
}

void testThresholdFloorForSmallRanges() {
    Recorder r;
    GestureTranslator t(r);
    frame(t, 20, 20, 1);
    frame(t, 20, 25);  // 5 is below the 10 unit floor
    release(t);
    CHECK(r.gestures[GestureTap] == 1);
}

void testPlainKeysPassThroughOutsideContacts() {
    Recorder r;
    GestureTranslator t(r);
    feed(t, EV_KEY, KEY_POWER, 1);
    feed(t, EV_SYN, 0, 0);
    feed(t, EV_KEY, KEY_POWER, 0);
    feed(t, EV_SYN, 0, 0);
    CHECK(r.keys == 2);
    CHECK(r.lastKey == KEY_POWER && r.lastValue == 0);
    // A key in the same report as a touch button is part of the contact.
    feed(t, EV_KEY, BTN_TOUCH, 1);
    feed(t, EV_KEY, KEY_VOLUMEUP, 1);
    frame(t, 500, 500);
    release(t);
    CHECK(r.keys == 2);
    CHECK(r.gestures[GestureTap] == 1);
}

void testResetDropsContact() {
    Recorder r;
    GestureTranslator t(r);
    frame(t, 500, 500, 1);
    t.reset();
    release(t);
    CHECK(r.total() == 0);
}

}

int main() {
    testTap();
    testSwipeDownFiresOnFirstClearMovement();
    testSwipeUpOnRelease();
    testHorizontalSwipes();
    testThresholdFloorForSmallRanges();
    testPlainKeysPassThroughOutsideContacts();
    testResetDropsContact();
    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("gesture tests passed\n");
    return 0;
}
