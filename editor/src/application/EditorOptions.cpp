#include "EditorOptions.hpp"

#include "engine/platform/Window.hpp"

#include <charconv>
#include <string>
#include <system_error>

namespace editor {

namespace {

// The whole text as a number. A plain value and a flag rather than
// std::optional: GCC 13 at -O3 reports a false "may be used uninitialized" for
// an optional<int> here.
template <typename T>
bool parseNumber(std::string_view text, T& value)
{
    value = T{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return !text.empty() && error == std::errc{} && end == text.data() + text.size();
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
            std::uint64_t frames = 0;
            if (!parseNumber(value, frames) || frames == 0) {
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
            int width = 0;
            int height = 0;
            const bool parsed = separator != std::string_view::npos &&
                                parseNumber(value.substr(0, separator), width) &&
                                parseNumber(value.substr(separator + 1), height);
            constexpr int kMax = engine::platform::kMaxWindowExtent;
            if (!parsed || width < 1 || height < 1 || width > kMax || height > kMax) {
                return invalid(argument, value,
                               "WIDTHxHEIGHT, each from 1 to " + std::to_string(kMax));
            }
            options.width = width;
            options.height = height;
        }
    }
    if (options.screenshotPath && !options.maxFrames) {
        options.maxFrames = kDefaultScreenshotFrames;
    }
    return options;
}

} // namespace editor
