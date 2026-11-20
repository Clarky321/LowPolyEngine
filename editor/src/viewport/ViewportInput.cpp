#include "ViewportInput.hpp"

namespace editor {

namespace {

float axis(bool positive, bool negative) noexcept
{
    return (positive ? 1.0F : 0.0F) - (negative ? 1.0F : 0.0F);
}

} // namespace

void ViewportInput::attach(Rid viewport)
{
    detach();
    viewport_ = viewport;
    Rwidget_set_event_callback(viewport_, &ViewportInput::onEvent, this);
}

void ViewportInput::detach() noexcept
{
    if (viewport_ != R_INVALID_ID && Rwidget_is_valid(viewport_) != R_FALSE) {
        Rwidget_set_event_callback(viewport_, nullptr, nullptr);
    }
    viewport_ = R_INVALID_ID;
    reset();
}

engine::camera::FlyInput ViewportInput::take() noexcept
{
    engine::camera::FlyInput fly;
    fly.move = engine::math::Vec3{axis(keys_.right, keys_.left), axis(keys_.up, keys_.down),
                                  axis(keys_.forward, keys_.back)};
    fly.fast = keys_.leftShift || keys_.rightShift;
    fly.look = look_;
    fly.speedSteps = wheelSteps_;
    look_ = engine::math::Vec2{0.0F};
    wheelSteps_ = 0.0F;
    return fly;
}

int ViewportInput::onEvent(Rid /*widget*/, const REvent* event, void* user) noexcept
{
    if (event == nullptr || user == nullptr) {
        return R_FALSE;
    }
    return static_cast<ViewportInput*>(user)->handle(*event) ? R_TRUE : R_FALSE;
}

bool ViewportInput::handle(const REvent& event) noexcept
{
    switch (event.type) {
    case R_EVENT_MOUSE_DOWN:
        // Not consumed: the viewport itself then takes the press, so it keeps
        // the pointer while the button is held, also outside the widget.
        if (event.button == R_MOUSE_RIGHT) {
            looking_ = true;
        }
        return false;
    case R_EVENT_MOUSE_UP:
        if (event.button == R_MOUSE_RIGHT) {
            looking_ = false;
        }
        return false;
    case R_EVENT_MOUSE_MOVE:
        if (!looking_) {
            return false;
        }
        {
            // Raven's logical units to pixels, the fly controller's unit.
            const float scale = Rwindow_get_scale(event.window);
            const float toPixels = scale > 0.0F ? scale : 1.0F;
            look_ += engine::math::Vec2{event.delta_x, event.delta_y} * toPixels;
        }
        return true;
    case R_EVENT_MOUSE_WHEEL:
        wheelSteps_ += event.wheel_y;
        return true;
    case R_EVENT_KEY_DOWN:
    case R_EVENT_KEY_UP:
        return setKey(event.key, event.type == R_EVENT_KEY_DOWN);
    case R_EVENT_FOCUS_LOST:
        reset();
        return false;
    default:
        return false;
    }
}

bool ViewportInput::setKey(RKey key, bool down) noexcept
{
    switch (key) {
    case R_KEY_W:
        keys_.forward = down;
        return true;
    case R_KEY_S:
        keys_.back = down;
        return true;
    case R_KEY_A:
        keys_.left = down;
        return true;
    case R_KEY_D:
        keys_.right = down;
        return true;
    case R_KEY_E:
        keys_.up = down;
        return true;
    case R_KEY_Q:
        keys_.down = down;
        return true;
    case R_KEY_LEFT_SHIFT:
        keys_.leftShift = down;
        return true;
    case R_KEY_RIGHT_SHIFT:
        keys_.rightShift = down;
        return true;
    default:
        return false;
    }
}

void ViewportInput::reset() noexcept
{
    keys_ = Keys{};
    looking_ = false;
    look_ = engine::math::Vec2{0.0F};
    wheelSteps_ = 0.0F;
}

} // namespace editor
