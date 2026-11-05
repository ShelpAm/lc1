# Build guide

English | [简体中文](BUILD.zh-cn.md)

The project uses C++23, CMake, and Conan 2. Linux is the primary development platform;
the repository also provides MinGW-w64 cross-compilation from Linux and native MSVC
builds on Windows. Run commands from the repository root unless stated otherwise.

## Prerequisites

| Tool | Requirement or repository configuration |
|---|---|
| CMake | 3.30 or newer |
| Conan | 2.x; CI pins 2.33.0, whose settings support GCC 16 |
| Python | 3.10 or newer, for shader interface generation and tests |
| Ninja | Used by all repository Conan profiles |
| GNU Make | Linux build shortcut; Conan and CMake can also be invoked directly |
| C++ compiler and standard library | Must support the C++23 features used by the project; see below |
| Slang | Complete installation; CI uses 2026.18.3. Put `slangc` on `PATH` or specify its path |

| Build | Host profile | Compiler |
|---|---|---|
| Default Linux build | `profiles/linux` | Clang 22 with libstdc++ |
| Linux GCC build | `profiles/linux-gcc` | GCC 16 |
| Linux → Windows | `profiles/mingw64` | MinGW-w64 GCC 16 with `x86_64-w64-mingw32-*` tools |
| Native Windows build | Local detected profile + `profiles/windows` | Installed MSVC with C++23 support, x64 developer environment |

The installed toolchain must match the version declared in its profile. Native
Windows builds detect the local compiler version; CI pins MSVC 19.4x separately
in `profiles/windows-ci`. The Clang
profile uses the system libstdc++, so its development files must also provide the
required C++23 support. Building dependencies from source may require system
development packages, Autoconf, Automake, Libtool, and pkg-config. See the
[CI workflow](.github/workflows/ci.yml) for its package installation list.

Compilation and CPU tests do not create a Vulkan device. Running the game or GPU
tests requires the system Vulkan loader, a driver, and a GPU meeting the device
selection requirements, including Vulkan 1.4, shader objects, native descriptor
heaps, and ray queries. Vulkan 1.4 support alone does not guarantee all required
extensions. Linux defaults to Wayland and requires a working Wayland desktop.

## Linux quick start

After installing the tools, create a local Conan build profile if you do not already
have a `default` profile:

```sh
conan profile detect
```

External resources under `runtime/assets/` are not included in the repository.
Place the required assets there. For compilation or CPU tests, an empty directory
satisfies the asset copy step; it does not supply the assets needed to run the game.

```sh
mkdir -p runtime/assets
make
```

`make` defaults to `PLATFORM=linux-x86_64`, `BUILD_TYPE=Debug`, and `JOBS=8`.
It installs dependencies, configures CMake, and builds the project. The first run
requires network access and can take a while because it downloads dependencies and
builds packages missing from the cache.

```sh
make run                        # Build and run
make run-gdb                    # Build and run in GDB; Linux only
make BUILD_TYPE=Release          # Build Release
make run BUILD_TYPE=Release      # Run Release
make JOBS=4                     # Limit dependency and project build parallelism
```

Make defaults to eight parallel jobs to control local memory use. Adjust this to
suit your machine.

## Build process and output

1. `make deps` runs `conan install` with the target platform's host profile and the
   local `default` build profile. Conan installs dependencies and generates the
   CMake toolchain, presets, and runtime environment scripts.
2. `make configure` installs dependencies, then runs
   `cmake --preset <platform>-<build_type>`.
3. `make build`, also the default `make` target, completes those steps and invokes
   the CMake build preset. CMake compiles `lc1_engine`, `lc1_game`, and the `lc1`
   executable, and invokes Slang to generate SPIR-V.
4. The build also copies assets and dialogue files and refreshes seed save
   files, overwriting existing copies. Runtime checkpoints are preserved.

[conanfile.py](conanfile.py) manages the main dependencies, including Vulkan-Headers,
GLFW, GLM, ImGui, model/texture loading, fonts, audio, and YAML support. CMake
FetchContent downloads VMA-Hpp using a pinned URL and SHA256; it includes VMA.
Slang and the system Vulkan loader are supplied by the external environment.

Each target platform and build type has a separate directory, for example:

```text
build/linux-x86_64/Debug/
├── generators/          # Conan toolchain, presets, conanrun.sh, etc.
├── shaders/             # Generated shader interfaces and .spv files
├── runtime/
│   ├── assets/          # External assets copied from the source tree
│   ├── dialogs/         # Dialogue data
│   └── saves/           # Seed saves and saved progress
├── compile_commands.json
└── lc1
```

Conan generates `CMakeUserPresets.json` at the repository root; it is not tracked
in version control. Presets have names such as `linux-x86_64-debug`, rather than
the generic `conan-debug`. Linux Clang and GCC share the same platform directory
name: move the previous build tree aside before switching compilers, then reinstall
dependencies and configure again. Likewise, do not reuse a CMake cache between
MSVC and MinGW.

## Invoking Conan and CMake directly

The default Linux Debug build can be performed in separate steps:

```sh
conan install . -pr:h profiles/linux -pr:b default --build=missing -s build_type=Debug -c tools.build:jobs=8
cmake --preset linux-x86_64-debug
cmake --build --preset linux-x86_64-debug -j 8
```

For Release, change Conan's `build_type` to `Release` and use
`linux-x86_64-release` in both CMake commands. Profile defaults differ, so always
specify the build type explicitly when installing dependencies manually.

For Linux GCC, replace the host profile with `profiles/linux-gcc` and use a separate,
clean build tree. `-pr:h` describes the target program's environment; `-pr:b`
describes the environment where build tools run. These differ during cross-compilation.

## Runtime environment and data

Use `make run` to build and enter the appropriate build directory. On Linux, it
also loads `generators/conanrun.sh`, which supplies dependency library paths,
`VK_LAYER_PATH` for Debug validation layers, and the XKB environment.
To run an existing build manually:

```sh
cd build/linux-x86_64/Debug
. ./generators/conanrun.sh
./lc1
```

Shader, dialogue, and default data paths are relative to the working directory.
Release builds must also run from the build directory. `LC1_ASSET_ROOT` overrides
the asset root; `LC1_SAVE_ROOT` overrides the default save directory,
`runtime/saves/01`. See the [save documentation](docs/save-data.md) (Chinese) for details.

Debug enables Vulkan validation and synchronization validation by default, and
Conan installs the corresponding layers by default. `with_validation_layers=False`
only controls dependency installation; it does not disable the application's Debug
validation behavior. For diagnostics explicitly performed without validation, set
`LC1_NO_VALIDATION=1`. Such a run is not evidence of passing validation checks.

`LC1_PLAYER_ID` selects the local player binding (default `1`), independently of character
keyids. See [player identities and local multiplayer](docs/players.md) for the binding contract.

## Windows builds

### Cross-compiling on Linux

Install MinGW-w64 GCC 16, including the C, C++, and `windres` tools with the
`x86_64-w64-mingw32-` prefix:

```sh
make build PLATFORM=windows-x86_64 BUILD_TYPE=Release
```

The executable is `build/windows-x86_64/Release/lc1.exe`. Slang still runs on the
Linux build machine. With Wine installed, run it using:

```sh
make run PLATFORM=windows-x86_64 BUILD_TYPE=Release
```

Make uses a separate `build/wine-prefix` instead of your personal `~/.wine`.
The MinGW profile does not install validation layers by default. To run Debug in
a Wine environment without those layers, explicitly opt out:

```sh
LC1_NO_VALIDATION=1 make run PLATFORM=windows-x86_64
```

### Native Windows with MSVC

Run these commands in the x64 developer command prompt for your installed Visual
Studio or Build Tools, with Python, Conan, CMake, Ninja, and Windows Slang on `PATH`.
Reuse your existing `default` Conan profile. If it does not exist, run
`conan profile detect` in this developer prompt first. Review it with
`conan profile show -pr:a default`.
The detected compiler must be MSVC and provide the C++23 features used by the project;
other versions have not been validated by the Windows CI configuration. If you change
toolchains, update your profile to match the active toolchain and move the old
build tree aside before reinstalling dependencies.
Supply assets separately; the empty directory below only satisfies the build step.

```bat
if not exist runtime\assets mkdir runtime\assets
conan install . -pr:a default -pr:a ./profiles/windows --build=missing -s:h build_type=Debug -s:b build_type=Release
cmake --preset windows-x86_64-debug
cmake --build --preset windows-x86_64-debug
cd build\windows-x86_64\Debug
call generators\conanrun.bat
lc1.exe
```

## Tests and optional configuration

`BUILD_TESTING` is enabled by default, so ordinary builds include CPU test
executables. After building on Linux, run:

```sh
. ./build/linux-x86_64/Debug/generators/conanrun.sh
ctest --test-dir build/linux-x86_64/Debug --output-on-failure --no-tests=error
```

CTest covers world data, input, collision, characters, view controls, saves, and
other CPU tests, along with shader interface checks. It does not create a window
or Vulkan device. Build and run GPU tests separately, for example:

```sh
cmake --build --preset linux-x86_64-debug --target lc1_pbr_tests -j 8
cd build/linux-x86_64/Debug
. ./generators/conanrun.sh
./lc1_pbr_tests
```

Other explicit GPU targets are `lc1_temporal_shadow_tests`, `lc1_shadow_path_tests`,
`lc1_descriptor_heap_tests`, and `lc1_descriptor_heap_bulk_tests`. See the
[descriptor heap test guide](docs/descriptor-heap-test.md) (Chinese) for its procedures.
GPU checks require a suitable runtime environment and clean validation output.
Successful compilation and CPU tests do not replace GPU execution.

After installing dependencies, configure CMake to disable test builds or enable
AddressSanitizer:

```sh
cmake --preset linux-x86_64-debug -DBUILD_TESTING=OFF
# Alternatively, enable ASan if supported by the compiler and runtime; default is OFF:
cmake --preset linux-x86_64-debug -Dlc1_enable_asan=ON
```

These options persist in the build tree's CMake cache. Set the opposite value
explicitly to restore the previous behavior.

## Troubleshooting

- **Release cannot find `spirv-opt` or `slang-glslang`:** the Slang installation is
  incomplete. Some Fedora `shader-slang` packages omit downstream compiler
  components; Debug's `-O0` can hide this issue. Use a complete distribution and
  select it with `make SLANGC=/path/to/slangc BUILD_TYPE=Release`, or configure
  manually with `-DSLANGC_EXECUTABLE=/path/to/slangc`. The path is cached per build tree.
- **Debug cannot find validation layers:** confirm that the Conan runtime
  environment was loaded from the build directory, then check that the layer
  dependency was installed.
- **Missing assets or a failed directory copy:** check that `runtime/assets/`
  exists and contains the required files. `LC1_ASSET_ROOT` affects runtime loading;
  the build still copies that directory from the source tree.
- **Duplicate presets or missing preset includes:** inspect the generated
  `CMakeUserPresets.json`. After changing preset generation, rerun Conan installation
  for every included platform and build type.
- **Cleaning:** `make clean` deletes all of `build/`, including every platform's
  output, the Wine prefix, and saved progress in build trees. Back up saves you want
  to keep before cleaning. The Conan package cache is unaffected.

See the [CI documentation](docs/ci.md) (Chinese) for platform coverage, dependency
installation policies, and validation scope; consult the
[workflow](.github/workflows/ci.yml) for the actual commands. CI builds and CPU tests
do not include launching the game or validating GPU rendering.

## Dependency maintenance constraints

- Keep `conanfile.py::layout()` platform/build-type isolation, `presets_prefix` and
  `build_folder_vars`. Regenerate every included build type after preset changes.
- Dependency defaults belong in `conanfile.py::default_options`; profiles describe
  toolchains and environment. Do not inherit unrelated default host options or use
  `*:shared=False`: Wayland and XKB must remain shared because GLFW dlopens them.
- Linux is Wayland-only by default: GLFW `with_wayland=True`, GLFW/XKB `with_x11=False`,
  validation layers `with_wsi_xcb/xlib=False`. X11 requires enabling all four X11 options
  and installing system development packages.
- VMA-Hpp bundles `vk_mem_alloc.h`; do not add a second Conan VMA dependency.
- Preserve MinGW's prefixed C/C++/`rc` tools and CMake link fixes: `stdc++exp` for
  `<print>`, executable `-static`, and KTX 4.4.2's missing `astcenc-avx2-static`
  companion archive from the same package. Link flags belong in CMake; profile
  `*_FLAGS_INIT` changes do not update an existing cache. TinyGLTF filenames use UTF-8.
