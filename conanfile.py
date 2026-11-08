from conan import ConanFile
from conan.tools.cmake import CMakeDeps, CMakeToolchain


class LowPolyEngineConan(ConanFile):
    """Dependency graph for LowPolyEngine.

    Conan only installs packages and generates CMake integration files.
    Targets, options and tests live in CMakeLists.txt (see docs/TECH_STACK.md).
    Add a dependency only when the current milestone needs it.
    """

    name = "lowpolyengine"
    settings = "os", "arch", "compiler", "build_type"

    def requirements(self):
        # Unit tests only; never linked into the engine runtime.
        self.test_requires("catch2/3.16.1")

    def generate(self):
        toolchain = CMakeToolchain(self)
        # The repository's CMakePresets.json is the single source of presets,
        # so do not let Conan write a CMakeUserPresets.json into the source tree.
        toolchain.user_presets_path = False
        toolchain.generate()
        CMakeDeps(self).generate()
