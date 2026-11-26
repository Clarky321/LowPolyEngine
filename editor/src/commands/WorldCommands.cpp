#include "editor/commands/WorldCommands.hpp"

namespace editor {

using engine::core::Error;

SetWorldSettings::SetWorldSettings(const engine::worldgen::WorldGenSettings& settings)
    : settings_(settings)
{
}

std::optional<Error> SetWorldSettings::apply(DocumentContent& content)
{
    if (auto error = engine::worldgen::validate(settings_)) {
        return Error{"world settings: " + error->message};
    }
    previous_ = content.worldSettings;
    content.worldSettings = settings_;
    return std::nullopt;
}

std::optional<Error> SetWorldSettings::revert(DocumentContent& content)
{
    content.worldSettings = previous_;
    return std::nullopt;
}

bool SetWorldSettings::absorb(const Command& next)
{
    const auto* set = dynamic_cast<const SetWorldSettings*>(&next);
    if (set == nullptr) {
        return false;
    }
    settings_ = set->settings_;
    return true;
}

} // namespace editor
