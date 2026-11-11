#include "DebugOverlay.hpp"

#include <glm/trigonometric.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <string>

namespace island_preview {

namespace {

using engine::debug::measureText;
using engine::debug::TextStyle;
using engine::math::Vec2;
using engine::math::Vec4;

constexpr float kTitleScale = 6.0F;
constexpr float kTextScale = 2.0F;
constexpr float kMargin = 16.0F;
constexpr float kPadding = 12.0F;

const Vec4 kTextColor{0.98F, 0.97F, 0.92F, 1.0F};
const Vec4 kAccentColor{1.0F, 0.84F, 0.42F, 1.0F};
const Vec4 kPanelColor{0.05F, 0.07F, 0.10F, 0.55F};

} // namespace

void buildOverlay(engine::debug::TextMeshBuilder& builder, const OverlayData& data)
{
    builder.clear();
    const auto width = static_cast<float>(data.framebuffer.width);
    const auto height = static_cast<float>(data.framebuffer.height);

    // Title, centered at the top.
    const std::string title = "LOW POLY ENGINE";
    const Vec2 titleSize = measureText(title, kTitleScale);
    builder.addText(title, {std::max(0.0F, (width - titleSize.x) * 0.5F), 28.0F},
                    TextStyle{.scale = kTitleScale, .color = kTextColor});
    const std::string subtitle = "M2  RENDERER / MATERIALS";
    const Vec2 subtitleSize = measureText(subtitle, kTextScale);
    builder.addText(subtitle, {std::max(0.0F, (width - subtitleSize.x) * 0.5F), 84.0F},
                    TextStyle{.scale = kTextScale, .color = kAccentColor});

    // Diagnostics panel in the bottom-left corner.
    const auto& camera = data.camera;
    const std::array<std::string, 9> lines{
        std::format("FPS: {:.0f}   FRAME: {:.2f} MS   MAX: {:.2f} MS", data.frame.framesPerSecond,
                    data.frame.averageFrameMs, data.frame.maxFrameMs),
        std::format("RENDERER: {}", data.renderer.api),
        std::format("GL: {}", data.renderer.driverVersion),
        std::format("GPU: {}", data.renderer.device),
        std::format("WINDOW: {} X {}", data.framebuffer.width, data.framebuffer.height),
        std::format("CAMERA: X {:.1f}  Y {:.1f}  Z {:.1f}  YAW {:.0f}  PITCH {:.0f}",
                    camera.position.x, camera.position.y, camera.position.z,
                    glm::degrees(camera.yawRadians), glm::degrees(camera.pitchRadians)),
        std::format("SPEED: {:.1f} M/S   DRAWS: {}   TRIANGLES: {}", data.moveSpeed,
                    data.renderStats.drawCalls, data.renderStats.triangles),
        "HOLD RMB: LOOK   WASD: MOVE   E/Q: UP/DOWN   SHIFT: FAST   WHEEL: SPEED",
        "F1: OVERLAY   F12: SCREENSHOT   ESC: QUIT",
    };

    float textWidth = 0.0F;
    for (const auto& line : lines) {
        textWidth = std::max(textWidth, measureText(line, kTextScale).x);
    }
    const float lineHeight = static_cast<float>(engine::debug::kLineAdvance) * kTextScale;
    const float textHeight = static_cast<float>(lines.size() - 1) * lineHeight +
                             static_cast<float>(engine::debug::kGlyphHeight) * kTextScale;
    const Vec2 panelSize{textWidth + 2.0F * kPadding, textHeight + 2.0F * kPadding};
    const Vec2 panelTopLeft{kMargin, height - kMargin - panelSize.y};
    builder.addRect(panelTopLeft, panelSize, kPanelColor);

    Vec2 pen = panelTopLeft + Vec2{kPadding};
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const Vec4& color = i == 0 ? kAccentColor : kTextColor;
        builder.addText(lines[i], pen, TextStyle{.scale = kTextScale, .color = color});
        pen.y += lineHeight;
    }
}

} // namespace island_preview
