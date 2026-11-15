#include "EditorOptions.hpp"

#include "engine/platform/Window.hpp"

#include <charconv>
#include <string>
#include <system_error>

namespace editor {

namespace {

template <typename T>
std::optional<T> parseNumber(std::string_view text)
{
    T value{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || error != std::errc{} || end != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

engine::core::Error invalid(std::string_view option, std::string_view value,
                            std::string_view expected)
{
    return engine::core::Error{std::string(option) + " " + std::string(value) + ": expected " +
                               std::string(expected)};
}

} // namespace

engine::core::Result<EditorOptions> parseEditorOptions(std::span<const std::string_view> arguments)
{
    EditorOptions options;
    for (std::size_t i = 0; i < arguments.size(); ++i) {
        const std::string_view argument = arguments[i];
        const bool takesValue =
            argument == "--frames" || argument == "--screenshot" || argument == "--size";
        if (!takesValue) {
            return engine::core::Error{"unknown argument '" + std::string(argument) + "'"};
        }
        if (i + 1 >= arguments.size()) {
            return engine::core::Error{std::string(argument) + " needs a value"};
        }
        const std::string_view value = arguments[++i];
        if (argument == "--frames") {
            const auto frames = parseNumber<std::uint64_t>(value);
            if (!frames || *frames == 0) {
                return invalid(argument, value, "a number of frames of at least 1");
            }
            options.maxFrames = frames;
        } else if (argument == "--screenshot") {
            if (value.empty()) {
                return invalid(argument, "''", "a file name");
            }
            options.screenshotPath = std::filesystem::path(value);
        } else {
            const std::size_t separator = value.find('x');
            const auto width = parseNumber<int>(value.substr(0, separator));
            const auto height = separator == std::string_view::npos
                                    ? std::optional<int>{}
                                    : parseNumber<int>(value.substr(separator + 1));
            const std::string range = "WIDTHxHEIGHT, each from 1 to " +
                                      std::to_string(engine::platform::kMaxWindowExtent);
            if (!width || !height || *width < 1 || *height < 1 ||
                *width > engine::platform::kMaxWindowExtent ||
                *height > engine::platform::kMaxWindowExtent) {
                return invalid(argument, value, range);
            }
            options.width = *width;
            options.height = *height;
        }
    }
    if (options.screenshotPath && !options.maxFrames) {
        options.maxFrames = kDefaultScreenshotFrames;
    }
    return options;
}

} // namespace editor
