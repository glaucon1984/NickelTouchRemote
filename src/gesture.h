// Gesture translation for touch-screen style Bluetooth remotes.
//
// Some cheap Bluetooth remotes ("TikTok rings") present themselves as a
// single-finger touch screen: every button sends a simulated finger gesture
// (BTN_TOUCH down, a few ABS_X/ABS_Y positions, BTN_TOUCH up) instead of a key.
// This translator folds one contact into one gesture and passes plain keys
// through unchanged. It has no Qt or Nickel dependency so it can be unit
// tested on the host.
#ifndef TOUCHREMOTE_GESTURE_H
#define TOUCHREMOTE_GESTURE_H

#include <stdint.h>

namespace touchremote {

enum Gesture {
    GestureNone = 0,
    GestureTap,
    GestureSwipeUp,    // finger moved towards smaller Y
    GestureSwipeDown,  // finger moved towards larger Y
    GestureSwipeLeft,  // finger moved towards smaller X
    GestureSwipeRight  // finger moved towards larger X
};

const char *gestureName(Gesture gesture);

struct RawEvent {
    uint16_t type;
    uint16_t code;
    int32_t value;
};

class GestureTranslator {
public:
    class Sink {
    public:
        virtual void gesture(Gesture gesture) = 0;
        virtual void key(uint16_t code, int32_t value) = 0;
    protected:
        ~Sink() {}
    };

    explicit GestureTranslator(Sink &sink);

    // Feed every event of the device in order, including EV_SYN.
    void feed(const RawEvent &event);
    void reset();

private:
    Gesture classify() const;
    void emitGesture(Gesture gesture);

    Sink &sink;
    bool active;    // contact in progress
    bool started;   // start position recorded
    bool emitted;   // gesture already delivered for this contact
    bool inFrame;   // current report carries a touch button
    bool haveLast;
    int32_t x, y;
    int32_t startX, startY;
    int32_t lastX, lastY;
    int32_t maxCoord;
};

}

#endif
