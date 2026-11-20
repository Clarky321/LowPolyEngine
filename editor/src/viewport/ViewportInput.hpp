#pragma once

// The editor camera's input (M5.3): the viewport widget's events, which Raven
// sends to the callback attached here (Rwidget_set_event_callback). The camera
// flies as in island-preview: hold the right mouse button in the viewport to
// look around, W/A/S/D to move, E up, Q down, Shift faster, the mouse wheel
// over the viewport changes the speed.
//
// Keys come only while the viewport has Raven's keyboard focus (a click in it
// gives it the focus): while a text field has the keyboard, typing never moves
// the camera. When the viewport loses the focus, every held key is forgotten
// and looking ends.

#include "engine/camera/FreeFlyController.hpp"
#include "engine/core/Math.hpp"

#include <Raven.h>

namespace editor {

class ViewportInput {
public:
    ViewportInput() = default;
    // Raven holds a pointer to the object while it is attached.
    ViewportInput(const ViewportInput&) = delete;
    ViewportInput& operator=(const ViewportInput&) = delete;
    ViewportInput(ViewportInput&&) = delete;
    ViewportInput& operator=(ViewportInput&&) = delete;
    ~ViewportInput() { detach(); }

    // Receives the events of `viewport` from now on, in place of any other
    // callback on that widget, and of the widget attached before.
    void attach(Rid viewport);
    // Stops receiving events; nothing happens when nothing is attached or the
    // widget is gone.
    void detach() noexcept;

    // What the user asked of the camera since the previous call: the held keys,
    // and the pointer movement (in pixels) and wheel steps gathered meanwhile,
    // which this call resets.
    [[nodiscard]] engine::camera::FlyInput take() noexcept;

    // The right mouse button is held after a press in the viewport.
    [[nodiscard]] bool looking() const noexcept { return looking_; }

private:
    struct Keys {
        bool forward = false;
        bool back = false;
        bool left = false;
        bool right = false;
        bool up = false;
        bool down = false;
        bool leftShift = false;
        bool rightShift = false;
    };

    static int onEvent(Rid widget, const REvent* event, void* user) noexcept;
    // Whether the event was used; an unused one goes on to the viewport.
    bool handle(const REvent& event) noexcept;
    bool setKey(RKey key, bool down) noexcept;
    void reset() noexcept;

    Rid viewport_ = R_INVALID_ID;
    Keys keys_;
    bool looking_ = false;
    engine::math::Vec2 look_{0.0F};
    float wheelSteps_ = 0.0F;
};

} // namespace editor
