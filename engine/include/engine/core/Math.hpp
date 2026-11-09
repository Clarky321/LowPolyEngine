#pragma once

// Engine math types. GLM provides the implementation (ADR 0007); engine APIs
// name these aliases so the library stays replaceable in one place.
//
// Conventions: right-handed coordinates, +Y up, angles in radians, column
// vectors (p' = M * p), OpenGL clip space with depth in [-1, 1].

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace engine::math {

using Vec2 = glm::vec2;
using Vec3 = glm::vec3;
using Vec4 = glm::vec4;
using Mat4 = glm::mat4;

} // namespace engine::math
