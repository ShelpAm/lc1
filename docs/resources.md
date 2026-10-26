# 资源加载与借用生命周期

当前游戏表现对象直接构造资源，`ResourceManager` 尚不是统一加载入口；见[模块依赖图](module-dependencies.md)。
本页规定资源接口的所有权与卸载约束，应用成员顺序见[代码边界](code-layout.md#生命周期与资源)。

- `ResourceManager` 按场景独占，只在渲染线程使用。类型缓存以 `unique_ptr<Resource>` 拥有资源；
  重复加载返回稳定指针，失败不得留下空缓存项。
- `load<T>` 要求 T 派生自 Resource、可移动构造，并提供
  `load_from_file(Device const&, path const&) -> T`。网格仍使用独立缓存。
  `GpuTexture` 上传 CPU Image，不是文件加载 Resource；ResourceManager 不依赖 Renderer。
- 资源 ID 相对于指定 asset root 词法规范化，拒绝绝对路径和目录逃逸；不合并符号链接别名。
- 卸载会立即使该资源的借用失效，没有引用计数。卸载资源或销毁 manager 前，必须停止使用相关借用，
  并等待使用它的 GPU 工作完成。RAII 销毁本身不等待 GPU。
- `DrawItem`、`scene::Object`、`RenderTarget` 借用资源；资源在成员声明顺序中先于使用者。
  材质所有者保持纹理存活到 GPU 使用结束，Device 比所有 GPU 资源长寿。
- `GpuBuffer::upload` 必须执行所需 VMA flush；描述符使用前完整初始化，绘制容量溢出显式失败。

坐标采用右手系、Y 向上；glTF 无需轴转换，Z-up 数据需要导入校正（教程房间绕 X 轴 −90°）。
校正只在 Model 烘焙一次，不再逐实例应用；法线、镜像与绕序见[角色与模型校正](character.md#模型校正)，
切线与法线贴图见[光照](lighting.md)。

## 模型资源与实例

模型相关类型 `Model`、`ModelDefinition`、`ModelNode`、`NodeTransform`、`ModelInstance` 和 `Mesh`
统一位于 `include/lc1/scene/`，使用 `lc1::scene` 命名空间。

`Model` 是加载与预处理结果，拥有 CPU 网格数组及独立的 `ModelDefinition`。
`ModelDefinition` 只拥有运行时共享的节点层级、默认姿态、网格索引和动画片段，不包含 CPU 几何或 GPU 资源。
上传完成后，通过 `std::move(model).definition()` 移出共享定义，再销毁局部 `Model`，释放 CPU 顶点和索引；
有额外 CPU 几何用途的调用方可以自行延长加载结果的生命周期。
`model.definition()` 在左值上返回只读借用，右值上返回拥有数据的值；不得在实例借用期间移动共享定义。节点通过索引引用父节点和网格，允许多个节点
复用同一网格，也允许没有网格的挂点和多个根节点。构造要求父节点先于子节点，
因此自引用、环和越界父节点均被拒绝。节点数量和顺序在构造后固定，名称不要求唯一。

`ModelInstance` 只借用存活、未移动且不再修改的 `ModelDefinition`，不可借用临时定义；
加载用的 `Model` 无需与实例同时存活。
实例的局部姿态数组与模型节点等长、同序，每个实例独立保存；重置恢复模型默认姿态。
节点姿态使用平移、四元数和缩放，按 `T × R × S` 组合，要求有限且可逆。
实例按父先子后的顺序计算模型空间累计矩阵，不缓存角色世界矩阵。
最终绘制矩阵为 `Character 世界矩阵 × 节点模型空间矩阵`，读取角色最终状态时直接组合，
不增加角色位置的场景副本或同步阶段。

原有 OBJ、程序化顶点/索引构造生成一个引用网格 0 的单位根节点，导入校正仍只烘焙一次。
多网格构造保留调用者提供的节点变换，不接受额外导入校正；`vertices`、`indices` 和
`gen_mesh` 接受网格索引，默认索引 0 保持旧调用方式。GPU 网格和材质继续由表现层持有，
`Model` 目前仍包含 GPU 上传入口，不是独立的纯 CPU 模块。

`ModelDefinition` 拥有只读 `AnimationClip` 和通道。通道按节点索引驱动平移、四元数旋转或缩放，
当前支持 STEP 和 LINEAR；旋转 LINEAR 使用归一化四元数的最短弧球面插值。
构造模型时校验目标、严格递增的非负有限时间、值类型、关键帧数量和数值；
同一片段不能重复写同一节点的同一属性。CUBICSPLINE、混合和骨骼蒙皮尚未实现。

`ModelInstance::play_animation()` 从默认姿态开始播放指定片段；播放索引、秒数和循环标记
均属于实例。片段时长为所有通道最后关键帧时间的最大值，通道范围外保持首/末值；
循环按整个片段时长取模，单次播放在末尾保持，零时长片段保持唯一关键帧。
更新要求有限且非负的时间，零时间仍可重新采样；`reset_pose()` 停止播放并恢复默认姿态。
采样在候选姿态上完成，验证后才提交姿态与播放时间；无效输入或插值产生奇异变换时保留旧状态。

`mesh_instances(world_transform)` 为每个带网格的节点生成网格索引及最终世界矩阵，
跳过纯挂点，重复网格引用分别输出。它不拥有 GPU 资源，也不保留世界矩阵。
共享定义 `draw_count()` 统计带网格节点数，而不是唯一网格数。
`find_node(name)` 返回首个同名节点索引，找不到时返回空；挂点使用稳定索引查询。
`node_world_matrix(index, world_transform)` 同样支持无网格节点，显式拒绝越界索引或非法最终矩阵。
占位角色的 `Eyes` 节点挂在头部下；表现层保存挂点索引，在动画更新后直接组合角色世界变换，
把眼睛世界变换（位置与旋转）交给第一人称相机，不复制角色位置到模型实例中。

当前 `WorldPresentation` 的角色资源只保留共享占位 `ModelDefinition` 及按网格索引上传的 `GpuMesh` 数组，
加载用的 `Model` 在角色资源构造返回前销毁，不长期保留 CPU 几何；
为每个角色持有独立 `ModelInstance`，按上述展开结果生成 DrawItem。
占位内容由 `make_character_model()` 构造，身体和头部是两个网格节点，共享定义中有头部左右转动的 `Idle` 和双次点头的 `Nod`；
表现层按玩家身份选择 `Nod`，NPC 使用独立相位的 `Idle`。角色世界位置始终直接读取 `Character`，没有增加同步阶段。
帧容量按每个角色实际展开的绘制项数量计算并检查 uint32 上限。
待机动画不是行走、攻击或 root motion；表现时间与会话一致，每帧最多推进 0.1 秒。

世界场景图、材质导入、骨骼和蒙皮尚未实现；glTF 文件解析草稿保留，几何/层级及动画导入明确报错。
后续应先打通实际模型导入与材质绑定，再接入 skin 和 GPU 蒙皮，不能把刚性节点动画当作蒙皮完成。

`lc1_model_tests` 在无窗口、无 Vulkan 设备的情况下验证父子变换、实例隔离、非法拓扑和导入校正。
`lc1_model_animation_tests` 覆盖平移/旋转/缩放采样、STEP 边界、循环和单次播放、
零时长及巨大时间步、反号四元数、非法片段与失败不提交、多节点展开、角色占位尺寸及 CPU 模型销毁后的动画、绘制展开和姿态重置。
GPU 目标 `lc1_pbr_tests` 增加共享网格的多节点动画回读，检查运动后的覆盖变化、实例隔离和重播。

可按构建指南编译 `lc1_pbr_tests` 后，在构建目录加载 Conan 环境并运行
`./lc1_pbr_tests --model-animation` 单独验证模型节点动画；不带参数运行完整 PBR 用例。
单项通过不代表完整 PBR、主程序或其他平台通过，GPU 结论还要求干净的 validation 输出。
