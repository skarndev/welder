import os
import re

from conan import ConanFile
from conan.tools.cmake import CMake, cmake_layout
from conan.tools.files import copy, load


class WelderConan(ConanFile):
    """welder's Conan package recipe.

    welder ships **header-only**, so this produces a single settings-independent
    package (see ``package_id``): the header tree plus the exported CMake package
    (``welder::headers`` / ``welder::flags`` and the build helpers).

    The package deliberately carries **no backend dependencies** — a consumer
    brings their own pybind11 / nanobind / sol2 / LuaBridge3 and wires it, matching
    the header-only, bring-your-own-backend delivery model. The backend headers are
    declared as ``test_requires`` (which never propagate to a consumer): they are
    pulled only when developing welder itself — building its own examples/tests — so
    ``conan install .`` still provisions them, gated per backend by the
    ``with_pybind11`` / ``with_nanobind`` / ``with_sol2`` options (e.g. Windows CI
    passes ``-o with_pybind11=False -o with_sol2=False``).
    """

    name = "welder"
    license = "MIT"
    author = "Sergey Shumakov"
    url = "https://github.com/skarndev/welder"
    homepage = "https://skarndev.github.io/welder/"
    description = (
        "Generate language bindings for annotated C++ types from C++26 reflection "
        "(P2996) + annotations (P3394), at compile time — header-only."
    )
    topics = ("bindings", "reflection", "cpp26", "p2996", "p3394",
              "pybind11", "nanobind", "sol2", "lua", "header-only")

    package_type = "header-library"
    settings = "os", "compiler", "build_type", "arch"

    # Per-backend switches for the dev-time test_requires below. Default ON for a
    # convenient full dev build; a consumer is unaffected either way (test_requires
    # never propagate). CI flips individual ones off where a backend is unwanted
    # (e.g. Windows/mingw drops pybind11 + sol2).
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

    # Everything needed to configure + install the header-only package. Examples,
    # tests and docs are dev-time only and intentionally left out.
    exports_sources = "CMakeLists.txt", "src/*", "cmake/*", "LICENSE"

    generators = "CMakeToolchain", "CMakeDeps"

    def set_version(self):
        # The version's single source of truth is src/welder/version.hpp (the
        # top-level CMakeLists.txt parses it the same way) — bump it there only.
        header = load(self, os.path.join(self.recipe_folder,
                                         "src", "welder", "version.hpp"))
        parts = {
            part: re.search(rf"#define WELDER_VERSION_{part} (\d+)", header)[1]
            for part in ("MAJOR", "MINOR", "PATCH")
        }
        self.version = "{MAJOR}.{MINOR}.{PATCH}".format(**parts)

    def build_requirements(self):
        # Backends are a development convenience only (see the class docstring), so
        # they are test_requires: available when building welder's own
        # examples/tests, never propagated to a consumer of the header-only package.
        if self.options.with_pybind11:
            self.test_requires("pybind11/3.0.1")
        if self.options.with_nanobind:
            self.test_requires("nanobind/2.13.0")
        if self.options.with_sol2:
            self.test_requires("sol2/3.5.0")

    def layout(self):
        cmake_layout(self)

    def package_id(self):
        # Header-only: one package for every setting/option combination.
        self.info.clear()

    def build(self):
        # Nothing to compile. Configure with every backend/example/test OFF so the
        # install step has a build tree and any CMake error surfaces early.
        cmake = CMake(self)
        cmake.configure(variables={
            "WELDER_BUILD_PYBIND11": "OFF",
            "WELDER_BUILD_NANOBIND": "OFF",
            "WELDER_BUILD_SOL2": "OFF",
            "WELDER_BUILD_LUABRIDGE": "OFF",
            "WELDER_BUILD_EXAMPLES": "OFF",
            "WELDER_BUILD_TESTS": "OFF",
            "WELDER_BUILD_STUBS": "OFF",
        })

    def package(self):
        copy(self, "LICENSE", src=self.source_folder,
             dst=os.path.join(self.package_folder, "licenses"))
        cmake = CMake(self)
        cmake.install()

    def package_info(self):
        self.cpp_info.bindirs = []
        self.cpp_info.libdirs = []
        # welder ships a complete CMake package config (welder::headers +
        # welder::flags with -freflection, plus the build helpers). Defer to it
        # rather than have CMakeDeps synthesize a thinner one: 'none' stops CMakeDeps
        # generating a competing module, and the toolchain still puts this package on
        # CMAKE_PREFIX_PATH so find_package(welder) resolves the installed config.
        self.cpp_info.set_property("cmake_find_mode", "none")
        self.cpp_info.builddirs = [os.path.join("lib", "cmake", "welder")]