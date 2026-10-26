# RenderGraph 渲染调度

应用的帧录制回调创建局部 `RenderGraph`，调用 `Renderer::add_passes()` 注册场景渲染，
再追加 ImGui overlay，在录制 GPU 命令前编译依赖，然后在调用者已开始录制的
`CommandBuffer` 中同步执行。回调随本次调用结束销毁，不会把上一帧的帧槽、extent、
相机、draw span 或输出目标保存在 Renderer 成员中。离屏调用可继续使用
`Renderer::record()`，它创建并执行只包含场景渲染的局部图。

`record()` 和 `add_passes()` 统一接收 `scene::Scene`，不再提供相机、灯光和 draw 列表
分开传入的入口；输出设置和文字输入仍作为独立参数。`Scene` 是当帧只读借用视图，
不拥有或同步游戏变换，不管理模型资源。调用方须让相机、灯光和绘制列表存活到注册返回；
注册时仍复制为下述 `FrameScene` 快照。未指定活动相机时，两个入口均显式失败。

## 当前通道

`frame.inputs` 表示注册时复制的相机、灯光和 draw 记录；`output.target` 表示调用者
提供的输出目标。两者以逻辑资源导入；输出目标以及 draw 引用的 GPU 资源仍由调用者持有。

| pass | 输入 | 输出 |
|---|---|---|
| Shadow scene preparation | `frame.inputs` | `shadow.scene`、`shadow.frame`、`shadow.history` |
| Shadow visibility | `shadow.scene`、`shadow.frame` | `shadow.raw_visibility`、`shadow.surface` |
| Temporal shadow resolve | `shadow.raw_visibility`、`shadow.surface`、`shadow.history`、`shadow.frame` | `shadow.temporal_visibility`、`shadow.temporal_surface` |
| Spatial shadow filter | `shadow.temporal_visibility`、`shadow.surface`、`shadow.frame` | `shadow.filtered_visibility` |
| HDR lighting（含世界文字） | `shadow.frame`、`shadow.filtered_visibility` | `hdr.color` |
| Tone mapping | `hdr.color`、`output.target` | `output.color` |
| UI text | `output.color` | `output.ui_text` |
| ImGui overlay（应用添加） | `output.ui_text` | `output.ui` |

`RayQueryShadows::add_passes()` 注册前四个 pass，Renderer 注册光照与色调映射以及界面文字的
pass；应用将 ImGui 绘制注册为消费 `output.ui_text` 的最后一个节点，渲染层不依赖 ImGui。
场景准备包括 camera/light/object UBO 上传、TLAS 更新、附件创建与历史准备；
光照 pass 保留线性 RGBA16F HDR、最多 4× MSAA 和 pass 内 resolve，之后才执行色调映射。
世界文字在光照 pass 的同一个渲染实例末尾混合绘制：瞬态 MSAA 附件不保证跨实例内容，
留在同一实例里既能复用场景深度做遮挡，也不需要第二次 resolve。详见[文字渲染](text-rendering.md)。
图的执行顺序由输入／输出依赖决定，不要求生产者先于消费者注册。

## 逻辑资源与同步

一个资源名只表示一个值或版本，由一个 pass 产生或从图外导入。
修改已有内容时需要使用新的输出名；导入和 pass 输出不能共用同一个名字。
多个输入来自同一个生产者时只生成一条依赖；同时就绪的 pass 按注册顺序选择。

`shadow.raw_visibility` 与 `shadow.filtered_visibility` 实际复用同一张帧槽图片。
空间滤波依赖时间累积结果，因此在覆盖原始可见度前，时间累积已经读取原图。
图不分析物理图片别名；增加原图消费者时，必须确保所有读者在覆盖节点前执行。

图只负责依赖校验和命令录制调度，不分配图片或自动生成屏障。
现有附件 layout transition、TLAS barrier、描述符 heap 绑定仍由各 pass 负责。
原始附件和 HDR 附件属于 `FrameResources`，只能在其帧槽 fence 完成后复用。
跨帧历史仍由 `RayQueryShadows` 双缓冲持有，resize 时等待整个设备空闲；
空间滤波结果不写回时间历史。历史索引和上一帧信息在空间滤波录制后更新。

`FrameLoop` 继续负责 acquire、submit、present 及输出图片转换。
Renderer 接收和返回的目标布局均为 `COLOR_ATTACHMENT_OPTIMAL`；UI pass 保留色调映射
写入到附件 load／混合的屏障。交换链信号量的 `COLOR_ATTACHMENT_OUTPUT` 等待阶段和同步规则不变。

## 编译与生命周期

`compile()` 返回 `std::expected<void, std::string>`，检查缺失输入、重复生产者、
资源自身依赖、环、重复 pass 名、空名字及空回调。错误包含相关 pass／资源名。
图变更或编译失败后，旧执行顺序立即失效；必须重新成功编译才能 `execute()`。
失败不会留下可执行的部分顺序，应用和 Renderer 的录制入口通过 `lc1::fail()` 报告编译错误。

一般图可以重复执行，但当前阴影回调会上传当帧数据并轮换历史，因此每次注册的
渲染帧只执行一次。回调借用的对象必须存活且输入保持不变，直到同步执行返回；
回调不得修改正在执行的图。

Renderer 在 `add_passes()` 注册时将相机、灯光和 draw 记录复制到一份不可变的
`FrameScene` 快照，由阴影和光照回调共同持有；各 pass 不重复复制列表。
调用方先保存 `WorldPresentation::draws()` 的结果，再用它构造 `scene::Scene`；注册返回后可以释放列表或修改原输入。
快照复制 draw 记录中的变换和标记，但不复制 mesh/material 指向的 GPU 资源；
这些资源及 Renderer、FrameResources、输出目标仍必须满足原有生命周期要求。
文字输入按值捕获，字体资源仍为借用。

## 验证

`lc1_render_graph_tests` 加入 CPU CTest，验证乱序注册、分支汇合、导入和版本、
缺失依赖、重复生产者、自环／环、编译失效及恢复。测试回调只记录 CPU 结果，
不创建 Vulkan 设备或调用 Vulkan 命令。

真实渲染通过显式 GPU 目标验证：`lc1_shadow_path_tests` 覆盖 1×／支持的 MSAA、
双帧槽、空场景、无灯光与恢复、resize、caster 变化及遮挡像素；`lc1_pbr_tests`
检查材质、HDR 和输出设置；`lc1_temporal_shadow_tests` 检查历史累积和空间滤波。
运行命令与 Conan 环境见 [ray-query 阴影](ray-query-shadows.md)。

历史实机结果见 [2026-10-09 验证记录](history/render-graph-validation-2026-10-09.md)。
