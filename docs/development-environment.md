# 开发环境排障

常规安装、构建与运行见[构建指南](../BUILD.zh-cn.md)，CI 工具链见 [CI](ci.md)。
本页收纳本地环境限制；这些限制不应直接复制到 CI 配置。驱动与编译器缺陷的复现、绕行和撤销条件见[外部缺陷记录](external-defects.md)。

## Slang 安装与编辑器根目录

Fedora 的不完整 `shader-slang` 安装曾缺少下游 `spirv-opt` / `slang-glslang` shim，表现为 Debug `-O0`
可编译而 Release 失败。需要完整 Slang 安装；例如使用已经安装完整发行版的 Mason 路径：

```sh
make SLANGC=~/.local/share/nvim/mason/packages/slang/bin/slangc BUILD_TYPE=Release
```

该参数设置各构建树缓存中的 `SLANGC_EXECUTABLE`，不是 MinGW 专用修复。路径仅为本机示例。

保留刻意为空的 `shaders/slangdconfig.json`。nvim-lspconfig 用它把工作区根定位到 `shaders/`，包含模块目录。
记录中的 Slang 2026.18 会递归遍历 ignored/build 目录并跟随符号链接，且没有循环追踪；
如果以仓库根为工作区，可能沿 `build/wine-prefix/dosdevices/z: -> /` 进入 `/proc`，耗尽 CPU、内存与 swap，编辑器退出后进程也可能继续运行。
这个文件是 Neovim 的根目录标记，不是编译器设置，也不代表已修复上游遍历问题。

## XKB、Wine 与 loader 输出

- 保留 Linux profile 的 `XLOCALEDIR=/usr/share/X11/locale`，供 Conan XKB 查找 compose/dead-key 数据。
- Windows 本地运行使用 `build/wine-prefix`，不要污染 `~/.wine`。运行环境没有 validation layers 时，必须显式传入 `--no-validation`；该运行不能作为验证层通过的证据。
- `driverVersion` 来自 physical-device properties，由厂商编码，日志按十六进制输出；不要按统一版本格式解码。
- loader 的 stderr 与应用 validation 输出分开调查。曾观察到 `/usr/libvulkan_dzn.so` 返回 -9 的 loader 消息，不能据此直接认定应用渲染出错。

## Hyprland 窗口检查

Hyprland 可能把新窗口平铺到屏幕外，即使窗口报告 visible。截图没有窗口时先检查实际位置。
这里没有 minimize 操作，零 extent 重建路径需要其他环境验证，不能以普通 resize 成功代替。

## 验证边界

Debug 开启 validation 和 synchronization validation；ERROR 回调会终止进程，因此保留行缓冲 stdout 以免丢失启动日志。
不要同时启用 GPU_ASSISTED 与 SYNCHRONIZATION_VALIDATION。关闭 validation 的性能测量、CPU CTest、
shader 编译和真实 GPU 执行是不同证据；记录结果时分别列明，不互相替代。
