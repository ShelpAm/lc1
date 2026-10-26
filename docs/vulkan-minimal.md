# 从一帧理解项目的 Vulkan 渲染

本文解释当前调用链，适合先了解代码再阅读具体 pass。项目使用 dynamic rendering、shader object
和原生 descriptor heap；官方 ImGui Vulkan backend 保留自己的 pipeline 与 descriptor set。
模块边界见 [Vulkan 分层](vulkan-layers.md)，资源借用见[资源管理](resources.md)，同步约束见下文。

## 先找到一帧的入口

[Application](../app/application.cpp) 更新游戏并准备相机、灯光、绘制项与文字，然后调用
[FrameLoop::draw_frame](../include/lc1/vk/presentation/frame_loop.hpp)。帧循环负责交换链与提交，
通过同步回调把已开始录制的命令缓冲、当前帧资源和输出目标交给应用。

```text
等待帧槽 fence → 获取交换链图像 → 开始录制并转换输出布局
→ 应用注册并执行 RenderGraph → 转换为呈现布局 → 提交 → 呈现
```

这是成功取得图像时的路径。过期交换链、暂停绘制与 surface 丢失由帧循环结果交给应用处理；
不能在没有取得有效图像时继续按正常路径录制。

## 设备与队列：谁选择，谁使用

Application 根据具体 surface 检查呈现支持，选择满足图形、计算和呈现要求的队列族与 GPU。
[Device](../include/lc1/vk/core/device.hpp) 接收选择结果与 `DeviceRequirements`，创建设备、分配器和队列；
它不接收或保留 surface。离屏测试通过自己的辅助函数选择设备。

当前使用 Device 拥有的一条队列。录制只是生成命令，提交后 GPU 才执行；函数返回不代表 GPU 已完成。
多队列需要独立评估调度收益和同步成本，不是当前架构的默认扩展方向。

## 帧槽与交换链图像：两种不同的复用周期

| 对象 | 所有者 | 复用条件 |
|---|---|---|
| 命令缓冲、acquire 信号量、fence、逐帧 GPU 资源 | FrameLoop 的帧槽 | 当前槽的 fence 完成 |
| 交换链图像、view、present 信号量 | Swapchain / SwapchainImage | 按图像获取与呈现协议复用 |
| 跨帧阴影历史 | Renderer 内的 RayQueryShadows | 按历史读写协议更新；resize 等待所有 GPU 工作 |

当前有两个帧槽，使 CPU 可以准备下一帧而 GPU 仍在处理已提交的工作。
fence 让 CPU 确认槽位可重用；acquire 和 present 使用的二元信号量连接图像获取、渲染提交与呈现。
帧槽数量不等于交换链实际图像数量，present 信号量必须跟随实际图像。

只在确定提交的路径上 reset fence；取得 SUBOPTIMAL 图像仍需渲染与呈现，再请求重建。
异常退出后由 Application 等待设备完成，之后才能销毁 GPU 正在使用的对象。
帧循环修改必须保持以下不变量：

1. 仅在必定提交的路径上 reset 当前槽 fence，否则下一次等待可能永久阻塞。
2. 先检查 acquire 结果再读取图像索引。SUCCESS/SUBOPTIMAL 都得到图像；SUBOPTIMAL 也会 signal
   semaphore，必须渲染、呈现后再重建，不能按 OUT_OF_DATE 丢弃（`VUID-vkAcquireNextImageKHR-semaphore-01286`）。
3. 只 reset 当前帧 command buffer，不 reset 含其他 pending 帧的 pool（`VUID-vkResetCommandPool-commandPool-00040`）。
4. acquire → render barrier 的 source stage 为 `COLOR_ATTACHMENT_OUTPUT`，与 semaphore wait stage 一致；
   `NONE` 会使布局转换失去同步（`SYNC-HAZARD-WRITE-AFTER-READ`）。
5. 每张实际交换链图像拥有自己的 present semaphore；不按请求的最小图像数建立独立数组。

保留 `VULKAN_HPP_HANDLE_ERROR_OUT_OF_DATE_AS_SUCCESS`，OUT_OF_DATE 属于帧循环控制流。
先捕获 `vk::SurfaceLostKHRError` 再捕获 `vk::SystemError`，返回 `FrameResult::SurfaceLost`。
交换链只在 extent 可用时懒创建；重建先检查零 extent，不能提前销毁旧资源；显式清空图像后才销毁旧 handle。
`SwapchainImage::image` 是借用，不能包装成拥有所有权的 `vk::raii::Image`。

## 附件与布局：开始绘制之前要约定什么

Dynamic rendering 在录制时给出附件 view、格式相关状态、load/store 操作等信息。
`loadOp = Clear` 指定通道开始时清除附件；`Load` 保留已有内容，前提是内容有效且访问已正确同步。
世界文字留在 HDR 光照的同一次 rendering 中，以复用深度和 MSAA 附件；屏幕文字在 tone mapping 后绘制。

布局表示图像的使用状态，不能简单理解成一种固定显存排列。当前呈现边界由 FrameLoop 将目标转换为
`COLOR_ATTACHMENT_OPTIMAL`，回调必须以该布局返回，随后再转换为 `PRESENT_SRC_KHR`。
HDR、深度、阴影和历史图片有各自的转换与访问依赖，不是整帧只需要两次 barrier。

图像 barrier 同时声明布局转换与访问同步。当前 acquire 后的转换使用 `COLOR_ATTACHMENT_OUTPUT`
源阶段，与提交等待 acquire 信号量的阶段匹配；不能因为丢弃旧内容就把同步要求一并去掉。

## Shader 与资源绑定：当前项目的做法

[ShaderObject](../include/lc1/vk/resources/shader-object.hpp) 持有可执行 shader，渲染层的
`GraphicsShaders` 绑定所需阶段并清空未使用阶段。`set_graphics_state` 为各 pass 设置固定功能的动态状态。
完整约定见 [shader object](shader-objects.md)。

每个帧槽有独立 descriptor heap。CPU 填写描述符，pass 通过 push data 传递资源索引，shader
用索引访问 heap。材质、对象和帧数据按绘制项准备，不能让后一次写入覆盖前一次 draw 尚需读取的数据。
只有该帧槽 fence 完成后才能更新属于它的可变描述符。结构生成与绑定验证分别见
[heap 结构生成器](heap-struct-generator.md)和[heap 测试](descriptor-heap-test.md)。

## RenderGraph：组织 pass，不代替 Vulkan 同步

Renderer 注册阴影准备、可见度、时间累积、空间滤波、HDR 光照、tone mapping 和 UI text；
Application 最后注册 ImGui overlay。图检查逻辑资源依赖并同步执行录制回调，不分配图片、不推导物理别名，
也不自动生成 barrier。增加 pass 时需要同时设计图依赖和实际 GPU 访问同步。
输入输出表及回调生命周期以 [RenderGraph](render-graph.md) 为准。

## 修改入口

| 修改目标 | 入口 |
|---|---|
| 世界中出现哪些物体、灯光和文字 | `WorldPresentation`、`GameUi` |
| 材质求值与 HDR 输出 | [光照](lighting.md) |
| 阴影采样与历史 | [ray-query 阴影](ray-query-shadows.md) |
| 新 pass 的注册和顺序 | [RenderGraph](render-graph.md) |
| 窗口、交换链重建和提交 | `Application`、`FrameLoop`、`Swapchain` |

修改 CPU 数据可先运行相关 CPU 测试；修改渲染、资源绑定或同步还需运行相关 GPU 目标并检查 validation
与 synchronization validation。命令见[构建指南](../BUILD.zh-cn.md)，CPU 测试成功不能替代 GPU 验证。
