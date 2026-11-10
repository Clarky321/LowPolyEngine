#pragma once

// OpenGL error reporting through the engine log. Internal to the OpenGL backend.

#include <cstdint>

namespace engine::render::opengl {

// Routes driver debug messages to stderr if the current context is a debug
// context. Returns false (and does nothing) otherwise.
bool enableDebugOutput();

// Turns the callback off again; call before the renderer goes away.
void disableDebugOutput();

// Drains glGetError, prints each error with `where`, returns how many were found.
std::uint32_t checkGlErrors(const char* where);

} // namespace engine::render::opengl
