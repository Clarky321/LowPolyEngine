#include "engine/world/WorldView.hpp"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace engine::world {

render::LightDesc archipelagoSunlight()
{
    return render::LightDesc{.direction = {-0.45F, -0.78F, -0.43F},
                             .color = math::Vec3{0.5F, 0.465F, 0.41F},
                             .ambient = math::Vec3{0.5F, 0.53F, 0.62F}};
}

camera::Camera cameraLookingAt(const math::Vec3& position, const math::Vec3& target)
{
    const math::Vec3 direction = glm::normalize(target - position);
    camera::Camera camera;
    camera.position = position;
    // forwardDirection() is (-sin(yaw) cos(pitch), sin(pitch), -cos(yaw) cos(pitch)).
    camera.yawRadians = std::atan2(-direction.x, -direction.z);
    camera.pitchRadians = std::asin(std::clamp(direction.y, -1.0F, 1.0F));
    return camera;
}

camera::Camera overviewCamera(const worldgen::Archipelago& archipelago)
{
    math::Vec3 centre{0.0F};
    for (const auto& island : archipelago.islands) {
        centre += island.shape.island.anchor;
    }
    centre /= static_cast<float>(std::max<std::size_t>(archipelago.islands.size(), 1U));
    float reach = 0.0F;
    for (const auto& island : archipelago.islands) {
        const math::Vec3 offset = island.shape.island.anchor - centre;
        reach = std::max(reach, std::sqrt(offset.x * offset.x + offset.z * offset.z) +
                                    island.shape.island.radius);
    }
    const math::Vec3 direction = glm::normalize(math::Vec3{0.55F, 0.45F, 0.72F});
    const math::Vec3 target = centre - math::Vec3{0.0F, 0.25F * reach, 0.0F};
    return cameraLookingAt(centre + direction * (1.3F * reach + 60.0F), target);
}

} // namespace engine::world
