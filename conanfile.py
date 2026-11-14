from conan import ConanFile
from conan.tools.cmake import CMakeDeps, CMakeToolchain

# SDL3 subsystems the engine does not use yet. They stay off until a milestone
# needs them: fewer build dependencies, smaller binaries. "gpu" (SDL_GPU) and
# "render" (SDL_Renderer) are off by design, see ADR 0002.
_SDL_UNUSED_SUBSYSTEMS = (
    "audio",
    "camera",
    "dialog",
    "gpu",
    "haptic",
    "hidapi",
    "joystick",  # enable together with gamepad input (not part of M1)
    "power",
    "render",
    "sensor",
    "tray",
)


class LowPolyEngineConan(ConanFile):
    """Dependency graph for LowPolyEngine.

    Conan only installs packages and generates CMake integration files.
    Targets, options and tests live in CMakeLists.txt (see docs/TECH_STACK.md).
    Add a dependency only when the current milestone needs it.
    """

    name = "lowpolyengine"
    settings = "os", "arch", "compiler", "build_type"

    def requirements(self):
        # Window, events and the OpenGL context (ADR 0002).
        self.requires("sdl/3.4.16")
        # Vector/matrix math behind engine::math (header-only, ADR 0007).
        self.requires("glm/1.0.3")
        # PNG encoding for frame captures (stb_image_write; header-only, ADR 0006).
        self.requires("stb/cci.20261021")
        # World generation settings files (JSON; header-only, ADR 0004).
        self.requires("nlohmann_json/3.12.0")
        # Development and debug UI in the sandbox (ADR 0018); its SDL3 and
        # OpenGL 3 backends are built from the package's sources in third_party/.
        self.requires("imgui/1.92.9b")
        # Unit tests only; never linked into the engine runtime.
        self.test_requires("catch2/3.16.1")

    def configure(self):
        sdl = self.options["sdl/*"]
        sdl.shared = False
        for subsystem in _SDL_UNUSED_SUBSYSTEMS:
            setattr(sdl, subsystem, False)
        # Only desktop OpenGL is used; Vulkan support is added with that backend.
        sdl.opengles = False
        sdl.vulkan = False
        if self.settings.os == "Linux":
            # X11 only for now (Wayland desktops run it through XWayland).
            # Wayland and D-Bus would build several large libraries from source.
            sdl.wayland = False
            sdl.dbus = False
            sdl.libudev = False

    def generate(self):
        toolchain = CMakeToolchain(self)
        # The repository's CMakePresets.json is the single source of presets,
        # so do not let Conan write a CMakeUserPresets.json into the source tree.
        toolchain.user_presets_path = False
        toolchain.generate()
        CMakeDeps(self).generate()
