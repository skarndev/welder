from conan import ConanFile
from conan.tools.cmake import cmake_layout


class WelderConan(ConanFile):
    name = "welder"
    version = "0.1.0"
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeToolchain", "CMakeDeps"

    options = {
        "with_pybind11": [True, False],
        "with_nanobind": [True, False],
        "with_sol2": [True, False],
    }
    default_options = {
        "with_pybind11": True,
        "with_nanobind": True,
        "with_sol2": True,
    }

    def requirements(self):
        if self.options.with_pybind11:
            self.requires("pybind11/2.13.6")
        if self.options.with_nanobind:
            self.requires("nanobind/2.13.0")
        if self.options.with_sol2:
            # sol2 is the header-only C++ wrapper only. Lua itself is NOT taken
            # from conan: welder::sol2 builds against the *system/user* Lua found
            # by CMake's FindLua (steered by WELDER_LUA_DIR/WELDER_LUA_VERSION) so
            # the module's ABI matches the interpreter + luarocks that load/test
            # it. sol2 still pulls a transitive `lua` for its own recipe, but the
            # welder build does not consume it. See src/welder/backends/CMakeLists.txt.
            self.requires("sol2/3.5.0")

    def layout(self):
        cmake_layout(self)
