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
- `scene::Scene` 中的实例、`DrawItem`、`scene::Object`、`RenderTarget` 借用资源；资源在成员声明顺序中先于使用者。
  材质所有者保持纹理存活到 GPU 使用结束，Device 比所有 GPU 资源长寿。
- `GpuBuffer::upload` 必须执行所需 VMA flush；描述符使用前完整初始化，绘制容量溢出显式失败。

坐标采用右手系、Y 向上；glTF 无需轴转换，Z-up 数据需要导入校正（教程房间绕 X 轴 −90°）。
校正通过模型根节点变换表达，CPU 几何保持导入坐标；法线、镜像与绕序见[角色与模型校正](character.md#模型校正)，
切线与法线贴图见[光照](lighting.md)。

## 持久渲染场景

`scene::Scene` 是引擎层的持久 CPU 表现状态，由 `WorldPresentation` 拥有；不再是每帧的借用包装。
Scene 通过 `unordered_map<T const*, unique_ptr<T>>` 独占拥有灯光参数和模型实例，不使用字符串标识。
`add_light()` 和 `add_model()` 返回非空借用指针；`remove_light()` 和 `remove_model()` 按指针删除，平均 O(1)，
空指针或不属于当前 Scene 的有效指针显式失败。删除后不得继续使用对应指针。
地形、展示物和角色统一使用模型实例；单网格使用只有一个网格节点、没有动画的模型定义。
Scene 不拥有 GPU 资产，也不认识角色等玩法类型。
返回的指针在对应条目删除或场景销毁时失效；添加、rehash 或删除其他条目不使其失效。

`WorldPresentation` 在装配时注册地形、展示物和角色模型实例，保存角色与场景实例借用指针的关联，决定播放哪个动画。
Scene 的 `SceneModel` 拥有 `ModelInstance`（节点姿态和播放状态），借用完整的 `GpuModel` 和材质，由同一资产初始化姿态并取得 GPU 网格，
并保存游戏层提交的世界矩阵与可见性、阴影／深度偏移标记。上述借用在条目删除前必须存活且不移动。
每次世界更新后，表现层提交角色最终世界矩阵，Scene 通过 `update_animations()` 推进所有模型动画，
通过 `SceneModel::node_world_matrix()` 查询最终挂点，在 `draws()` 中展开模型节点。
角色变换仍是 GameWorld 的权威状态，Scene 中的世界矩阵只是单向提交的表现副本，不能反向修改玩法位置。

相机由 `PlayerView` 持有，渲染调用显式指定相机；Scene 没有全局 active camera。
`lights()` 生成启用灯光的值快照，`draws()` 从可见实例生成 DrawItem，集合遍历顺序不作保证，模型内部按节点顺序展开；
禁用／隐藏不会删除条目，`draw_count()` 按全部模型（含隐藏模型）的带网格节点总数计算帧容量。Renderer 在注册渲染图时生成这些列表，
与相机值一起放进不可变帧快照；随后修改 Scene 不改变已生成的矩阵和灯光。
材质和 GPU 资源仍按借用约束存活，Scene 的删除操作不等待 GPU，也不释放借用的资产。

## 模型资源与实例

模型相关类型 `Model`、`GpuModel`、`ModelDefinition`、`ModelNode`、`NodeTransform`、`ModelInstance` 和 `Mesh`
统一位于 `include/lc1/scene/`，使用 `lc1::scene` 命名空间。

`Model` 是 CPU 加载与预处理结果，拥有网格数组及独立的 `ModelDefinition`；构造时一次提交完整的几何、节点和动画。
OBJ 加载器先收集顶点与索引，再通过同一个单网格构造入口生成模型，不先创建空模型再修改其内部数组。
`ModelDefinition` 只拥有运行时共享的节点层级、默认姿态、网格索引和动画片段，不包含 CPU 几何或 GPU 资源。
表现层通过 `GpuModel{device, model}` 一次创建运行时资源：参数按值接收 Model，依网格索引上传全部几何，
然后移入共享定义，构造结束释放参数中的 CPU 顶点和索引。临时模型直接传入，已完成预处理的局部模型可用 `std::move(model)` 转交；
需要保留 CPU 几何时传入左值，显式承担模型复制成本。
`GpuModel` 拥有共享定义和 GPU 网格数组，提供只读 `definition()` 与 `meshes()` 借用；
它禁止复制和移动，以保持定义及网格借用稳定。Device 必须比它长寿，销毁前须等待相关 GPU 工作完成。
CPU 测试可直接使用 `Model` / `ModelDefinition` / `ModelInstance`，无需构造 GpuModel。
`model.definition()` 在左值上返回只读借用，右值上返回拥有数据的值；不得在实例借用期间移动共享定义。节点通过索引引用父节点和网格，允许多个节点
复用同一网格，也允许没有网格的挂点和多个根节点。构造要求父节点先于子节点，
因此自引用、环和越界父节点均被拒绝。节点数量和顺序在构造后固定，名称不要求唯一。

`ModelInstance` 只借用存活、未移动且不再修改的 `ModelDefinition`，不可借用临时定义；
加载用的 `Model` 无需与实例同时存活。
实例的局部姿态数组与模型节点等长、同序，每个实例独立保存；重置恢复模型默认姿态。
节点姿态使用平移、四元数和缩放，按 `T × R × S` 组合，要求有限且可逆。
实例按父先子后的顺序计算模型空间累计矩阵，不缓存角色世界矩阵。
最终绘制矩阵为 `SceneModel 世界矩阵 × 节点模型空间矩阵`，Scene 在绘制展开与挂点查询时使用同一份已提交变换。

OBJ、程序化顶点/索引构造生成一个引用网格 0 的单位根节点，多网格构造保留调用者提供的节点变换。
`vertices` 和 `indices` 接受网格索引，默认索引为 0；Model 不再提供 GPU 上传接口。
单独上传几何时使用 `GpuMesh{device, model.vertices(index), model.indices(index)}`，
需要完整模型资源时使用 GpuModel。GPU 模型和材质继续由表现层持有，Scene 只借用它们。
单网格资产可通过 `GpuModel{GpuMesh{...}}` 接管已上传网格；已有网格数组与定义可一起转交构造，构造时检查节点网格索引。
`SceneModel::model` 必须非空，默认姿态从其定义初始化；注册及绘制时检查姿态定义与资产一致，禁止拼接不同资产。
地形和 PBR 展示同样持有 GpuModel，向场景提供模型实例，而非将裸网格重新包装。

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
占位角色的 `Eyes` 节点挂在头部下；表现层保存挂点索引，在动画更新后向 Scene 查询，
把眼睛世界变换（位置与旋转）交给第一人称相机。

当前 `WorldPresentation` 通过 `GpuModel{device, make_character_model(body)}` 创建共享角色资源，
加载用的 CPU 几何在 GpuModel 构造返回前释放；
在 Scene 中为每个角色创建独立 `ModelInstance`，游戏层只保存借用指针。
占位内容由 `make_character_model()` 构造，身体和头部是两个网格节点，共享定义中有头部左右转动的 `Idle` 和双次点头的 `Nod`；
表现层按玩家身份选择 `Nod`，NPC 使用独立相位的 `Idle`。角色世界位置从 `Character` 读取，更新末尾将角色世界矩阵提交给 Scene。
帧容量按每个角色实际展开的绘制项数量计算并检查 uint32 上限。
待机动画不是行走、攻击或 root motion；表现时间与会话一致，每帧最多推进 0.1 秒。

`Model::load_from_file()` 支持 OBJ、glTF 和 GLB。glTF 导入默认场景（未指定时选第一个场景）；
没有 scene 时推导所有根节点。只导入选中场景的节点及其动画通道，按父先子后重排并重映射动画目标。
共享网格不重复存储；每个 primitive 形成独立网格，多 primitive 节点通过单位子节点展开。
支持 TRIANGLES、索引及非索引几何、交错缓冲、accessor/view 偏移、8/16/32 位无符号索引、
浮点 POSITION/NORMAL/TANGENT、浮点或归一化无符号整数 UV0/颜色；缺少法线时生成平面法线。
保留 TRS 以及可分解为 TRS 的 matrix；有剪切或透视的矩阵明确拒绝。
导入器保留一个单位根节点；需要轴向或单位校正时，在文件层级中提供校正父节点，
原始几何与动画通过同一层级变换，不使用额外 correction 参数。
动画导入 translation/rotation/scale 的 STEP 和 LINEAR，继续使用运行时的完整校验与独立实例播放。
缓冲范围、步长、属性数量、索引和节点拓扑均在返回模型前检查。

材质绑定尚未导入：文件含材质时记录警告，调用方仍须提供绘制材质。
稀疏 accessor、必需扩展、压缩 primitive、非三角形、额外顶点属性、透明顶点色、
CUBICSPLINE、morph、骨骼和蒙皮明确报错，不产生声称支持这些特性的模型。
世界场景图、材质导入、骨骼和蒙皮尚未实现。
后续应先打通实际模型导入与材质绑定，再接入 skin 和 GPU 蒙皮，不能把刚性节点动画当作蒙皮完成。

`lc1_model_tests` 在无窗口、无 Vulkan 设备的情况下验证父子变换、实例隔离、非法拓扑和导入校正。
`lc1_model_animation_tests` 覆盖平移/旋转/缩放采样、STEP 边界、循环和单次播放、
零时长及巨大时间步、反号四元数、非法片段与失败不提交、多节点展开、角色占位尺寸及 CPU 模型销毁后的动画、绘制展开和姿态重置。
GPU 目标 `lc1_pbr_tests` 增加共享网格的多节点动画回读，检查运动后的覆盖变化、实例隔离和重播。

可按构建指南编译 `lc1_pbr_tests` 后，在构建目录加载 Conan 环境并运行
`./lc1_pbr_tests --model-animation` 单独验证模型节点动画；不带参数运行完整 PBR 用例。
单项通过不代表完整 PBR、主程序或其他平台通过，GPU 结论还要求干净的 validation 输出。

`lc1_model_loading_tests` 通过临时 glTF/GLB 文件验证实际解析、交错缓冲和偏移、层级重排、
多 primitive 与共享网格、平面法线、matrix 节点、无 scene 根推导、动画目标重映射与导入校正顺序，
并检查越界缓冲/索引、非法拓扑及不支持特性的失败路径。
