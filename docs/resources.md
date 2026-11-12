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

## 资源版本管理（Git LFS）

`.gitattributes` 将 `runtime/assets/` 下参与版本管理的文件，以及 `docs/` 下的 PNG、JPEG、GIF、WebP、
AVIF、BMP、TIFF、ICO 和 SVG 图片交给 Git LFS（扩展名不区分大小写）。其他文档和代码继续由普通 Git 保存。
资源按用途分类：

| 目录 | 内容 |
|---|---|
| `runtime/assets/models/` | 被配置引用的模型，以及生成这些模型所需的 GLB／Blender 源文件 |
| `runtime/assets/textures/` | 被材质和渲染代码引用的贴图 |
| `runtime/assets/fonts/` | 游戏和字体测试使用的字体 |
| `runtime/assets/unused/` | 没有代码、种子存档或生成脚本引用的本地备用资源；由 `.gitignore` 排除 |

`unused/` 只保留本机副本，不进入新提交或构建产物。构建重新生成输出目录的 assets 副本，
避免旧路径或已停用资源残留；游戏存档不在该生成目录内。

新增资源照常 `git add` / `git commit`；本地 Git LFS 过滤器生成指针，推送钩子先上传资源内容。
更换机器前安装 Git LFS，已有工作区使用 `git lfs install` 和 `git lfs pull` 补齐资源。

本次接入不重写历史：既有文档图片从新提交开始使用 LFS，旧提交中的普通 Git blob 保留。
原始资源与运行时派生资源一并跟踪；修改兰博原始 GLB 后需执行 `scripts/prepare-rumble.py`，
将原文件、派生 GLB 和贴图在同一提交中更新。CI 检出时下载实际 LFS 内容，构建复制的不是指针。

## 持久渲染场景

`scene::Scene` 是引擎层的持久 CPU 表现状态，由 `WorldPresentation` 拥有；不再是每帧的借用包装。
Scene 通过 `unordered_map<T const*, unique_ptr<T>>` 独占拥有灯光参数和模型实例，不使用字符串标识。
`add_light()` 和 `add_model()` 返回非空借用指针；`remove_light()` 和 `remove_model()` 按指针删除，平均 O(1)，
空指针或不属于当前 Scene 的有效指针显式失败。删除后不得继续使用对应指针。
地形、展示物和角色统一使用模型实例；单网格使用只有一个网格节点、没有动画的模型定义。
Scene 不拥有 GPU 资产，也不认识角色等玩法类型。
返回的指针在对应条目删除或场景销毁时失效；添加、rehash 或删除其他条目不使其失效。

`WorldPresentation` 在装配时注册地形、展示物、角色和各角色可使用武器定义的模型实例，决定播放哪个动画。
`Character` 和 `Weapon` 不保存模型实例借用，模拟线程不访问 Scene。
`WorldPresentation` 按角色 ID 保存角色/武器实例关联，从主线程快照单向提交姿态，
并在攻击序号变化时播放武器动作。Scene 继续独占实例，GPU 资源与材质仍由表现层拥有。
角色名单和物品定义在表现层存活期间保持不变；主手关联可随快照切换，重新加载通过替换完整会话完成。
Scene 的 `SceneModel` 拥有 `ModelInstance`（节点姿态和播放状态），借用完整的 `GpuModel` 和材质，由同一资产初始化姿态并取得 GPU 网格，
并保存游戏层提交的世界矩阵与可见性、阴影／深度偏移标记。
`visible` 控制整个实例是否提交；`view_visible`、`use_view_indices` 与节点掩码只控制相机光栅绘制，仍提交完整投影几何。
蒙皮的可选 `view_index_count` 表示身体索引前缀；DrawItem 的同名字段为空时绘制全部，为零时只保留阴影/GI。
索引范围在资产构造与渲染输入入口校验；颜色和阴影接收面光栅通道读取相同前缀，TLAS/BLAS 和 DDGI 使用完整索引。
切换相机可见性会使阴影时间历史失效，帧容量不变。上述借用在条目删除前必须存活且不移动。
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
同一片段不能重复写同一节点的同一属性。CUBICSPLINE 和动画混合尚未实现；骨骼蒙皮见下文。

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

glTF 导入只读取几何、层级、蒙皮与动画，不自动导入材质绑定。默认 `GltfMaterialPolicy::WarnIfPresent`
在源文件带材质时提示调用方绑定绘制材质；已由 `ModelConfig.material` 提供外部材质的角色/武器加载路径
显式使用 `UseExternal`，不再对此预期行为报警。该选项不跳过几何、蒙皮、扩展或贴图加载校验。

当前 `WorldPresentation` 按角色与武器的 YAML 模型配置加载 GPU 资源；相同配置共享资源与材质，
占位几何另外按身体尺寸区分，贴图按规范路径和色彩空间共享。
刚性模型的 CPU 几何在 GpuModel 构造返回前释放；蒙皮资产保留绑定姿态几何和权重；
在 Scene 中为每个角色创建独立 `ModelInstance`，游戏层只保存借用指针。
占位内容由 `make_character_model()` 构造，身体和头部是两个网格节点，共享定义中有头部左右转动的 `Idle` 和双次点头的 `Nod`；
角色与武器的模型路径、节点、局部变换、材质及动画字段见[模型配置](save-data.md#角色与武器的-model)，
占位角色保留 `Nod`，配置了待机片段的角色使用独立相位的循环动画。角色世界位置从 `WorldSnapshot` 读取，更新末尾将角色世界矩阵提交给 Scene。
帧容量按每个角色实际展开的绘制项数量计算并检查 uint32 上限。
角色可配置待机、移动、身体攻击、死亡与 F2 动作片段；移动根据表现位置变化切换，单次动作结束后回到移动或待机。动画不驱动 gameplay 位移（无 root motion）；表现时间与渲染帧一致；固定模拟与线程交换见[代码边界](code-layout.md#固定步长模拟与线程交换)。

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
CUBICSPLINE、morph 和额外权重组（如 JOINTS_1）明确报错，不产生声称支持这些特性的模型。
材质仍由 YAML 显式绑定，glTF 材质和透明特效尚未自动接入。

### 骨骼蒙皮

GLB/glTF 支持一个顶点最多四个关节的线性混合蒙皮。加载器读取 `skin.joints`、可选
`inverseBindMatrices`（缺省单位矩阵）、`JOINTS_0` 和 `WEIGHTS_0`；支持 8/16 位关节索引、
浮点或归一化 8/16 位权重，在导入入口校验并归一化正权重总和。
节点重排、primitive 展开和 ModelConfig 校正父节点插入都会重映射绑定节点与关节。
缺失法线展开为平面三角形时，权重跟随顶点同步展开。

`Model` 保存经过校验的 `SkinnedMesh`：网格节点、关节调色板、逆绑定矩阵、绑定顶点、索引和权重。
`GpuModel` 对蒙皮资产额外保留这些 CPU 数据；刚性资产继续在上传后释放 CPU 几何。
`ModelInstance` 使用独立的节点动画姿态，在 CPU 计算
`inverse(mesh_node) × joint_node × inverse_bind`，按权重混合顶点变换；法线使用混合线性变换的逆转置，
切线重新正交化并保留镜像手性。零缩放和奇异变换继续明确失败，不降低现有可逆变换约束。

Scene 的 DrawItem 保存本帧变形顶点的不可变快照。Renderer 在各个 FrameResources 中独立拥有
动态顶点/索引缓冲、上传暂存和 BLAS，仅在对应帧槽 fence 完成后复用。
`Skinned geometry` pass 在同一次帧提交中上传顶点并重建 BLAS，随后重建 TLAS；
光栅、ray-query 阴影和 DDGI 读取相同的变形几何，不使用绑定姿态阴影，也不逐帧等待队列空闲。
阴影历史使用原始资产身份，避免不同帧槽缓冲导致全场景拓扑失效；变形接收面本身拒绝历史。
当前是 CPU 蒙皮加 GPU 上传/BLAS rebuild，尚无 GPU 蒙皮、动画混合或 root motion。

兰博接入资源由 `python scripts/prepare-rumble.py` 从原始 `models/rumble/source.glb` 生成；
保留骨架、权重与待机、跑步、攻击、死亡、玩笑五段动画并原样提取底色贴图。
透明护盾和使用零缩放切换可见性的其他片段属于尚未实现的特效路径，不纳入人物变体；原文件不修改。
`runtime/saves/01` 和 `blacksmith` 中 lc 使用该变体；已有 checkpoint 保留自己的外观配置，不自动迁移。

yyx 使用 `models/yone/封魔剑魂.glb`。`python scripts/prepare-yone.py yone` 从 `models/yone/source.glb`
生成模型及 `textures/yone/base-color.png`，将原文件的四张底色图拼为一张图集并改写各网格的 UV，
以适配当前单模型单材质绑定。原文件的节点、骨骼和动画保持不变；已有 checkpoint 仍保留自己的外观配置。
铁匠使用 `models/western-yone/西部魔影_炽天使_永恩.glb`。`python scripts/prepare-yone.py western-yone`
从同目录的 `source.glb` 生成模型及 `textures/western-yone/base-color.png`，并只保留兼容可逆变换约束的
待机、行走、攻击、死亡与玩笑动画。两份原始 GLB 都已从 `unused/` 移入对应的模型目录。

北极星使者_彗使用 `models/winterblessed-hwei/北极星使者_彗.glb`。`python scripts/prepare-winterblessed-hwei.py` 从同目录的
`source.glb` 提取两张内嵌贴图，分别缩放至 1024×1024 后合成为 `textures/winterblessed-hwei/base-color.png`。
子网格共享 UV accessor，按各子网格索引引用的顶点仅改写一次 UV，映射至对应图集区域；模型保留原始动画，种子存档的
3 号角色使用 `Idle1_Base`、`RunSlow`、`RunFast`、`Attack1_A`、`Death` 和 `Joke`。

CPU 导入测试覆盖权重混合、逆绑定矩阵、网格局部空间、独立实例和非法绑定；
可额外执行 `lc1_model_loading_tests <rumble.glb>` 逐段采样真实资源。
GPU `lc1_pbr_tests --player-models` 覆盖真实模型的变形绘制、帧槽一致性及生产装配路径。

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

运行期主手切换复用会话构造时加载的武器定义资源和每个角色的显示实例，未选中的实例隐藏；
帧容量包含这些实例，切换不在 GPU 在途时销毁资源，也不动态扩展帧容量。手电筒的程序模型与光束见 [资源与穿戴](design/resources.md#手电筒)。

### GI 网格读取

`GpuMesh` 暴露已有顶点/索引缓冲的只读 GPU 地址、索引步长和三角形数，供 DDGI 命中着色使用，
不复制网格或延长 CPU 几何寿命。上传屏障同时覆盖片元阶段的物理地址读取。
16 位索引缓冲分配到四字节边界，shader 以 32 位字读取并取半字，避免要求额外的 shaderInt16 feature；
三角形索引和光栅绘制数量不变。地址和借用的有效期与对应 mesh 一致，必须活到提交完成。
