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
            # sol2 is header-only and pulls a specific Lua (5.4.6 today) as a
            # transitive dependency; the welder::sol2 target consumes both. The
            # Lua version is bounded by what sol2's recipe accepts — override the
            # transitive `lua/*` here to move within that range if needed.
            self.requires("sol2/3.5.0")

    def layout(self):
        cmake_layout(self)
