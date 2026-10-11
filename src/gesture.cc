#include "gesture.h"

namespace touchremote {
namespace {
const uint16_t EV_SYN_ = 0x00, EV_KEY_ = 0x01, EV_ABS_ = 0x03;
const uint16_t SYN_REPORT_ = 0;
const uint16_t ABS_X_ = 0x00, ABS_Y_ = 0x01;
const uint16_t ABS_MT_POSITION_X_ = 0x35, ABS_MT_POSITION_Y_ = 0x36;
const uint16_t BTN_TOUCH_ = 0x14a;
const uint16_t BTN_DIGI_MIN_ = 0x140, BTN_DIGI_MAX_ = 0x14f;

int32_t absolute(int32_t value) {
    return value < 0 ? -value : value;
}
}

const char *gestureName(Gesture gesture) {
    switch (gesture) {
    case GestureTap: return "tap";
    case GestureSwipeUp: return "swipe_up";
    case GestureSwipeDown: return "swipe_down";
    case GestureSwipeLeft: return "swipe_left";
    case GestureSwipeRight: return "swipe_right";
    default: return "none";
    }
}

GestureTranslator::GestureTranslator(Sink &sink) : sink(sink) {
    reset();
}

void GestureTranslator::reset() {
    active = started = emitted = inFrame = haveLast = false;
    x = y = startX = startY = lastX = lastY = 0;
    maxCoord = 0;
}

// Movement below 5% of the largest coordinate seen so far counts as a tap.
Gesture GestureTranslator::classify() const {
    const int32_t fromX = started ? startX : 0, fromY = started ? startY : 0;
    const int32_t dx = (haveLast ? lastX : fromX) - fromX;
    const int32_t dy = (haveLast ? lastY : fromY) - fromY;
    int32_t threshold = maxCoord / 20;
    if (threshold < 10)
        threshold = 10;
    if (absolute(dx) < threshold && absolute(dy) < threshold)
        return GestureTap;
    if (absolute(dy) >= absolute(dx))
        return dy > 0 ? GestureSwipeDown : GestureSwipeUp;
    return dx > 0 ? GestureSwipeRight : GestureSwipeLeft;
}

void GestureTranslator::emitGesture(Gesture gesture) {
    emitted = true;
    sink.gesture(gesture);
}

void GestureTranslator::feed(const RawEvent &event) {
    if (event.type == EV_ABS_) {
        if (event.code == ABS_X_ || event.code == ABS_MT_POSITION_X_)
            x = event.value;
        else if (event.code == ABS_Y_ || event.code == ABS_MT_POSITION_Y_)
            y = event.value;
        else
            return;
        if (event.value > maxCoord)
            maxCoord = event.value;
        return;
    }
    if (event.type == EV_SYN_) {
        if (event.code == SYN_REPORT_) {
            if (active && !started) {
                startX = x;
                startY = y;
                started = true;
            } else if (active) {
                lastX = x;
                lastY = y;
                haveLast = true;
                // Fire a swipe as soon as its direction is clear instead of
                // waiting for the finger to lift, which happens several
                // reports later.
                if (!emitted) {
                    const Gesture gesture = classify();
                    if (gesture != GestureTap)
                        emitGesture(gesture);
                }
            }
            inFrame = false;
        }
        return;
    }
    if (event.type != EV_KEY_)
        return;
    if (event.code == BTN_TOUCH_) {
        inFrame = true;
        if (event.value == 1) {
            active = true;
            started = false;
            emitted = false;
            haveLast = false;
        } else if (event.value == 0 && active) {
            active = false;
            if (!emitted)
                emitGesture(classify());
        }
        return;
    }
    // Tool and contact buttons that accompany a touch contact are not buttons
    // the user pressed.
    if (inFrame || (event.code >= BTN_DIGI_MIN_ && event.code <= BTN_DIGI_MAX_))
        return;
    sink.key(event.code, event.value);
}

}
