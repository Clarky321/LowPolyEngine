#pragma once

// The editor shell's Raven layout (editor/layouts/shell.xml), embedded at
// build time (editor/CMakeLists.txt, cmake/EmbedFiles.cmake): the editor reads
// no layout file at run time and gives Raven the text (ADR 0026, ADR 0027).

#include <string_view>

namespace editor::layouts {

[[nodiscard]] std::string_view shell() noexcept;

// The name the layout has in messages.
inline constexpr std::string_view kShellName = "editor/layouts/shell.xml";

} // namespace editor::layouts
