import os

from conan import ConanFile
from conan.tools.cmake import CMakeDeps, CMakeToolchain


def build_platform(conanfile):
    """The first level of the build tree: build/<platform>/<build_type>.

    Platform is the *target* os and arch rather than the machine doing the compiling,
    so a MinGW cross build running on Linux lands in build/windows-x86_64/ -- the same
    tree a native Windows build would use, and never the one it is compiled from.
    """
    return f"{conanfile.settings.os}-{conanfile.settings.arch}".lower()


class Lc1Recipe(ConanFile):
    name = "lc1"
    version = "0.1.0"
    settings = "os", "compiler", "build_type", "arch"
    # No `generators = ...` attribute: generate() below instantiates the same two
    # generators by hand, because CMakeToolchain needs a per-platform presets_prefix
    # that the attribute form cannot express.

    options = {"with_validation_layers": [True, False]}
    # Project dependency defaults belong here; profiles describe the toolchain
    # and environment. Explicit profile/CLI options can still override these.
    default_options = {
        "with_validation_layers": True,
        # Name our static libraries explicitly: a propagated "*:shared" default
        # can override transitive dependencies' shared-library exceptions.
        "glfw/*:shared": False,
        "glm/*:shared": False,
        "ktx/*:shared": False,
        "mikktspace/*:shared": False,
        "spdlog/*:shared": False,
        "tinyobjloader/*:shared": False,
        "spdlog/*:use_std_fmt": True,
        # Only libktx is used, not the CLI tools.
        "ktx/*:tools": False,
        # GLFW dlopens these libraries, so they must remain shared.
        "wayland/*:shared": True,
        "xkbcommon/*:shared": True,
        # Native Wayland only: avoid X11's system development dependencies.
        # The dependency recipes remove inapplicable options on Windows.
        "glfw/*:with_wayland": True,
        "glfw/*:with_x11": False,
        "xkbcommon/*:with_x11": False,
        "vulkan-validationlayers/*:with_wsi_xcb": False,
        "vulkan-validationlayers/*:with_wsi_xlib": False,
        "reflect-cpp/*:with_yaml": True,
    }

    def requirements(self):
        self.requires("vulkan-headers/1.4.350.0")
        self.requires("glfw/3.4")
        self.requires("glm/1.0.3")
        self.requires("spdlog/1.17.0")
        self.requires("stb/cci.20240531")
        self.requires("tinyobjloader/2.0.0-rc10")
        self.requires("tinygltf/2.9.7")
        self.requires("ktx/4.4.2")
        self.requires("mikktspace/cci.20200325")
        self.requires("imgui/1.92.9b")
        self.requires("reflect-cpp/0.25.0")
        self.requires("freetype/2.14.3")
        self.requires("openal-soft/1.24.3")
        self.requires("drmp3/0.6.38")
        self.requires("drwav/0.13.14")

        # Validation layers are a debug-only dependency: nothing needs them in a
        # Release build, and building them from source is expensive.
        #
        # They come from conan rather than a system package because Windows has no
        # system package manager at all -- keeping one dependency story for both
        # platforms is worth the one-time build cost. The recipe exports
        # VK_LAYER_PATH via runenv_info, so Debug runs must source the generated
        # conanrun.sh (see AGENTS.md).
        #
        # profiles/mingw64 opts out to avoid an expensive cross build of the layers.
        # Wine can run the resulting application, but its prefix has no layers by
        # default; pass --no-validation there, or explicitly enable this option
        # and install the layers for the target environment. Native Debug builds
        # keep them by default.
        if self.settings.build_type == "Debug" and self.options.with_validation_layers:
            self.requires("vulkan-validationlayers/1.4.350.0")

    def layout(self):
        # Deliberately NOT cmake_layout(): the shape it produces depends on the
        # generator. It appends the build type only for single-config generators
        # (Ninja, Unix Makefiles) and omits it for multi-config ones (Visual Studio,
        # Xcode), so the tree would be build/<platform>/<build_type> with one and
        # build/<platform> with the other, the build type living only inside the CMake
        # cache. Pinning the folder here gives the requested shape for every generator.
        build_folder = os.path.join(
            "build", build_platform(self), str(self.settings.build_type)
        )
        self.folders.source = "."
        self.folders.build = build_folder
        self.folders.generators = os.path.join(build_folder, "generators")
        # Presets must reflect our per-build-type trees even with multi-config
        # generators. Otherwise both VS trees get the same "<platform>-default"
        # configure preset. Conan uses this for configure, build and test names.
        self.folders.build_folder_vars = ["settings.build_type"]
        self.cpp.source.includedirs = ["include"]
        self.cpp.build.libdirs = ["."]
        self.cpp.build.bindirs = ["."]

    def generate(self):
        CMakeDeps(self).generate()

        toolchain = CMakeToolchain(self)
        # One CMakeUserPresets.json at the repo root includes every platform's
        # generated CMakePresets.json at once, and CMake refuses to read a file whose
        # includes define the same preset name twice:
        #   CMake Error: Duplicate preset: "conan-debug"
        # The default prefix is "conan" for every install, so Debug under
        # build/linux-x86_64/ and Debug under build/windows-x86_64/ would collide the
        # moment both build trees exist. The platform prefix and build_folder_vars
        # from layout() give linux-x86_64-debug and windows-x86_64-debug with both
        # single-config and multi-config generators.
        toolchain.presets_prefix = build_platform(self)
        toolchain.generate()


# Deliberately NOT a dependency: vulkan-loader.
#
# The system loader is dlopened exactly once, by lc1::VulkanLoader
# (vk::raii::Context's no-argument constructor: libvulkan.so on Linux,
# vulkan-1.dll on Windows), which hands its vkGetInstanceProcAddr to GLFW via
# glfwInitVulkanLoader so that GLFW never loads one itself. A conan
# vulkan-loader could only be a *second* loader: Context{} dlopens by name no
# matter what is linked, and VK_NO_PROTOTYPES leaves nothing that would call
# into the linked copy. This holds on both platforms -- it is not a Linux
# workaround.
#
# This was volk's job until 2026-09-21. volk is no longer a dependency: after
# the vk::raii migration nothing called a bare vk* function, so volk's
# per-level function-pointer tables were never read, and its only remaining
# work was one dlopen plus one vkGetInstanceProcAddr lookup -- which
# vk::raii::Context already does. See the note at the top of include/lc1/vk/core/common.hpp.
