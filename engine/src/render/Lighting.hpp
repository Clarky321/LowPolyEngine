#pragma once

// GPU-free lighting math of the renderer (ADR 0011), unit-tested directly.

#include "engine/core/Math.hpp"

#include <glm/mat3x3.hpp>

namespace engine::render {

// The matrix that carries a mesh's normals into world space for a model
// matrix: the cofactor matrix of its upper 3x3 part, negated when that part
// mirrors (negative determinant). It is the inverse transpose scaled by the
// determinant, so after normalization it gives the same directions, and it
// stays defined when the model flattens an object (determinant 0): a flat
// face squashed along its normal keeps its normal. The result is not
// normalized; the fragment shader normalizes the interpolated normal.
[[nodiscard]] glm::mat3 normalMatrix(const math::Mat4& model);

} // namespace engine::render
