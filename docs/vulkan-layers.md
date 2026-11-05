# Vulkan 代码分层

`include/lc1/vk/` 与 `src/vk/` 使用相同的四层目录。类型仍位于 `lc1` 命名空间，
Vulkan 调用继续使用 `vk::raii`；目录分层不改变资源所有权、提交顺序或销毁顺序。

| 目录 | 职责 | 代表类型与文件 |
|---|---|---|
| `core/` | Vulkan 绑定配置、loader、实例与设备创建、分配器 | `VulkanLoader`、`Instance`、`Device`、`memory.hpp` |
| `resources/` | GPU 存储、上传、命令录制、描述符、shader object 与图像屏障 | `CommandBuffer`、`one-time-submit.hpp`、`GpuBuffer`、`GpuImage`、`GpuTexture`、`GpuMesh`、`AccelerationStructure`、`DescriptorHeap` / `DescriptorHeapCache`、`ShaderObject`、`RenderTarget` |
| `presentation/` | 窗口表面、交换链策略、acquire/submit/present、帧槽同步 | `Surface`、`Swapchain`、`FrameLoop`、`swapchain-policy.hpp` |
| `render/` | 渲染图调度、具体渲染算法、shader 接口、材质和绘制数据 | `Renderer`、`RenderGraph`、`GraphicsShaders`、`GraphicsState`、`MaterialInfo`、`DrawItem`、`FrameResources`、阴影与色调映射 |

## 依赖方向

箭头表示“使用”。应用将呈现与渲染组合起来；两者没有彼此的实现依赖。

```mermaid
flowchart TD
    app[Application] --> presentation[presentation]
    app --> render[render]
    presentation --> resources[resources]
    render --> resources
    resources --> core[core]
    presentation --> core
    render --> core
```

- `core` 不包含其他三层，也不包含窗口、场景或游戏类型。
- `resources` 可以使用 `core` 与 CPU 图像、顶点、切线数据，不依赖呈现、渲染或游戏。
- `presentation` 可以使用 `core`、`resources` 和 `Window`，不认识具体 `Renderer` 或材质。
- `render` 可以使用 `core`、`resources` 和场景输入，不读取窗口或交换链。

目前这四层仍编入 `lc1_engine`，CMake 源文件清单按层组织，没有新增四个独立链接目标。
公开头文件根目录仍共享，因此构建目标本身不强制隔离头文件；新增依赖时需遵守上述方向。

## 边界上的几个约定

`Device` 接收调用者选择的物理设备、队列族和 `DeviceRequirements`，负责创建设备与分配器。
应用在选择物理设备时查询表面的呈现支持；`Device` 本身不接收或保存表面。GPU 渲染测试
通过测试辅助函数完成设备筛选，再调用同一显式构造函数。

`CommandBuffer` 与 `one-time-submit.hpp` 位于 `resources/`，由呈现与渲染层共同使用。
`CommandBuffer` 拥有命令缓冲并维护当前录制的 heap 绑定缓存；绑定 heap 和 reset 统一经过它，
`DescriptorHeap` 的底层绑定入口仅供它调用。通过原生句柄改写 heap 绑定后，必须调用
`invalidate_bindings()`，再使用包装层的绑定与 push-data 接口。
`GraphicsShaders` 留在 `render/`，决定各 pass 绑定和清空哪些 shader 阶段，再调用
`CommandBuffer::bind_shaders`。资源层只接收 Vulkan 阶段与句柄，不依赖渲染层类型。

`FrameLoop<T>` 管理帧槽、命令缓冲和同步，通过回调调用渲染；模板参数由应用选择。
具体灯光、阴影、HDR 附件和描述符集合组成的 `FrameResources` 属于 `render`。
两者共享的 `RenderTarget` 只是非拥有的输出 image view 与 extent，放在 `resources`；
曝光和输出编码策略单独放在 `render/output-settings.hpp`。

应用的帧录制回调创建当帧 `RenderGraph`。`Renderer::add_passes()` 接收 `RayQueryShadows`
注册的四个阴影节点，再注册 HDR 光照、色调映射和 UI text，应用最后追加 ImGui overlay。
离屏调用可用 `Renderer::record()` 创建并执行七个场景节点（包括 UI text）。图验证逻辑资源依赖并同步执行
录制回调；图不拥有图片，也不接管 pass 内的 Vulkan 屏障或呈现层的 acquire／submit／present。
回调随本次录制结束销毁，不在 Renderer 成员中保存帧槽、相机或目标的借用。
资源版本、历史和验证约定见 [RenderGraph](render-graph.md)。

`Pipeline`、`ShaderModule` 和 `ShaderStages` 已删除。通用 `ShaderObject` 在资源层读取
SPIR-V 并持有 `vk::raii::ShaderEXT`；渲染层的 `GraphicsShaders` 管理各 pass 的顶点/可选
片元阶段，`set_graphics_state` 记录固定功能的动态状态。shader 的 GPU 使用结束后才能销毁。
具体状态与逐采样约定见 [shader object](shader-objects.md)。
交换链 extent 选择位于 `presentation/swapchain-policy.*`，图像屏障位于
`resources/image-barrier.*`，`core/common.*` 只保留绑定配置与通用 Vulkan 诊断。

`GpuMesh` 负责上传、绘制和 BLAS 资源，不包含材质或每次绘制的变换。
这些引用与变换由 `render/draw-item.hpp` 的 `DrawItem` 表达。
CPU `Vertex` 位于 `lc1/vertex.hpp`，不包含 Vulkan；与 shader location、format 对应的
输入布局位于私有的 `src/vk/render/vertex-input.hpp`。

设备的固定扩展名与 feature 配置位于 `src/vk/core/device.cpp`；通用能力协商框架已移除。
绘制校验和顶点输入布局位于 `src/vk/render/`。应用只包含 `include/lc1/` 下的公开接口。

Vulkan 调用失败直接传播 `vk::SystemError`，保留具体异常类型与原始诊断；
仅在能够恢复时捕获，例如帧循环将 surface lost 转为 `FrameResult::SurfaceLost`。

## Dispatch、设备与生命周期约束

- 系统 loader 只有一个：`VulkanLoader` 长期持有 `vk::raii::Context`，在 GLFW 初始化前将其
  `vkGetInstanceProcAddr` 交给 `glfwInitVulkanLoader`。不引入 Conan vulkan-loader、volk、
  裸 `vk*` 调用或第二套/default dispatcher。
- 保留 `VK_NO_PROTOTYPES`，`VULKAN_HPP_ENABLE_DYNAMIC_LOADER_TOOL` 使用默认值 1。
  通过 raii CommandBuffer/Queue 录制与提交；原始参数和 hpp 句柄数组使用解引用后的 `*handle`。
  不对 null/default raii 对象调用操作，其 dispatcher 可能为空。
- `vk::` 结构体按声明顺序使用 designated initializer；保留 `VULKAN_HPP_NO_STRUCT_CONSTRUCTORS`，
  不定义 `VULKAN_HPP_NO_SETTERS`，数组/count 配对继续使用 setter。
- VMA 前包含 `common.hpp`。使用 `vma::raii::Allocator{instance, device, info}` 时，
  `info.instance`、`info.device`、`info.pVulkanFunctions` 留空，由包装层从 raii dispatcher 填充。
- Application 按 `src/vk/core/device.cpp` 的显式要求筛选设备，包括 BDA、maintenance5、
  ray-query/acceleration-structure/deferred-host-operations 扩展及所需 feature，检查 graphics、compute、
  present 支持并排除 CPU 设备；VMA feature flags 与之保持一致。`LC1_DEVICE=<substring>` 选择 GPU，
  启动日志输出名称、类型与驱动。不增加通用能力协商框架。
- 构造顺序为 `VulkanLoader → Window → Instance → Surface → Device → Renderer → GameSession → FrameLoop`，
  逆序销毁；Application/GameSession 不可移动。Window 的首成员 `glfw_lifetime_` 保证构造失败时也终止 GLFW，
  Window 自己拥有并只销毁一次 GLFW C 句柄。
- Device 的 allocator 先于逻辑设备销毁，Instance 的 messenger 先于 instance 销毁。
  Surface 比 Swapchain 长寿；交换链 view/semaphore 先于 handle 销毁。
- `FrameLoop<T>` 用应用提供的 factory 创建帧槽资源，等 fence 后才把资源传给同步录制回调，不解释其内容。
  Renderer 接收资源、相机与绘制项，不接收帧索引；draw i 使用当前槽对应的 object/material/frame UBO。
  资源借用与上传约束见[资源管理](resources.md)。
- Vulkan 使用负 viewport height 且 `y = height`；保持背面剔除，模型行列式为负时 front face 为 clockwise，
  否则为 counter-clockwise。
