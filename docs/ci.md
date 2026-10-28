# 持续集成

## 分支与 PR

采用 GitHub Flow，只有 `main` 是长期分支。每项工作从最新 `origin/main` 创建短期分支，
通过 PR 验证后 squash merge 到 `main`，合入后删除工作分支。下一项工作重新从最新 `main`
创建分支，不继续使用上一次 squash 前的分支历史；`dev` 不作为持续集成分支。

## 构建检查

`.github/workflows/ci.yml` 在 push、pull request 和手动触发时执行四组独立的构建任务：

| 平台 | 环境 | 配置 |
|---|---|---|
| Linux x86_64 | Fedora 44 容器，Clang 22，`profiles/linux` | Debug |
| Linux x86_64 | Fedora 44 容器，GCC 16，`profiles/linux-gcc` | Debug |
| Windows x86_64 | Windows Server 2022，MSVC 19.4x，`profiles/windows-ci` | Debug |
| Windows x86_64 | Fedora 44 容器，MinGW-w64 GCC 16，`profiles/mingw64` | Debug |

所有任务都通过 Conan 安装依赖，构建完整程序与 Slang 着色器。
Linux Clang、GCC 和 Windows MSVC 设置 `BUILD_TESTING=ON`，构建后运行现有五组
CPU 单元测试：`world_map`、`continent`、`character`、`tangent_space`、`player_view`。
MinGW 交叉编译设置 `BUILD_TESTING=OFF`，仅检查程序和着色器构建。
所有 CI 构建通过 `-o:h "lc1/*:with_validation_layers=False"` 关闭 VVL 依赖，
避免编译当前任务用不到的验证层。本地 Debug 默认仍然包含 VVL；未来 GPU 渲染测试
任务应单独启用它。此选项只控制依赖安装，不关闭程序自身的 Debug 验证逻辑。
Windows 同时覆盖原生 MSVC 构建和 Linux 上的 MinGW 交叉编译。
`profiles/windows-ci` 继承 `profiles/windows` 的项目设置并固定编译器版本；
本地原生构建将检测到的工具链 profile 与 `profiles/windows` 组合，不要求安装特定年份的 Visual Studio。
CI 不固定编译并行度，使用 Conan 和构建工具的默认策略。Conan 根据可用 CPU
（包括容器 cgroup 限制）选择并行度，并写入生成的 CMake build preset。

测试运行前加载 Conan 运行环境，单个测试超时为 120 秒。
测试失败会输出详细日志，没有发现测试也会报错。JUnit 报告和 `LastTest.log`
在成功或失败时均上传为各工具链独立的 `test-results-*` 附件。

host 和 build profile 都显式指定：项目构建为 Debug，构建工具为 Release。
原生构建的两种上下文使用同一份 profile，MinGW 交叉编译的 build profile 使用
`profiles/linux-gcc`，保证构建工具运行于 Linux。

Fedora 临时容器以 root 运行，Conan 安装命令为两种上下文设置
`tools.system.package_manager:mode=install`，允许依赖配方通过 dnf 安装
`xkeyboard-config-devel` 等系统开发包。这项设置仅用于 CI，不修改本地 profile。

Conan 固定为 2.33.0，其默认设置表支持 GCC 16，解决此前 2.27.0 不识别该版本的问题。
Slang 使用完整的 2026.18.3 官方发行包。Conan 包缓存按平台、工具链和依赖配置隔离，
每组构建使用独立 runner 和构建目录，不共享 CMake 缓存。
相同分支的新运行取消旧运行；矩阵设置 `fail-fast: false`，一组失败不会取消其他组。

`runtime/assets/` 未纳入 Git，因此 CI 创建空目录以满足构建时的资源复制步骤。
CPU 测试使用程序生成的数据，不创建窗口或 Vulkan 设备。CI 不启动游戏，也不执行
需要真实 GPU 的渲染集成测试；不验证外部资源完整性或 Vulkan 验证层输出。
