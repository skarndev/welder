import os

from conan import ConanFile
from conan.tools.cmake import CMake, cmake_layout
from conan.tools.build import can_run


class WelderTestConan(ConanFile):
    """Consumes the freshly created welder package the way a downstream project
    would: find_package(welder) + link welder::headers, then compile a tiny TU that
    exercises reflection. Building it at all proves the exported target carries
    -freflection and C++26 through to a consumer (no backend involved)."""

    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeToolchain", "CMakeDeps", "VirtualRunEnv"
    test_type = "explicit"

    def requirements(self):
        self.requires(self.tested_reference_str)

    def layout(self):
        cmake_layout(self)

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def test(self):
        if can_run(self):
            self.run(os.path.join(self.cpp.build.bindir, "welder_smoke"),
                     env="conanrun")