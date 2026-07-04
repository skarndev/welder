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
    }
    default_options = {
        "with_pybind11": True,
        "with_nanobind": True,
    }

    def requirements(self):
        if self.options.with_pybind11:
            self.requires("pybind11/2.13.6")
        if self.options.with_nanobind:
            self.requires("nanobind/2.13.0")

    def layout(self):
        cmake_layout(self)
