#pragma once

// Commands on the project's world settings (M5.5, ADR 0029). Regenerating
// the shown world from them is the WorldGen panel's (M5.10).

#include "editor/document/Command.hpp"

#include "engine/worldgen/WorldGenSettings.hpp"

#include <optional>
#include <string>

namespace editor {

// Replaces the world settings with settings that pass worldgen::validate(). A
// continuous edit (a dragged field) merges into one step.
class SetWorldSettings final : public Command {
public:
    explicit SetWorldSettings(const engine::worldgen::WorldGenSettings& settings);

    [[nodiscard]] std::string name() const override { return "Change world settings"; }
    [[nodiscard]] std::optional<engine::core::Error> apply(DocumentContent& content) override;
    [[nodiscard]] std::optional<engine::core::Error> revert(DocumentContent& content) override;
    [[nodiscard]] bool absorb(const Command& next) override;

private:
    engine::worldgen::WorldGenSettings settings_;
    engine::worldgen::WorldGenSettings previous_;
};

} // namespace editor
