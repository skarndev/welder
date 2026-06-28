from conan import ConanFile
from conan.tools.cmake import cmake_layout


class WelderConan(ConanFile):
    name = "welder"
    version = "0.1.0"
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeToolchain", "CMakeDeps"

    options = {
        "with_python": [True, False],
    }
    default_options = {
        "with_python": True,
    }

    def requirements(self):
        if self.options.with_python:
            self.requires("pybind11/2.13.6")

    def layout(self):
        cmake_layout(self)
