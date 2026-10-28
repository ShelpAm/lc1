# 光照

当前使用 metallic-roughness PBR 直接光照：GGX 法线分布、Smith-GGX 遮蔽项、Schlick Fresnel
及 Lambert 漫反射。底色为 sRGB 贴图解码后的线性颜色乘材质 factor、顶点颜色；顶点颜色只表达材质色。

每个 MSAA 采样点计算材质与直接光，阴影可见度仍按像素计算：

```text
L = 从表面指向光源的单位向量
V = 从表面指向相机的单位向量
N = 世界空间单位法线
H = normalize(L + V)
alpha = max(roughness, 0.045)²
F0 = lerp(0.04, base_color, metallic)
diffuse = (1 - F) * (1 - metallic) * base_color / pi
specular = D * G * F / (4 * NdotV * NdotL)
color = emissive
      + hemisphere_ambient(normal) * base_color * (1 - metallic)
      + Σ visibility * radiance * (diffuse + specular) * NdotL
```

背光、视线处于表面背侧或半程向量退化时，直接光 BRDF 为零。`sample_light` 已计算点光源的距离衰减和
聚光灯的锥角衰减，这些权重只应用一次。主通道保留最多 4× sample shading，按实际 HDR 和深度格式
选择支持的采样数。当前没有 IBL，但主通道加入了轻量半球环境补光：天空色主要照亮朝上的表面，
地面反弹色为陡峭侧坡保留较低的最低亮度，背光坡和阴影中的物体不会完全变黑；金属另有一个很弱的
环境高光项以保留轮廓。补光强度刻意低于主方向光，避免整体发灰。该补光不参与阴影，也不是完整
的环境反射或间接光。

`scene::PbrParameters` 保存线性参数，`scene::PbrMaterial` 另带非拥有的 CPU 图像引用。
游戏持有 `MaterialInfo`（参数及 GPU 纹理/采样器借用），`DrawItem::material` 指向它。
多个物体可共享并修改同一份 CPU 材质，不再创建 `GpuMaterial`，也没有独立材质 descriptor pool
或 64 个存活材质的限制。参数使用 glTF 默认值；程序生成的普通表面显式设 metallic = 0、
roughness = 0.8。

`Renderer::record()` 在验证输入后，将材质参数及 heap 索引复制到当前 `FrameResources`。
每个 draw 各有 object、material、frame UBO，后者保存该 draw 的资源索引；不会在录制下一次
draw 时覆盖前一次 draw 的数据。camera/lights 在同一帧槽内共享。CPU 参数只需活到 record
返回，mesh、纹理和采样器则必须活到对应 GPU 工作完成。

主材质通道使用原生 descriptor heap。每个帧槽持有自己的 heap；buffer 描述符在创建时分配，
纹理/采样器槽位首次使用时分配，后续在等待该槽 fence 后原位写入。因此改变参数、换纹理或
resize 不持续消耗新槽位，也不修改其他在途帧的描述符。buffer 的重复查询通过每个帧槽自己的 `DescriptorHeapCache`；会原位重写的 image 槽位
直接持有索引，不与缓存的资源身份混用。帧容量仍由 `make_frame_resources` 指定，
超限在上传前明确失败，不自动扩容。

阴影、时间累积、空间滤波和 tone mapping 也都使用 native descriptor heap。
`PassData` 将各通道所需的资源索引作为 push data 记录，每个 draw 的 object 索引独立。
相机、灯光与物体 buffer 在 ray-query 准备阶段上传一次，随后主材质通道复用；不能在提交前
清除阴影接收面 ID 或覆盖其他通道要读取的数据。各通道使用 shader object，不创建 graphics
pipeline、pipeline layout 或 descriptor set layout/pool。ImGui 按显式约定继续使用官方 Vulkan
后端，其内部 pipeline 和 descriptor set 是例外。主片元 shader 通过实际读取 `SV_SampleIndex`
并在该采样点插值 UV 保持完整 sample shading；实现及验证见 [shader object](shader-objects.md)。

已采样底色、metallic-roughness 和 emissive；MR 的 G 是粗糙度，B 是金属度，分别乘 factor。
底色和 emissive 使用 `TextureColorSpace::Srgb`，MR 使用 `Linear`，错误的色彩空间在绘制前验证时报错。
缺少上述贴图时绑定白色替代纹理，因此常量 factor 同样有效。
normal 贴图已接入，必须使用 `TextureColorSpace::Linear`；`normal_scale` 缩放切线空间 XY 分量。
材质 UBO 的 `flags` bit 0 表示贴图存在。没有贴图或强度为零时直接用网格法线，不采样默认法线纹理。
occlusion 仍待 IBL 接入，非空贴图目前明确报未支持。
所有材质目前按不透明单面绘制，alpha 不开启混合；参与投影的几何仍视为不透明遮挡物。

颜色先写入各帧槽的 RGBA16F 附件，在 HDR 空间完成 MSAA resolve，再用单采样全屏通道执行
`mapped = (color * exposure) / (1 + color * exposure)`。曝光默认 1，必须有限且大于零。
输出到 sRGB attachment 时由硬件编码；UNORM 显示输出显式传 `OutputEncoding::Srgb`，
线性离屏输出使用默认 `Linear`，避免二次 gamma。超出 half-float 范围的亮度饱和到 65504。

程序生成的箱体各面拥有独立法线；地面和道路法线朝上。OBJ 保留自带法线，没有法线时使用三角形面法线。顶点法线通过模型矩阵的逆转置变换到世界空间，并在片元阶段重新归一化，因此支持非均匀缩放和镜像变换。零缩放等不可逆模型矩阵会在上传前报错。

## 切线与法线贴图

`Vertex::tangent` 的 xyz 是物体空间切线，w 是副切线手性（+1 或 -1）；默认全零表示没有切线。
`generate_tangents(vertices, indices)` 使用 Conan 的 `mikktspace/cci.20200325` 生成 UV0 切线，
按每个三角形角点的结果重新索引，因此会拆开 UV 镜像接缝，不能用旧索引覆盖不同切线。
`Model::generate_tangents()` 是当前 CPU 几何的便捷入口，在交给 `GpuModel` 或构造 `GpuMesh` 之前调用。
不使用法线贴图的网格不需要执行这一步。

生成过程拒绝退化 UV、退化三角形、非法索引和非有限数据，失败时保留原网格。
对于已经烘焙好的法线图，应在非均匀导入修正之前准备匹配的切线；已有的切线会随模型修正变换、
正交化，并在镜像时翻转 w。绘制使用法线贴图却没有有效切线的网格时，会在 Vulkan 绘制之前报错。

顶点阶段用模型矩阵的线性部分变换切线，用逆转置变换法线，并将模型的镜像符号乘入切线手性。
片元阶段重新正交化 T、归一化 N，按 `B = cross(N, T) * sign` 构造副切线，再把采样法线转换到世界空间。
法线图采用切线空间 +Y 约定；Vulkan viewport 的 Y 翻转不需要额外翻转法线图的绿色通道。
阴影射线偏移、时间历史和空间滤波继续使用未扰动的表面法线；背向该表面的光不会因法线图而漏光。
这不改变轮廓、碰撞或实际几何高度。

## 球形面积光与阴影

`SphereLight` 用 `position` 表示球心，`radius` 表示半径，`shadow_sample_count` 控制阴影采样数。
它与点光源一样，以中心位置计算方向和距离平方反比衰减，没有距离截断；遮挡则在朝向着色点的光源圆盘上采样多个位置，
把未遮挡射线的比例作为 `visibility`。半径为零退化为点光硬阴影。
这是面积光阴影近似，PBR BRDF 仍以球心方向求值，不能表现光源面积对高光的完整影响。

方向光、点光和聚光灯也投射 ray query 硬阴影。每个像素的光源可见度先进行时间累积，再供主通道读取。默认每帧 1 条射线、最多 32 帧历史；
材质、高光和
光照衰减仍按 MSAA 采样点计算，但阴影射线数量不随 MSAA 倍增。
采样、缓冲布局和已知限制见 [Ray query 阴影](ray-query-shadows.md)。

## 主场景展示区与验证

`make run` 进入游戏后，出生点道路两侧就是 PBR 展示区，直接由 `src/game/session/game-session.cpp` 装配。
旧的 `make run-pbr` 是同一入口的兼容别名；不再单独构建展示程序。
展台与背板使用相同的 `ContinentBlock` 数据绘制并参与碰撞，中间保留 11 米通道。

| 位置（相对出生点） | 展示内容 |
|---|---|
| 左前方 | 3×5 金属度／粗糙度矩阵；其前方增加 NORMAL OFF／x1／x2 三块相同表面的对照 |
| 右前方 | 程序生成的底色纹理、同图乘 tint、线性 MR 图；金／铜／银／铁色导体与蓝色涂漆 |
| 两侧中部 | 同功率点光／球形光、匹配的栏杆和球形遮挡物，比较硬阴影与软阴影；右侧有移动遮挡物 |
| 左后方 | 0.25／1／4／16／64 发光强度阶梯、发光贴图与曝光响应 |
| 右后方 | 使用轻微细节法线的镜像彩色方块、非均匀缩放椭球和旋转方块 |

光源支架使用开放框架，避免封闭的灯泡网格遮住自己的光源。发光表面只增加自身亮度，
没有实现对周围物体的间接照明；球形光仍是中心光照加软阴影近似。
展区使用共享网格、共享贴图及创建后不变的材质，动画只更新对象矩阵。
强波纹仅用于 NORMAL OFF／x1／x2 对照台；普通变换展品采用低幅度的细节法线，避免像深褶皱或穿孔。
标牌文字作为印刷内容，通过[文字渲染](text-rendering.md)的混合文字通道绘制：它在光照 pass 末尾
与场景共享深度，因此仍会被遮挡，但不参与 ray-query 投影，也不再走不透明材质通道。
ray-query 遵守此标志，切换投影资格时会重置阴影历史。

沿用 WASD 移动、滚轮缩放、M 切换斜视／第一人称、Y 解锁镜头、Space 回到玩家。
新增调试操作也经 Gameplay 输入路由：

| 按键 | 操作 |
|---|---|
| F4 | 全部 → 方向光 → 点光 → 聚光灯 → 球形光 → 仅发光材质 |
| F5 | 暂停／继续展品动画 |
| PageUp / PageDown | 曝光增减半档，范围 1/32～16 |

窗口标题显示当前灯光模式与曝光。关闭直接光可以检查纯发光图案，降低曝光可以分辨高亮色阶。

`lc1_pbr_tests` 是需要显示/GPU 的显式测试目标，普通 CPU CTest 不运行它：

```bash
cmake --build --preset linux-x86_64-debug --target lc1_pbr_tests -j 4
cd build/linux-x86_64/Debug
. ./generators/conanrun.sh
./lc1_pbr_tests
```

测试实际 shader 的 BRDF 参考值、默认纹理、MR 通道、色彩空间、发光、粗糙度边界、各类光源、
HDR 读回、曝光、sRGB 输出、MSAA 后色调映射、resize、共享 CPU 材质修改、逐 draw 数据隔离及纹理切换。
还检查展区中央道路畅通、展台碰撞、动画矩阵更新、主场景斜视与地面视角。
CPU `tangent_space` 测试覆盖平面 UV、旋转 UV、镜像接缝和退化输入；模型测试覆盖烘焙的切线变换。
GPU 测试另检查默认法线、强度零、缺失切线、错误色彩空间、镜像 UV、镜像／非均匀实例变换，
并与直接写入世界空间参考法线的结果对照。
输出 `artifacts/pbr-showcase.ppm`、`artifacts/pbr-emission.ppm`、`artifacts/pbr-normals.ppm` 和 `artifacts/pbr-transforms.ppm`，
验证与同步验证均开启。
## Native heap 的字段读取约定

heap `ConstantBuffer` 中的矩阵与 `Light` 都**逐字段读取**，不整块传给 `mul` 或函数：
矩阵按 GLM 存储的列逐列读取后求线性组合（镜像手性用三列混合积），灯光逐字段读进局部
`Light` 再传给 `sample_light`。这不是风格偏好，而是绕开两个外部缺陷——NVIDIA 驱动对整块
矩阵加载的处理，以及 Slang 对 heap 嵌套数组的寻址。现象、触发形状、最小复现、外部链接与
复测方法见[外部缺陷记录](external-defects.md)第 1、2 条；`shaders/text.slang` 用四个
`float4` push constant 传矩阵，因此不经过这两条路径。

PBR GPU 测试覆盖投影、物体平移、逆转置法线、镜像/非均匀缩放、镜像 UV，以及不同光源。
保留这些数值检查；升级 Slang 或改变 heap 读取写法时需重新运行，不能只检查编译成功。

各阶段的实机结果与未验证范围见[历史验证记录](history/lighting-validation.md)。
