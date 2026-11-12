# Ray query 阴影

使用 `VK_KHR_ray_query` 查询场景遮挡，方向光、点光和聚光灯发射一条硬阴影射线。
`SphereLight` 支持球形面积光近似，结合帧内采样和跨帧累积求可见度。可选漫反射间接光复用同一 TLAS，见 [DDGI](lighting.md#ddgi-漫反射间接光)；当前没有镜面 IBL。

## 球形光

`include/lc1/scene/lights/sphere-light.hpp` 提供球心 `position`、半径 `radius`、光色、强度和
`shadow_sample_count`，类型默认每像素每帧 1 条射线，示例参数在 `src/game/session/game-session.cpp` 中设置。
阴影采样数限制在 1–32；半径为零只发射一条射线，恢复点光硬阴影。
所有局部光源仅按距离平方反比衰减，没有距离截断参数；`radius` 只表示光源大小与阴影采样范围。

对每个着色点，根据指向光心的方向构造正交基 T、B。在光心的朝向接收点的圆盘上取点：

```text
sample_position = center + radius * (T * disk.x + B * disk.y)
visibility = 未被遮挡的射线数 / shadow_sample_count
```

圆盘半径使用面积分层与平方根映射，角度使用随机采样，避免采样挤在圆心。
随机种子由像素坐标、光源索引和帧序号共同确定；每帧使用不同的圆盘采样位置，
以便时间累积获得新样本。
射线从沿法线偏移的表面位置指向采样位置，并止于该位置之前，光源后方几何体不产生遮挡。

直接光仍在中心求方向、距离衰减及 GGX/Smith/Schlick PBR 项，再乘平均可见度。
这是球形光的圆盘阴影近似，不是球面发光的完整积分。
半径只控制阴影半影，不代表可见的发光球网格。方向光启用时仍使用硬阴影。
增加每帧采样数可加快收敛，但开销相应增加；默认通过下述时间累积减少低采样噪点。

## 保留 sample shading

分成三个通道：

1. 单采样通道计算表面的 ray query 可见度，每盏灯量化为 8 位，十盏灯打包进一个
   `RGBA32Uint` 像素，同时输出 `RGBA32Sfloat` 世界坐标和法线，使用独立单采样深度附件。
2. 全屏时间累积通道重投影、拒绝无效历史、限制历史值，输出过滤后的阴影。
3. 主通道使用最多 4× MSAA、完整 sample shading。材质、法线、衰减、高光仍逐采样计算，
   通过像素坐标读取过滤后的可见度，不在每个 MSAA 采样点重复追踪。

通道之间使用显式附件写入到片元读取屏障。单采样阴影和主光照入口启用提前深度测试；
阴影通道仍可能因绘制顺序产生过度绘制，并非严格每像素只调用一次着色器。

几何边缘的 MSAA 采样点可能属于不同表面，共享像素中心的阴影是近似，会出现细小的漏光或
阴影错配；像素中心没有几何覆盖时默认受光。该版本未实现按采样点/表面分类的边缘修复。

## 软阴影时间累积

只累积阴影可见度，保留现有 sample shading；不对整张彩色画面做 TAA，也不抖动相机投影。
使用世界坐标和上一帧的 view-projection 矩阵重投影，历史点必须仍在屏幕内，且表面编号、
法线、位置和沿法线的深度误差均通过检查。邻域只选择同一接收表面，将历史值限制在当前
3×3 邻域的可见度范围内，减轻移动遮挡物留下的阴影尾迹。

历史长度逐帧增加到 32，之后以 1/32 的当前帧权重滚动更新。初次显示、窗口尺寸变化、
灯光参数/数量变化和场景拓扑变化会使历史失效。移动接收物体不复用历史，
静态表面上的移动投影由邻域限制响应；这不是物体运动矢量重投影。
因此移动物体、新露出的表面仍可能暂时带噪，快速变化的半影也可能有短暂拖影。
半影需要几帧到几十帧收敛，8 位可见度缓存也带来量化误差。

历史采用 `RayQueryShadows` 持有的双缓冲，连续帧共享，不能仅按 frame slot 管理：某帧写入的图片还会被
下一帧读取。正常帧用队列屏障排序读写；改变尺寸时等待整个设备空闲后重建历史图片，
不能只等待当前帧 fence。每帧的原始阴影、表面和深度缓冲仍由各 frame slot 独立持有。

代价是更多图片和带宽。4K 下每张 RGBA32 图片约 127 MiB：两帧原始阴影/表面与两组历史
阴影/表面共约 1 GiB，另有深度及主通道 MSAA 附件。该实现优先验证时间累积，后续可测量后
压缩表面信息或降低阴影分辨率。

## 加速结构与范围

网格上传后构建一次 BLAS，实例共享网格的加速结构。每个在途帧各有一份 TLAS、实例缓冲区和
构建暂存缓冲区，等待该帧 fence 后才修改；实例列表不变时复用 TLAS，角色移动时重建。
构建完成后通过屏障连接到后续 TLAS 构建或片元射线查询。整个已加载大陆参与遮挡；
未来增加视锥剔除时，需单独保留视野外阴影投射者。

TLAS 实例 custom index 保存当帧 draw 索引，供 DDGI 命中后读取对应 geometry/material。
所有几何体按不透明、双面遮挡处理，未实现透明贴图裁剪或半透明阴影。
需要支持 ray query、acceleration structure 和 buffer device address 的 GPU，未提供硬件回退。
旧方向光深度图诊断接口已删除，渲染器只保留正常 ray-query 阴影路径。

## 模块边界

- `Renderer::add_passes()` 向当帧 `RenderGraph` 注册正常光照，输入为相机、灯光和绘制列表；
  应用可以继续追加 UI 节点。离屏入口 `Renderer::record()` 创建并执行同样的场景渲染图。
- `RayQueryShadows::add_passes()` 注册场景准备／TLAS、单采样可见度、时间累积和空间滤波四个
  节点；历史缓冲和各通道的屏障仍属于该模块。空间滤波录制完毕后才轮换历史索引并更新上一帧信息。
- 原始可见度和空间滤波结果使用不同的逻辑资源名；时间累积结果的依赖确保空间滤波覆盖原图前，
  原始可见度已经被读取。具体图结构见 [RenderGraph](render-graph.md)。

所有通道均使用 native descriptor heap，`PassData` 的 push data 选择 camera/lights/object、
TLAS 以及当前与历史图片。每个 draw 在命令流中记录自己的 object 索引；
各通道不创建 descriptor set 或 pipeline layout。

所有绘制通道使用 `GraphicsShaders` 和显式动态状态，不创建 graphics pipeline。
主通道、可见度、双附件时间累积、单附件空间滤波与色调映射各自重设状态，
避免采样数或颜色写入设置串到下一通道。见 [shader object](shader-objects.md)。

### TLAS 地址的 heap 读取（2026-10-06）

Slang 2026.18 将 `ResourceDescriptorHeap[index]` 转成 acceleration structure 时，
先从 heap 读取 64 位地址，再执行 `OpConvertUToAccelerationStructureKHR`。
因此 `visibility.spv` 声明 `Int64`；应用的设备需求与 GPU 测试使用的默认设备路径都必须
检查并启用 `shaderInt64`，否则创建 shader module 会触发 `pCode-08740`。

本机 RTX 4060 Laptop / NVIDIA 610.57.04 上，修复 feature 后，该原生 AS heap 读取路径
仍导致 `ErrorDeviceLost`，内核记录 Xid 109。CPU 检查确认描述符内保存了正确地址，stride
为 8；独立 GPU 原始地址回读却得到零。同一 TLAS 地址通过 push data 传入时可正常查询。
原始 SPIR-V 通过 `spirv-val --target-env vulkan1.4`；这些结果定位了失败路径，尚不能据此
确定 Slang 或驱动哪一方有缺陷。同类"分不清编译器还是驱动"的记录集中在
[外部缺陷记录](external-defects.md)。

生产可见度通道因此让 `PassData.scene` 索引一个 heap uniform-buffer descriptor。
每个帧槽的 `scene_address` 缓冲保存 8 字节 TLAS 地址，shader 读取
`ConstantBuffer<ShadowSceneData>` 后构造 `RaytracingAccelerationStructure`。
地址缓冲随 TLAS 创建，原地址上的重建不需更新它；两者都保留到该槽 GPU 工作完成。
每槽增加一个 buffer descriptor，不改变 ray query、同步、阴影采样或 native heap 架构。
`ResourceHeap::allocate_acceleration_structure` 保留为独立接口，生产 Renderer 不使用该路径；
恢复直接 AS heap 读取前必须重新验证。

Linux Debug 的 PBR、阴影切换/resize、时间累积/空间过滤 GPU 测试及主程序运行检查见[历史验证记录](history/ray-query-validation.md)。新增遮挡回归固定可见几何，仅切换高处物体的 `casts_shadow`：加入 TLAS 必须
产生可见阴影，移除后必须恢复原图，避免仅凭“没有崩溃”认定查询正确。


## 性能比较

Ray query 不保证比 shadow map 快。像素数、有效光源数、每灯采样数、过度绘制、几何复杂度、
硬件和 TLAS 更新都会影响成本。独立通道避免了 MSAA 对射线数量的倍增，代价是额外绘制与读写。
比较方案时应固定分辨率、相机、采样配置、光源和验证配置，测量 GPU 帧时间。
Debug 验证层也有 CPU 开销，不能仅凭“使用了硬件光追”判断性能。

## 验证

`lc1_temporal_shadow_tests` 在 GPU 上运行实际时间累积着色器并读回结果，检查累积权重、
历史重置、移动表面、深度/法线/表面编号拒绝、越界、移动阴影邻域限制、相机重投影和历史长度。
它需要显示环境、兼容 GPU 及验证层，故不加入默认 CPU CTest：

```sh
cmake --build --preset linux-x86_64-debug --target lc1_temporal_shadow_tests -j 8
cd build/linux-x86_64/Debug
. ./generators/conanrun.sh
./lc1_temporal_shadow_tests
```

## 空间降噪

`shaders/spatial-shadows.slang` 使用 5×5 联合双边滤波，仅处理半径大于零的球形光。
权重综合屏幕距离、法线夹角和双向切平面距离；表面 ID 不同、法线差异大或位置不连续的
邻居直接拒绝，背景不参与混合。采用切平面距离避免把倾斜平面上的正常深度变化视为边缘。

历史较短时允许阴影值不同的邻居混合，以清理单帧二值噪声；双方历史增长至 16 帧后，
收紧阴影值差异权重，尽量保留已稳定的阴影边缘。这是历史长度启发式，不是方差估计。
新显露区域的接触阴影仍可能暂时变软，不同 draw 的接缝不互相过滤。

空间结果只用于当前帧光照，不写回时间历史，避免反复滤波造成逐帧扩散。
复用原始可见度附件，不新增全屏图片；增加一次全屏绘制和邻域读取，未实现半分辨率、
自适应射线数或多尺度滤波。现有 MSAA 边缘共享阴影的近似仍然存在。

`lc1_temporal_shadow_tests` 是需要真实 GPU 和 Conan 运行环境的显式测试目标，验证时间累积、
空间噪声抑制、表面 ID/法线/深度边缘隔离、硬阴影与背景保留，以及稳定阴影对比度。

## Ray-query 渲染验证

`lc1_shadow_path_tests` 使用真实 GPU 绘制和像素回读，覆盖首次渲染创建 ray-query 资源、
两个帧槽的一致性、缩放及恢复尺寸、投影资格变化与空 TLAS、镜像物体和真实遮挡效果。
该目标同样不加入 CPU CTest，运行时开启验证层及同步验证：

```sh
cmake --build --preset linux-x86_64-debug --target lc1_shadow_path_tests -j 8
cd build/linux-x86_64/Debug
. ./generators/conanrun.sh
./lc1_shadow_path_tests
```

迁移与地址读取修复的实机结果见[历史验证记录](history/ray-query-validation.md)。

蒙皮模型先在 `Skinned geometry` pass 上传本帧 CPU 变形并重建帧槽独立 BLAS，
随后即使实例矩阵与 BLAS 地址未变，也重建 TLAS 以更新边界。
光栅可见性和 ray query 使用同一网格；变形接收面不复用历史。蒙皮数据与资源归属见[资源管理](resources.md#骨骼蒙皮)。
