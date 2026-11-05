# 构建指南

[English](BUILD.md) | 简体中文

项目使用 C++23、CMake 和 Conan 2。Linux 是主要开发平台，另提供 Linux 上的
MinGW-w64 交叉编译和 Windows 原生 MSVC 配置。以下命令除特别说明外，均在仓库根目录执行。

## 工具与环境

| 工具 | 要求或仓库配置 |
|---|---|
| CMake | 3.30 或更新版本 |
| Conan | 2.x；CI 固定使用 2.33.0，支持 GCC 16 的设置值 |
| Python | 3.10 或更新版本，用于着色器接口生成和测试 |
| Ninja | 仓库中的 Conan profiles 均使用 Ninja |
| GNU Make | Linux 快捷构建入口；也可直接调用 Conan 和 CMake |
| C++ 编译器和标准库 | 支持项目使用的 C++23 功能，具体工具链见下表 |
| Slang | 完整安装；CI 使用 2026.18.3，`slangc` 需在 `PATH` 中或显式指定 |

| 场景 | Host profile | 编译器 |
|---|---|---|
| Linux 默认构建 | `profiles/linux` | Clang 22，libstdc++ |
| Linux GCC 构建 | `profiles/linux-gcc` | GCC 16 |
| Linux → Windows | `profiles/mingw64` | MinGW-w64 GCC 16，`x86_64-w64-mingw32-*` 工具 |
| Windows 原生构建 | 本机检测 profile + `profiles/windows` | 已安装且支持所需 C++23 功能的 MSVC，x64 开发环境 |

Profile 中声明的版本必须与实际工具链一致。Windows 原生构建检测本机编译器版本，
CI 单独通过 `profiles/windows-ci` 固定 MSVC 19.4x。Clang 配置使用系统 libstdc++，因此也需要
提供 C++23 支持的标准库开发文件。首次构建依赖还可能需要系统开发包及 Autoconf、Automake、
Libtool、pkg-config 等工具；具体安装列表可参考 [CI 配置](.github/workflows/ci.yml)。

编译和 CPU 测试不需要创建 Vulkan 设备。运行游戏或 GPU 测试需要系统 Vulkan loader、
驱动和满足设备选择要求的 GPU，包括 Vulkan 1.4、shader object、原生 descriptor heap
及 ray query 等功能；仅支持 Vulkan 1.4 并不保证具备全部所需扩展。Linux 默认使用 Wayland，
需要可用的 Wayland 桌面环境。

## Linux 快速开始

安装上述工具后，首次使用 Conan 时创建本机 build profile；已有 `default` profile 时可跳过：

```sh
conan profile detect
```

仓库不包含 `runtime/assets/` 内的外部资源。将所需资源放入该目录；只编译或运行 CPU 测试时，
可以创建空目录以满足资源复制步骤。空目录不代表具备完整游戏运行资源。

```sh
mkdir -p runtime/assets
make
```

`make` 默认使用 `PLATFORM=linux-x86_64`、`BUILD_TYPE=Debug`、`JOBS=8`，依次执行
依赖安装、CMake 配置和构建。首次运行会下载依赖，并编译缓存中缺失的包，需要联网且耗时较长。

```sh
make run                        # 构建并启动
make run-gdb                    # 构建并在 GDB 中启动，仅 Linux
make BUILD_TYPE=Release         # Release 构建
make run BUILD_TYPE=Release      # 运行 Release
make JOBS=4                     # 限制依赖和项目构建的并行度
```

Make 默认的 8 个并行任务用于控制本地内存占用，可按机器资源调整。

## 构建流程与输出

1. `make deps` 调用 `conan install`，使用目标平台的 host profile 和本机的 `default`
   build profile。Conan 安装依赖并生成 CMake toolchain、presets 和运行环境脚本。
2. `make configure` 先安装依赖，再执行 `cmake --preset <platform>-<build_type>`。
3. `make build`（也是 `make` 的默认目标）完成前两步，再调用 CMake build preset。
   CMake 编译 `lc1_engine`、`lc1_game` 和可执行程序 `lc1`，并调用 Slang 生成 SPIR-V。
4. 构建同时复制资源和对话文件，覆盖更新种子存档文件；运行时的 `checkpoint.yaml` 进度文件保留。

主要依赖由 [conanfile.py](conanfile.py) 管理，包括 Vulkan-Headers、GLFW、GLM、ImGui、
模型/纹理加载、字体、音频和 YAML 支持。VMA-Hpp 由 CMake FetchContent 按固定 URL 和
SHA256 下载，内含 VMA；Slang 和系统 Vulkan loader 由外部环境提供。

每种平台和构建类型使用独立目录，例如：

```text
build/linux-x86_64/Debug/
├── generators/          # Conan toolchain、presets、conanrun.sh 等
├── shaders/             # 生成的着色器接口和 .spv
├── runtime/
│   ├── assets/          # 从源码树复制的外部资源
│   ├── dialogs/         # 对话数据
│   └── saves/           # 种子存档与运行进度
├── compile_commands.json
└── lc1
```

根目录的 `CMakeUserPresets.json` 由 Conan 自动生成，未纳入版本控制。
Preset 使用 `linux-x86_64-debug` 等名称，不使用通用的 `conan-debug`。
Linux Clang 和 GCC 共享平台目录命名，因此切换编译器时应先移走原构建树，并重新安装依赖和配置；
同理，不要在 MSVC 和 MinGW 之间复用同一 CMake 缓存。

## 手动调用 Conan 和 CMake

与默认 Linux Debug 构建对应的分步命令为：

```sh
conan install . -pr:h profiles/linux -pr:b default --build=missing -s build_type=Debug -c tools.build:jobs=8
cmake --preset linux-x86_64-debug
cmake --build --preset linux-x86_64-debug -j 8
```

构建 Release 时，将 Conan 的 `build_type` 改为 `Release`，两条 CMake 命令的 preset
改为 `linux-x86_64-release`。Profile 的默认构建类型不完全相同，手动安装时应始终显式指定。

使用 Linux GCC 时将 host profile 替换为 `profiles/linux-gcc`，并使用独立、干净的构建树。
`-pr:h` 描述目标程序的环境，`-pr:b` 描述运行构建工具的环境；交叉编译时二者不同。

## 运行环境与数据

推荐使用 `make run`：它先构建，再进入对应构建目录；Linux 下还会加载
`generators/conanrun.sh`，提供依赖库路径、Debug 验证层的 `VK_LAYER_PATH` 和 XKB 环境。
手动运行已有构建时使用：

```sh
cd build/linux-x86_64/Debug
. ./generators/conanrun.sh
./lc1
```

着色器、对话和默认数据路径相对于工作目录解析，Release 同样需要从构建目录运行。
`LC1_ASSET_ROOT` 可覆盖资源根目录，`LC1_SAVE_ROOT` 可覆盖默认存档目录
`runtime/saves/01`。存档行为见[存档文档](docs/save-data.md)。

Debug 默认启用 Vulkan 验证和同步验证，Conan 也默认安装对应验证层。
`with_validation_layers=False` 只控制依赖安装，不会关闭程序的 Debug 验证逻辑。
在明确不使用验证的诊断场景，可通过 `LC1_NO_VALIDATION=1` 启动；该运行结果不能作为验证层检查通过的证据。

`LC1_PLAYER_ID` 选择本地玩家绑定，默认 `1`，与角色编号独立。绑定约定见[玩家与多人模拟](docs/players.md)。

## Windows 构建

### Linux 上交叉编译

安装 MinGW-w64 GCC 16，包括带 `x86_64-w64-mingw32-` 前缀的 C、C++ 和 `windres` 工具：

```sh
make build PLATFORM=windows-x86_64 BUILD_TYPE=Release
```

产物为 `build/windows-x86_64/Release/lc1.exe`。Slang 仍在 Linux 构建机上执行。
可安装 Wine 后运行：

```sh
make run PLATFORM=windows-x86_64 BUILD_TYPE=Release
```

Make 使用独立的 `build/wine-prefix`，不使用个人的 `~/.wine`。MinGW profile 默认不安装验证层；
在没有验证层的 Wine 环境中运行 Debug 时，需要显式选择：

```sh
LC1_NO_VALIDATION=1 make run PLATFORM=windows-x86_64
```

### Windows 原生 MSVC

在已安装的 Visual Studio 或 Build Tools 对应的 x64 开发者命令提示符中执行，
确保 Python、Conan、CMake、Ninja 和 Windows 版 Slang 在 `PATH` 中。
复用已有的 Conan `default` profile；仅在没有该 profile 时，先在此开发者命令提示符中
执行 `conan profile detect`。通过 `conan profile show -pr:a default` 核对结果。
检测到的编译器应为 MSVC，且必须支持项目使用的 C++23 功能；其他版本尚未由 Windows CI 验证。
切换工具链后，更新 profile 以匹配当前工具链，并先移走旧构建树再安装依赖。
资源仍需自行准备；以下空目录只满足构建步骤。

```bat
if not exist runtime\assets mkdir runtime\assets
conan install . -pr:a default -pr:a ./profiles/windows --build=missing -s:h build_type=Debug -s:b build_type=Release
cmake --preset windows-x86_64-debug
cmake --build --preset windows-x86_64-debug
cd build\windows-x86_64\Debug
call generators\conanrun.bat
lc1.exe
```

## 测试与可选配置

`BUILD_TESTING` 默认开启，普通构建会生成 CPU 测试程序。Linux 下构建后执行：

```sh
. ./build/linux-x86_64/Debug/generators/conanrun.sh
ctest --test-dir build/linux-x86_64/Debug --output-on-failure --no-tests=error
```

CTest 覆盖世界数据、输入、碰撞、角色、视角、存档等 CPU 测试及着色器接口检查，
不创建窗口或 Vulkan 设备。GPU 测试需要单独构建并执行，例如：

```sh
cmake --build --preset linux-x86_64-debug --target lc1_pbr_tests -j 8
cd build/linux-x86_64/Debug
. ./generators/conanrun.sh
./lc1_pbr_tests
```

其他显式 GPU 目标包括 `lc1_temporal_shadow_tests`、`lc1_shadow_path_tests`、
`lc1_descriptor_heap_tests` 和 `lc1_descriptor_heap_bulk_tests`。
描述符堆测试步骤见[专门文档](docs/descriptor-heap-test.md)。GPU 检查需要满足运行条件，
并确认验证层没有错误；编译成功和 CPU 测试通过不能替代 GPU 执行。

需要关闭测试构建或启用 AddressSanitizer 时，可在依赖安装后配置：

```sh
cmake --preset linux-x86_64-debug -DBUILD_TESTING=OFF
# 或在编译器和运行库支持时启用 ASan，默认关闭：
cmake --preset linux-x86_64-debug -Dlc1_enable_asan=ON
```

这些 CMake 选项会保存在当前构建树的缓存中；需要恢复时显式设置为相反值。

## 常见问题

- **Release 提示找不到 `spirv-opt` 或 `slang-glslang`**：Slang 安装不完整。
  部分 Fedora `shader-slang` 包缺少下游编译器组件，Debug 的 `-O0` 可能掩盖问题。
  使用完整发行包，并通过 `make SLANGC=/path/to/slangc BUILD_TYPE=Release` 指定；
  手动配置对应 `-DSLANGC_EXECUTABLE=/path/to/slangc`，路径会缓存到该构建树。
- **Debug 提示找不到验证层**：先确认从构建目录加载了 Conan 运行环境，再检查是否安装了验证层依赖。
- **缺少资源或复制目录失败**：检查 `runtime/assets/` 是否存在且内容齐全。
  `LC1_ASSET_ROOT` 影响运行时加载路径，构建时仍会复制源码树中的该目录。
- **CMake 报 preset 重复或引用失效**：检查生成的 `CMakeUserPresets.json`。
  修改 preset 生成规则后，应为其包含的每个平台和构建类型重新执行 Conan 安装。
- **需要清理**：`make clean` 删除整个 `build/`，包括所有平台产物、Wine prefix
  和构建树内的存档进度；清理前备份需要保留的存档。Conan 包缓存不受此命令影响。

CI 的平台覆盖、依赖安装策略和执行范围见 [CI 文档](docs/ci.md)，具体命令以
[工作流](.github/workflows/ci.yml)为准。CI 构建与 CPU 测试不包含游戏启动和 GPU 渲染验证。
