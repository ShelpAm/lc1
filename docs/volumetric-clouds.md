# 体积云

层式体积云：天空由可平铺的三维形态、独立细节噪声和一个水平高度区间定义，逐像素沿视线积分
密度、消光与散射，再在色调映射之前合成进 HDR 画面。云层不是实体、不进入 ECS、不参与
物理或存档；渲染模块只读取相机、方向光和场景深度。

当前范围是地面观察：相机位于云层下方，不支持云层内部的近距离观察、云体独立管理、
降雨/雷暴、地面云影或天气模拟。

## 模块与资源

| 组件 | 位置 | 职责 |
|---|---|---|
| `CloudSettings` | `include/lc1/vk/render/volumetric-clouds.hpp` | 高度、覆盖率、密度、风、光学与步进参数 |
| `make_cloud_shape_noise()` | `src/cloud-noise.cpp` | 生成可平铺的 Perlin-Worley R8 体数据，纯 CPU、确定性 |
| `VolumetricCloudRenderer` | `src/vk/render/volumetric-clouds.cpp` | 持有形态、细节体纹理与两个 shader，注册并录制两个 pass |
| `shaders/clouds.slang` | 视线步进 + 光照步进 | 输出 `float4(辐亮度, 透射率)` |
| `shaders/clouds-composite.slang` | 合成 | 输出 `cloud.rgb + cloud.a * scene.rgb` |

资源由帧槽持有，两张噪声体纹理属于 `VolumetricCloudRenderer`：

| 资源 | 格式 | 用途 |
|---|---|---|
| `shape_noise_` | `R8_UNORM` 128³ | 云层形态与覆盖阈值，重复寻址采样 |
| `detail_noise_` | `RG8_UNORM` 64³ | 独立 Worley 的中频翻卷与高频侵蚀；增加 512 KiB 体素数据 |
| `FrameResources::cloud_image` | `RGBA16F` | 视线步进结果：rgb 累积辐亮度，a 累积透射率 |
| `FrameResources::hdr_cloud_image` | `RGBA16F` | 合成后的 HDR，Bloom 与色调映射读它 |
| `FrameResources::cloud_buffer` | UBO | 每帧云参数，布局与 `CloudData` 一一对应 |
| 场景深度 | 现有深度附件 | 限制积分终点 |

`GpuImage` 新增三维构造，`extent()` 仍按二维返回，`extent_3d()` 保留深度；二维 mipmap
生成拒绝多层体数据，而不是把深度当高度展平。

深度附件现在带 `SAMPLED_BIT`，且只在启用体积云时把 `storeOp` 设为 `eStore`：云的遮挡
需要它，其余帧仍丢弃深度。这两个改动都属于深度附件既有契约的扩展，见
[RenderGraph](render-graph.md)。

## 密度场

两张噪声体在首次启用云层时一次性生成并上传，Debug 构型下有可见的启动开销；
之后常驻显存，不会逐帧重算。生成算法是确定性的整数哈希，因此同一尺寸每次得到相同字节；测试据此校验。

`sampleCloudDensity(worldPosition, time)` 是视线步进和光照步进共用的唯一定义：

```text
p        = worldPosition - windVelocity * time
h        = (worldPosition.y - CloudBottom) / (CloudTop - CloudBottom)
profile  = smoothstep(0, 0.3, h) * (1 - smoothstep(0.55, 1, h))
q        = p / NoiseScale * (1, 2, 1)
mass     = ShapeNoise.Sample(q)
u        = q * DetailScale + 偏移 + (mass - 0.5) * (1.3, -0.9, 1.1)
fields   = DetailNoise.Sample(u)
billow   = lerp(0.5, smoothstep(0.2, 0.8, fields.r), BillowWeight)
detail   = lerp(0.5, fields.g, DetailWeight)
mass     = saturate(mass + (billow - 0.5) * 0.2)
remapped = saturate((mass * profile - (1 - Coverage)) / max(Coverage, eps))
erosion  = DetailStrength * (1 - detail) * 0.25
bodies   = smoothstep(0, 0.45, saturate((remapped - erosion) / (1 - erosion)))
density  = bodies * Density
```

形态体决定大云团的位置；独立细节体的 R 通道保存 2 格周期 Worley，G 通道保存 8、16 格
周期 Worley 按 0.75、0.25 权重合成的结果，亮值靠近胞元中心。一次 RG 采样同时获得
中频翻卷与高频侵蚀：前者扰动云团形态，后者切掉胞元间的薄边缘。每次密度求值共两次
三线性采样。细节坐标受低频形态场扰动，避免形成整齐、等大的小泡。
视线与太阳射线共用密度定义，并按各自积分步长估计采样足迹：中频/高频超过可解析范围时，
`BillowWeight` / `DetailWeight` 平滑减小到零，噪声回到均值 0.5，减少远处的颗粒走样。
这属于振幅滤波近似，不是完整的体纹理 mip 预滤波。

- 世界坐标采样，不是相机相对坐标：相机移动不会带动云层，云层形态只由世界位置与时间决定。
  采样距离受最大追踪距离限制，float 精度足够。
- 覆盖率移动阈值而不是缩放任一常数，因此 0 附近密度必为零、1 时保留噪声原始形态。
- 高度包络先作用于形态场，再做覆盖率阈值：弱云团在更高处才形成密度，强云团向下鼓出，
  云底不再只是所有云团共享的一张水平渐变面。垂直形态频率为水平的两倍，让云团在层内有起伏。
- 细节通过减法和重映射切掉低密度边缘，再做 `smoothstep`，而不是仅乘系数变暗；
  `DetailStrength` 越大，薄边缘越容易消失，厚实内部保留。
- 风把采样位置按 `windVelocity * time` 平移，时间来自 `steady_clock` 的挂钟秒数，
  与帧率无关。高度包络仍使用世界高度，与视线和光照求交的固定云层范围一致；
  垂直风只平移层内噪声，不会把整层密度移出求交范围。
- 两张体数据都在首次启用时由 CPU 确定性生成并上传，之后常驻；没有逐帧噪声生成。
  天气分布图尚未实现。

## 视线步进

1. 由像素构造 NDC，经 CPU 预先求逆的 view-projection 还原世界射线；相机位置由 CPU 给出。
2. 深度在四个 MSAA 采样点里取最近值；深度仍是清除值（1.0）时视为无几何体，用最大追踪
   距离截断，而不是把远平面距离当作终点的假象。
3. 求交云底与云顶两个高度平面，得到进入/离开距离；`|dir.y|` 退化时直接判定不相交。
4. `t_end = min(层离开距离, 场景距离, MaxTraceDistance)`；不是云层正前方的射线立即跳过。
5. 固定 `ViewSteps` 步，步长由有效区间决定，每步取分段常值近似。各像素使用固定的
   分层采样偏移，打散相邻射线同步采样产生的等高线条带；偏移不随帧变化。
6. 采样距离接近 `MaxTraceDistance` 时按 0.5～1.0 区间渐变到零，避免云层在远距离被切成
   平面；超出追踪距离的视线直接回到“无云”。

透射率按 Beer-Lambert 逐段累加：

```text
segment     = exp(-Extinction * density * stepLength)
radiance   += viewT * (1 - segment) * source
viewT      *= segment            # 低于 0.005 时提前结束
```

密度为零的采样点跳过光照计算；`viewT` 足够低时终止积分。

## 光照

每个有密度的采样点沿太阳方向做短距离光照步进，采到云顶或 `LightMarchDistance` 为止：

```text
source = ScatteringAlbedo * (sunRadiance * (single + multiple) + ambientRadiance)
single = exp(-Extinction * opticalDepth) * HenyeyGreenstein(cos(视向, 日光), Anisotropy)
```

太阳射线在完整可追踪路径上使用二次分层：靠近着色点的间隔小，越远的间隔越大，
每段按真实长度累计光学深度。默认追踪上限 6 km，仍保持 6 次光照采样；原来的 400 m
短段不能表示厚云上方的遮挡。低太阳高度或更厚的云层仍可能被该上限截断。

单散射相函数混合 90% 配置的 HG 前向瓣与 10% 的 HG(-0.2) 后向瓣，避免只剩一个强烈
朝向太阳的亮点。`multiple` 为两段经验多散射近似：消光倍率为 0.2、0.04，初始能量为 0.5，
下一段乘以 `0.5 * ScatteringAlbedo`，最后按 `1/pi` 归一化。较低消光近似光在云内重新分布后
仍可照亮内部，同时保留太阳方向光学深度带来的遮挡层次；它不是能量守恒的完整多次散射求解。

太阳取场景中最亮的**方向光**（`Light.type == 0`），方向取传播方向的反向；没有方向光时
太阳辐射为零。直射项再乘 `RadianceScale`：单次散射加两段多散射仍低估真实云散射的能量，
而场景的光度学光强远低于它的绘制天空，`1.0` 会让云比它所在的背景更暗；这个系数就是常规的
云亮度控制。

环境项取 `AmbientScale * 场景天空颜色 * lerp(0.45, 1, h)`，其中 h 是归一化世界高度。
这是上部接收更多天空光、底部更少的近似，没有额外追踪天空方向。天空颜色与渲染器清除用的背景是同一个值
（`scene::Scene::set_sky()`，主场景按太阳高度在昼间蓝/暖色/夜间黑之间插值），所以云和背景
永远同步变化，不会出现“背景换色而云没换”的不协调。没有设置天空的场景使用默认日间蓝。

所有计算在线性 HDR 空间完成；`CloudResult.a` 是透射率而不是不透明度。

## 合成与顺序

```
场景不透明几何 + 深度
  → HDR 光照（hdr.color）
  → Cloud ray marching（cloud.result）
  → Cloud composite（hdr.cloud）
  → Bloom（可选）
  → Tone mapping
```

合成 pass 与步进 pass 分开：`cloud.result` 保存真正的辐亮度与透射率，调试视图和后续
低分辨率路径都直接读它；合成先为未被几何覆盖的像素计算天空与太阳，再做
`radiance + transmittance * background`，读写同分辨率
像素，不需要采样器。合成节点产出新的 HDR 资源名，并把 `pass_data.hdr_image` 指向它，
Bloom 与色调映射据此读合成结果。该指向每帧由 `ensure_attachments` 重设，否则复用的帧槽
会从上一帧的合成图开始。

`CloudSettings::enabled` 默认为 false：引擎不为关闭云层的场景生成形态体、中间图和 pass。
游戏在 `GameSession` 构造时显式启用，GPU 测试与工具保持单张 HDR 图。

## 参数

| 参数 | 默认值 | 说明 |
|---|---:|---|
| `enabled` | false | 是否注册云层 pass |
| `bottom` / `top` | 1500 / 3500 m | 云层高度区间 |
| `coverage` | 0.45 | 覆盖率，必须 > 0 |
| `density` | 1.0 | 密度倍率 |
| `noise_scale` | 14000 m | 云团特征尺度（形态噪声瓦片的世界大小） |
| `detail_scale` / `detail_strength` | 6 / 0.35 | 细节采样倍率与边缘侵蚀强度 |
| `wind_velocity` | (5, 0, 2) m/s | 云层移动速度 |
| `extinction` | 0.02 /m | 单位密度消光 |
| `scattering_albedo` | 0.9 | 散射反照率 |
| `anisotropy` | 0.55 | Henyey-Greenstein 各向异性 |
| `view_steps` / `light_steps` | 48 / 6 | 视线与太阳方向采样数 |
| `light_march_distance` | 6000 m | 太阳方向累计追踪距离上限 |
| `max_trace_distance` | 40 km | 视线积分距离上限 |
| `ambient_scale` | 0.45 | 环境光占场景天空颜色的比例 |
| `radiance_scale` | 4.0 | 太阳直射项增益（1.0 为与场景灯光一致） |
| `debug` | Off | 关闭 / 密度 / 太阳透射率 / 视线透射率 / 步数 |

参数在 `Renderer::add_passes` 注册时校验一次：区间必须有厚度、覆盖率必须大于零、步数必须
在 1 以上，非法值以 `lc1::Error` 报错而不是除以零或死循环。游戏调试面板直接编辑同一份
`CloudSettings`，并把编辑结果夹到合法范围，尤其是保证 `top > bottom`。

调试视图输出到 `cloud.result` 并令 alpha 为 0，因此合成后看到的就是调试值本身：密度视图
显示该射线**最大**密度（层底首个命中永远接近零，没有诊断价值），太阳透射率与视线透射率
为灰度，步数视图显示实际步进数与总步数之比。

## 性能

GPU 测试使用固定地面视点、1244×1516、无风场景，在三十对预热帧后交错测量十五对无云/有云帧，
报告整帧 GPU 时间的中位数及有云范围。计时循环不读回图像，避免 CPU 逐像素检查之间的空闲期
影响显卡频率；截图和数值检查仍单独读回。Debug shader 为 `-O0` 且验证开启，结果不能当作
Release 性能预算。每个有密度的视线点仍做 6 次太阳方向采样，每次密度求值保持两次纹理读取；
更长的光照路径不增加步数，实际成本仍取决于云覆盖、提前终止和 GPU 状态。

后续阶段与已知限制：

- 半分辨率计算与深度感知上采样。
- 固定步数在接近地平线的长路径上仍有走样与斑驳；当前的采样偏移和细节振幅滤波只能缓解，
  自适应步进与时间重投影尚未实现。
- 只做单次散射加近似多散射，不含完整多次散射能量守恒。
- 云层内部视角、地面云影、天气分布图。

## 验证

```sh
ctest --test-dir build/linux-x86_64/Debug --output-on-failure --no-tests=error
cmake --build --preset linux-x86_64-debug --target lc1_cloud_tests -j 4
cd build/linux-x86_64/Debug && . ./generators/conanrun.sh && ./lc1_cloud_tests
```

`lc1_cloud_noise_tests`（CPU）校验形态体的尺寸、确定性、直方图、范围与**环绕连续性**，并校验独立细节体的确定性、对比度和三个轴的接缝：
最后一个体素到第一个体素的落差必须与体数据内部相邻体素同量级，否则重复寻址会露出网格。

`lc1_cloud_tests`（GPU）在 1× 与 4× MSAA 下校验：云层改变天空像素、地面像素因深度限制
保持不变、覆盖率增大时光学厚度上升、零密度不改变前景几何、各调试视图有内容、
关闭 pass 后恢复无云画面，以及非法参数在注册期被拒绝。测试另外输出
`artifacts/clouds.ppm`、`artifacts/clouds-clear.ppm`、`artifacts/clouds-density.ppm` 供人工查看。
另有固定无风、960×540 的 `artifacts/clouds-ground.ppm` 与 `artifacts/clouds-underside.ppm`，
分别从地面和云下仰视，检查底部轮廓；`artifacts/clouds-sunlit.ppm` 从反方向观察受光面。
这些画面使用与游戏相同的曝光 2，并给 UNORM 输出显式编码 sRGB；HDR 数值检查不受影响。
GPU 测试同时要求细节侵蚀能把薄边缘切成空隙、长太阳路径能包含更多云内遮挡，
以及没有太阳和天空光时多散射不能凭空发光。

编译、CPU 测试、GPU 数值与实窗观感分别验收；本地结果不构成远端 CI 或性能预算的保证。

## 可见太阳

体积云通路同时绘制角直径 0.53° 的太阳圆盘，与云照明使用同一方向光，随主场景日照时间移动。
圆盘仅出现在没有几何覆盖的天空像素，按云层累计透射率衰减，在地平线下或无日光时消失；
边缘使用角距离的屏幕导数抗锯齿；本体的线性 HDR 辐亮度为方向光辐亮度的 800 倍。
本体周围叠加紧凑亮晕与更宽的柔和散射尾部，以角距离定义，随视场角保持稳定；约 8° 到 14° 之间
淡出。太阳高度越低，用近似光程增加蓝光吸收，圆盘与亮晕一起转暖。这是太阳附近的艺术化散射近似，
并非完整的大气散射解算。圆盘和亮晕共用几何遮挡与云层透射率，随后参与 Bloom 和色调映射。
天空合成读取同一组相机／太阳参数，背景从天顶偏蓝过渡到地平线亮雾；低太阳高度在向阳地平线
加入暖色散射。只替换没有几何覆盖的背景，再计算 `云辐亮度 + 云透射率 × 天空`，保留遮挡关系。
共享的 uniform 定义与逐列坐标重建放在 `shaders/cloud-data.slang`，避免云步进和天空合成各自维护布局。
云诊断视图保留原背景，不绘制圆盘或天空过渡；关闭体积云通路也会关闭这层天空效果。

`artifacts/solar-day-clear.ppm`、`solar-day-clouds.ppm` 与 `solar-sunset.ppm` 使用正常 55° 视场角、
960×540、曝光 2、Bloom 0.12 与 sRGB 输出，供检查正常视角的太阳／天空关系。
GPU 数值检查额外覆盖圆盘外的亮晕与没有方向光时不产生太阳。
