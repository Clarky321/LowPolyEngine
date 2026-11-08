#include "engine/core/Version.hpp"

#if !defined(LPE_VERSION_MAJOR) || !defined(LPE_VERSION_MINOR) || !defined(LPE_VERSION_PATCH)
#error "LPE_VERSION_* must be defined by the build system (see engine/CMakeLists.txt)"
#endif

namespace engine::core {

Version engineVersion() noexcept
{
    return Version{LPE_VERSION_MAJOR, LPE_VERSION_MINOR, LPE_VERSION_PATCH};
}

std::string toString(const Version& version)
{
    return std::to_string(version.major) + "." + std::to_string(version.minor) + "." +
           std::to_string(version.patch);
}

} // namespace engine::core
