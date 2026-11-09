#include "engine/platform/Window.hpp"

#include <string>

// Platform-independent part of the window API, testable without a display.

namespace engine::platform {

std::optional<core::Error> validate(const WindowDesc& desc)
{
    const auto checkExtent = [](const char* name, int value) -> std::optional<core::Error> {
        if (value <= 0 || value > kMaxWindowExtent) {
            return core::Error{std::string("window ") + name + " must be in 1.." +
                               std::to_string(kMaxWindowExtent) + ", got " + std::to_string(value)};
        }
        return std::nullopt;
    };

    if (auto error = checkExtent("width", desc.width)) {
        return error;
    }
    return checkExtent("height", desc.height);
}

} // namespace engine::platform
