# 体积云

层式体积云：天空由一张可平铺的三维形态噪声和一个水平高度区间定义，逐像素沿视线积分
密度、消光与散射，再在色调映射之前合成进 HDR 画面。云层不是实体、不进入 ECS、不参与
物理或存档；渲染模块只读取相机、方向光和场景深度。

当前范围是地面观察：相机位于云层下方，不支持云层内部的近距离观察、云体独立管理、
降雨/雷暴、地面云影或天气模拟。

## 模块与资源

| 组件 | 位置 | 职责 |
|---|---|---|
| `CloudSettings` | `include/lc1/vk/render/volumetric-clouds.hpp` | 高度、覆盖率、密度、风、光学与步进参数 |
| `make_cloud_shape_noise()` | `src/cloud-noise.cpp` | 生成可平铺的 Perlin-Worley R8 体数据，纯 CPU、确定性 |
| `VolumetricCloudRenderer` | `src/vk/render/volumetric-clouds.cpp` | 持有形态体纹理与两个 shader，注册并录制两个 pass |
| `shaders/clouds.slang` | 视线步进 + 光照步进 | 输出 `float4(辐亮度, 透射率)` |
| `shaders/clouds-composite.slang` | 合成 | 输出 `cloud.rgb + cloud.a * scene.rgb` |

资源由帧槽持有，只有形态体纹理属于 `VolumetricCloudRenderer`：

| 资源 | 格式 | 用途 |
|---|---|---|
| `shape_noise_` | `R8_UNORM` 128³ | 云层形态与覆盖阈值，重复寻址采样 |
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

形态噪声在启动后首次启用云层时生成，一次约数百毫秒（Debug 构型），之后常驻显存，不会
逐帧重算。生成算法是确定性的整数哈希，因此同一尺寸每次得到相同字节；测试据此校验。

`sampleCloudDensity(worldPosition, time)` 是视线步进和光照步进共用的唯一定义：

```text
p        = worldPosition - windVelocity * time
h        = (p.y - CloudBottom) / (CloudTop - CloudBottom)
profile  = smoothstep(0, 0.15, h) * (1 - smoothstep(0.6, 1, h))
mass     = ShapeNoise.Sample(p / NoiseScale)                 # 大尺度云团
detail   = ShapeNoise.Sample(p * DetailScale / NoiseScale + 偏移)  # 侵蚀边缘
remapped = saturate((mass - (1 - Coverage)) / max(Coverage, eps))
bodies   = smoothstep(0, 1, remapped) * (1 - DetailStrength * detail)
density  = bodies * profile * Density
```

两层频率用的是同一张体数据，只差采样缩放：低频决定“哪里有一团云”，高频只侵蚀边缘。
只用一层做不出这两件事——细到像云的一层，也细到把整片天空切成一样大的小泡。

- 世界坐标采样，不是相机相对坐标：相机移动不会带动云层，云层形态只由世界位置与时间决定。
  采样距离受最大追踪距离限制，float 精度足够。
- 覆盖率移动阈值而不是缩放任一常数，因此 0 附近密度必为零、1 时保留噪声原始形态。
- 重映射后再做一次 `smoothstep`：把连续的噪声梯度压成**成团 + 干净间隙**，而不是一层
  任何覆盖率下都铺满天空的薄雾。`DetailStrength` 越大，云的边缘越碎，越大团。
- 高度剖面在底面与顶面平滑收敛，中间保持高密度，避免水平硬边。
- 风把采样位置按 `windVelocity * time` 平移，时间来自 `steady_clock` 的挂钟秒数，
  与帧率无关。
- 底噪只有一张：高频细节噪声、多尺度形态、天气分布图属于后续阶段。

## 视线步进

1. 由像素构造 NDC，经 CPU 预先求逆的 view-projection 还原世界射线；相机位置由 CPU 给出。
2. 深度在四个 MSAA 采样点里取最近值；深度仍是清除值（1.0）时视为无几何体，用最大追踪
   距离截断，而不是把远平面距离当作终点的假象。
3. 求交云底与云顶两个高度平面，得到进入/离开距离；`|dir.y|` 退化时直接判定不相交。
4. `t_end = min(层离开距离, 场景距离, MaxTraceDistance)`；不是云层正前方的射线立即跳过。
5. 固定 `ViewSteps` 步，步长由有效区间决定，每步取分段常值近似。
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

`multiple` 是两段多散射近似：多散射后的光近似各向同性、能穿过更长的路径，因此每一段
只计入一半消光、携带上一段一半的能量，并按漫射体的 `1/pi` 归一化。没有它，光学厚的
云会饱和在单散射相函数上而呈深灰色；有了它仍保留自阴影，因为每段都重新测量太阳方向的
光学深度。

太阳取场景中最亮的**方向光**（`Light.type == 0`），方向取传播方向的反向；没有方向光时
太阳辐射为零。直射项再乘 `RadianceScale`：单次散射加两段多散射仍低估真实云散射的能量，
而场景的光度学光强远低于它的绘制天空，`1.0` 会让云比它所在的背景更暗；这个系数就是常规的
云亮度控制。

环境项取 `AmbientScale * 场景天空颜色`。天空颜色与渲染器清除用的背景是同一个值
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
低分辨率路径都直接读它；合成只做 `radiance + transmittance * background`，读写同分辨率
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
| `coverage` | 0.35 | 覆盖率，必须 > 0 |
| `density` | 1.0 | 密度倍率 |
| `noise_scale` | 14000 m | 云团特征尺度（形态噪声瓦片的世界大小） |
| `detail_scale` / `detail_strength` | 8 / 0.40 | 细节采样倍率与最大侵蚀比例 |
| `wind_velocity` | (5, 0, 2) m/s | 云层移动速度 |
| `extinction` | 0.02 /m | 单位密度消光 |
| `scattering_albedo` | 0.9 | 散射反照率 |
| `anisotropy` | 0.55 | Henyey-Greenstein 各向异性 |
| `view_steps` / `light_steps` | 48 / 6 | 视线与太阳方向采样数 |
| `light_march_distance` | 400 m | 单次光照步进长度上限 |
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

实测（RTX 4060 Laptop，Debug 构型即 shader `-O0`，1244×1516，单张地面与天空）：
无云约 0.6 ms，含云约 2.6 ms 每帧。固定 48 步视线 + 6 步光照在光学厚区会因透射率提前
终止，实际平均步数远低于上限。

尚未实现（属于后续阶段，不影响当前正确性）：

- 半分辨率计算与深度感知上采样。
- 固定步数在接近地平线的长路径上会产生明显的走样与斑驳；自适应步进、时间重投影与
  细节噪声都会改善它。
- 只做单次散射加近似多散射，不含完整多次散射能量守恒。
- 云层内部视角、地面云影、天气分布图。

## 验证

```sh
ctest --test-dir build/linux-x86_64/Debug --output-on-failure --no-tests=error
cmake --build --preset linux-x86_64-debug --target lc1_cloud_tests -j 4
cd build/linux-x86_64/Debug && . ./generators/conanrun.sh && ./lc1_cloud_tests
```

`lc1_cloud_noise_tests`（CPU）校验形态体的尺寸、确定性、直方图、范围与**环绕连续性**：
最后一个体素到第一个体素的落差必须与体数据内部相邻体素同量级，否则重复寻址会露出网格。

`lc1_cloud_tests`（GPU）在 1× 与 4× MSAA 下校验：云层改变天空像素、地面像素因深度限制
保持不变、覆盖率增大时光学厚度上升、零密度等价于无云的逐字节结果、各调试视图有内容、
关闭 pass 后恢复无云画面，以及非法参数在注册期被拒绝。测试另外输出
`artifacts/clouds.ppm`、`artifacts/clouds-clear.ppm`、`artifacts/clouds-density.ppm` 供人工查看。

编译、CPU 测试、GPU 数值与实窗观感分别验收；本地结果不构成远端 CI 或性能预算的保证。
