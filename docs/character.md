# Character：角色状态、模型与控制

当前角色支持站立和沿地面移动，表现层的身体/头部节点已有待机动画。主角、士兵和 NPC 都可以使用
`lc1::Character`；由玩家还是 AI 控制，不改变角色自身的类型。

角色属于游戏层，声明在 `include/lc1/game/character/character.hpp`，状态与动作请求处理在 `src/game/character/character.cpp`，
运动求解由角色拥有的 `CharacterMotion` 组件负责，见[角色物理](physics.md)，
与 `Continent` 和角色控制器一样使用 `lc1` 命名空间。角色持有唯一的 `scene::Transform`，外观资源和绘制由 `WorldPresentation` 负责，
场景层不负责角色的出生、行走或碰撞规则。

## 数据归属

| 数据 | 所属对象 |
|---|---|
| 世界位置与身体朝向 | `Character` 内部的 `transform_`，只保存一份 |
| 相对身体的观察角度 | `Character` |
| 身体宽高、眼睛高度、行走和奔跑速度 | 每个角色的 `CharacterBody` |
| 模型原点、单位、坐标轴的校正 | `ModelDefinition` 的根节点变换，随实例姿态参与绘制 |
| CPU 顶点和索引 | `Model` |
| GPU 顶点、索引和 BLAS | `WorldPresentation` 拥有的 `GpuModel`；NPC 共用占位资源，玩家和武器各自加载 |
| 绘制资源引用 | `WorldPresentation` 生成的 `DrawItem`，借用资源 |
| 玩家输入和控制决策 | `PlayerController` 解释输入并产生 `CharacterIntent`；角色不包含 `GameInputRouter`、`Window` 或相机 |
| 动作请求、期望速度与朝向 | `Character::update(delta, continent, intent)` 及其 gameplay 规则 |
| 竖直速度、接地与碰撞求解 | 角色独占的 `CharacterMotion`，借用角色位置完成本次计算 |
| AI 巡逻目标与等待 | `AiCharacterController` 的巡逻模式，只通过 `CharacterIntent` 影响角色 |
| 队员跟随/等待 | `AiCharacterController` 的 `CompanionOrder` 模式，由队伍命令安装和替换 |
| 当前观察画面的相机 | 应用/场景，按需要读取角色眼睛位置和观察方向 |

角色世界坐标采用右手系、Y 向上，位置约定为脚底。身体 yaw 为绕 +Y 的右手旋转角，
单位为度，零角度朝向 −Z。观察 yaw 相对身体，正数向左；观察 pitch 正数向上。
当前分别限制在 ±85°、±89°，防止视线基向量退化。

`heading()` 是不包含俯仰的观察方向，`body_heading()` 是身体朝向，
`look_direction()` 是包含俯仰的完整观察方向。它们在抬头或向侧面观察时有意不同。

## 身份与队伍关系

`Character::config()` 只返回只读配置，外部不能改写角色编号、所属队伍或装备槽映射。
角色和队伍禁止复制、移动；注册表、队伍、对话和表现中的借用因此保持对象地址稳定。
独立的 `CharacterMotion` 仍可移动，它不保存角色位置指针。

`Party::restore_roster()` 是一次性装配入口，完整校验后提交名单，不能用于覆盖运行中的队伍。
`recruit()` 同时更新来源、目标和角色所属队伍，先分配控制器及目标成员空间再修改关系，
失败不留下已转队却未安装跟随控制器的状态。
`change_leader()` 只允许提升本队普通成员或清空队长；原队长转为普通成员，俘虏不能提升。
新队长的伴随命令会清除，清空队长时清除成员伴随命令，避免保存无法恢复的无队长命令。

## 构造和移动

角色构造只需要大陆、初始世界变换、配置和可选身体参数：

```cpp
lc1::Character character{
    continent,
    {.position = continent.spawn()},
    lc1::CharacterConfig{},
    lc1::CharacterBody{}};

character.set_body_yaw(90.0F);
character.set_look_angles(0.0F, 20.0F);
character.move(continent, {0.0F, 0.0F, -0.5F});
```

构造时校验出生点、身体参数和世界变换。世界变换保持直立、单位缩放；
模型尺寸通过资源校正处理，实际碰撞尺寸通过 `CharacterBody` 设置。
`WorldPresentation` 读取 `character.transform().model_matrix()` 生成绘制项；角色不提供绘制接口。
默认身体参数同时用于当前世界角色和占位网格；未来支持不同体型时，表现资源也需要匹配相应体型。

`move(continent, displacement)` 接受以米为单位的 `vec3` 世界位移，当前要求 y 为零。
它借用大陆的碰撞查询，让身体沿障碍滑动，并把结果写回同一份世界位置。

`walk(delta_seconds, continent, direction, running)` 根据角色速度计算位移。方向投影到
XZ 平面，长度超过 1 时归一化，保留较小输入的模拟量强度，使用完整的非负有限时间计算位移。第一人称控制器使用 `heading()`，因此抬头不会减慢行走。

`update(delta_seconds, continent, intent)` 是控制器意图进入角色状态的唯一路径，并把
“应用 intent”与“推进物理”合并为一次状态更新。它先校验时间，私有 `apply_intent()`
校验完整 intent，再应用身体 yaw/朝向、观察目标和跳跃请求，不执行位移。
随后角色根据移动方向、奔跑和转身策略计算水平期望速度，交给 `CharacterMotion::update()`。
物理组件不接收 `CharacterIntent`，不读取玩家／AI 控制器，不决定行走速度或身体朝向；
它只处理物理运动并更新角色唯一的 `transform_.position`。状态归属、配置、时间细分与碰撞规则
以[角色物理](physics.md)为准。
`walk()` 和 `move()` 保留为角色自身的低层移动 API，供测试和特殊的世界级调整使用。

`move()` 不自动转身；`walk()` 按 `face_movement` 决定是否转身。第一人称可以侧移、后退；斜视角控制器通过 `face_movement`
请求身体朝向移动方向，由角色决定是否执行。

## 跳跃与接地

控制器通过 `CharacterIntent::jump` 发出一次跳跃请求，由 `Character::update()` 内部的
`apply_intent()` 转成 `request_jump()`，再由 `CharacterMotion` 根据接地状态更新竖直速度、高度和接地状态。
应用层使用按下事件而不是持续按住事件，避免落地后自动连跳；第一人称和斜视角控制器均支持
该请求。当前 Space 也绑定了相机回中，斜视角下会同时触发原有回中行为，按键分离留待输入设计调整。

“只有着地才能起跳”这条规则在角色侧：控制器的 `jump` 只是期望，空中请求会被丢弃。
日后加入眩晕、攻击状态或跳跃缓冲时，也应在 `Character::update()`/`apply_intent()` 一侧增加判断，
而不是让每个控制器各自复制。

地面高度来自 `WorldCollision::support_below()`，不再固定为 y=0。构造时若脚下有支撑面，
角色会贴到该表面；也可以直接在空中出生，第一次 `update(delta_seconds, continent, intent)`
会应用重力并落地。`update` 在着地时跟随缓坡和小台阶，离开边缘后进入下落，并在下落
过程中查询支撑面落地。默认跳跃高度为 0.5 米（脚底相对起跳支撑面的上升高度），使用重力计算起跳速度；
因此不能直接跳上 0.8 米高的展台，也不包含攀爬或翻越动作。空中请求被丢弃，不支持二段跳或跳跃缓冲；
零时间更新不消耗请求。

`support_below()` 会忽略脚下高于当前高度的表面，因此桥面、平台和地面可以在同一
XZ 坐标上重叠。`ContinentBlock::walkable_top` 标记可站立的体块顶面；普通墙体和建筑
外壳不参与支撑查询。着地行走遇到低于 `step_height` 的可行走台面时，物理组件
会先尝试普通水平扫掠，再尝试抬高脚底扫掠，确认能站到更高支撑面后才上台阶；不会借机
跨过不可站立的矮障碍。空中移动时还会检查地形是否已经从脚下“追上”身体，避免在陡坡上
加速跳跃时穿入高度场后一直下落。向上运动使用头顶扫掠检测实体底面，撞顶后停止上升并继续计算
剩余时间的下落。移动平台、坡面滑落和任意三角网格地形尚未实现。

## 模型校正

模型原点、单位和坐标轴校正由 `ModelDefinition` 的根节点 `NodeTransform` 表达。
`Model` 的文件加载和单网格构造不接受 correction 参数，也不修改 CPU 顶点与索引；
需要校正时，使用多网格构造入口提供一个校正根节点，让几何节点成为它的子节点。
节点使用平移、四元数旋转和缩放，组合约定见[资源管理](resources.md#模型资源与实例)。

每次绘制的矩阵包含角色世界变换和节点累计变换。法线使用线性变换的逆转置，
切线使用线性变换；镜像保留在矩阵行列式中，由渲染器选择 front face，CPU 索引不翻转。
共享模型实例各自保存姿态，修改一个实例的节点不会改变共享几何或其他实例。
节点及最终绘制变换必须有限且可逆；网格构造仍独立检查三角形完整性、索引和顶点有效性。

## 控制与视角

控制流程是 `PlayerController / AiCharacterController -> CharacterIntent -> Character`。
控制器只表达“想做什么”，不直接移动角色、扣状态或决定最终速度。

`PlayerView` 拥有视角状态、记忆观察角和相机；它从 `GameInputRouter` 读取 `GameAction`，
在斜视角下先更新相机基，再交给 `PlayerController`。`PlayerController` 负责：

- 第一人称：鼠标增量转绝对 yaw/pitch，移动方向跟随观察水平方向，身体随视角转向。
- 第三人称：WASD 按相机水平方向解释，角色朝移动方向转身。
- 把 Sprint、Jump、Interact 的按下/按住状态填入 intent。

相机锁定和回中留在 `PlayerView`，因为它们改变相机而不是角色；
`update_pointer()` 解析滚轮及逻辑屏幕边缘，`MapCameraController` 只提供锁定、缩放、回中、
平移和跟随算法，不接收输入打包结构。

M 切换只改变视角状态；恢复身体/观察角的 intent 在接下来的世界更新中执行。
`GameWorld::update()` 使用同一份完整时间先取得控制器 intent：玩家由 `PlayerController`
产生，NPC 由其 `CharacterController::update()` 产生；随后统一调用
`Character::update(elapsed, continent, intent)`。控制器暂停时只是传入空 intent，物理仍然推进。
`update_camera()` 随后读取最终姿态，因此跳跃和相机跟随在同一帧生效。
两种视角使用同一个角色，保留斜视角缩放与锁定状态。

环境对话请求也由角色规则决定：`Character::accepts_ambient_requests(intent)` 要求角色着地、
没有移动、没有冲刺且本帧没有跳跃请求；会话另外用 `player_input_enabled` 表示 UI/焦点
是否把输入交给玩家。两者分开，避免把输入门控写进角色规则。
对话中的 NPC 不再由 `Character` 直接转向：`GameWorld` 把“面向对话伙伴”写入
`CharacterIntent::body_direction`，再交给统一的 `Character::update()`。

会话按菜单状态、Gameplay/UI context、窗口焦点和 ImGui capture 决定是否提供控制输入。
鼠标捕获由 Application 执行，菜单打开时释放，不用于反推是否启用鼠标观察。

## 动画与后续蒙皮边界

Scene 拥有每个角色的 `ModelInstance`，借用共享 `ModelDefinition` 中的节点和动画片段；
`WorldPresentation` 保存角色与 Scene 实例的关联、选择动画并提交世界变换，Scene 负责推进动画、挂点查询与绘制展开。
NPC 循环播放头部左右转动的 `Idle`，玩家默认保持静止，按 F2 播放一次 `Nod`（再次按下从头播放）：
前 1.2 秒向下点头两次（25°），随后保持直立，片段总长 3 秒且不循环。均为刚性节点动画；角色世界姿态仍只属于 `Character`。
这不是骨骼蒙皮，尚未实现行走/攻击动画选择、动画混合、root motion、死亡或布娃娃。
占位模型的 `Eyes` 是 `Head` 下的无网格挂点，默认世界位置等于脚底加身体配置的眼睛高度。
第一人称相机读取本帧动画后的挂点世界变换；位置和观察方向都跟随挂点，鼠标观察角在身体局部空间叠加到挂点旋转。
斜视角忽略眼睛挂点。`Character::eye_position()` 仍是玩法层的配置高度查询，未被动画改写；
不提供挂点的 `PlayerView` 调用方继续使用这一回退值。
占位几何由 `make_character_model(CharacterBody)` 构造，默认姿态尺寸与身体配置一致。

## 本地模型试用

玩家当前使用 `runtime/assets/lc-res/Modular Village/Prop_Barrel_1.obj`，NPC 保留方块占位模型。
木桶的静态几何通过 OBJ 加载器读取，校正节点把底部放到脚底、水平居中，分别调整高度与水平尺寸，
使默认姿态匹配玩家 `CharacterBody`。不修改导入顶点；`Eyes` 仍按身体配置以米表示。
木桶挂在原有 `Head` 下，F2 会带动整个桶身摇动并保留相机挂点动画。
木桶使用统一棕色材质，未导入原素材的 MTL 材质分组。

武器使用本地 `weapon_set_kingdom.blend` 中 pfunked 的 `KingdomLongsword`，
经 `scripts/export-kingdom-longsword.py` 提取为同目录的 `KingdomLongsword.obj`。
导出仅包含静态网格；运行时通过节点执行 Z-up 校正、单位缩放和握柄偏移，使用统一金属材质。
Scene 独立拥有武器实例，`WorldPresentation` 拥有其 GPU 资源并提交玩家的身体世界矩阵；
武器不改变 Character 的位置、碰撞或眼睛挂点。
鼠标左键按下播放一次 0.55 秒的 `Swing`，包含蓄力、向前挥动和回位，再次按下从头播放。
当前为可操作的模型展示，没有命中、伤害、连击或装备数据绑定。

上述素材仍属于不纳入 Git 的外部 `runtime/assets/`，缺失时加载明确失败；
运行前需准备村庄素材，并执行 `python3 scripts/export-kingdom-longsword.py` 生成武器 OBJ。
CPU 测试使用仓库内的独立几何夹具验证校正、挂点、武器跟随和单次播放，不依赖外部素材。
`./lc1_pbr_tests --player-models` 使用上述真实素材进行 GPU 回读，检查挥动后的画面变化，
并在构建目录的 `artifacts/player-weapon-{rest,swing}.ppm` 保存两帧。

后续蒙皮继续由模型实例拥有当前骨骼姿态和播放进度，借用共享骨骼和片段，通过网格索引关联 GPU 资源。
眼睛位置和观察方向已跟随头部挂点；后续平滑策略仍由视角控制决定。
骨骼、逆绑定矩阵和动画轨道必须采用一致的坐标校正，不能只套用当前静态顶点的处理步骤。
模型播放和资源生命周期详见[模型资源与实例](resources.md#模型资源与实例)。

## 验证

`lc1_character_tests` 覆盖角色初始状态、身体与观察的区别、碰撞滑动、实例世界矩阵、
配置和无效输入、第一人称倒退/侧移、切换控制器、`CharacterIntent` 的冲刺速度/跳跃限制/
环境请求规则、AI 决策不改角色、跟随控制器的停止距离，以及节点校正矩阵、共享几何不变性和镜像行列式。
原有 `lc1_continent_tests` 已使用 `Character`，继续检查速度、世界边界和城市穿行。
`lc1_player_view_tests` 和 `lc1_game_world_tests` 覆盖玩家输入门控、intent 到物理的时序。

## 背包负重

`Resource::weight` 定义单件重量，使用 `uint32_t` 整数克；相同资源 ID 的定义必须一致。
当前初始值为钱币 10 g、木材 1000 g、生铁 2000 g、铁盔 2500 g，属于可调整的玩法参数。
`Bag` 构造时显式指定上限，角色配置为 10000 g（10 kg），通过 `total_weight()` 和 `max_weight()` 查询。
添加按数量乘单件重量检查负重，同时检查数量溢出；乘法使用 64 位中间值。
移除按实际存储的资源定义扣除重量。制作在候选背包先消耗再添加，成功才提交数量和重量，
失败保持原状，因此满载时也可以制作减轻负重的配方。配方输入／输出重量以克记录。
现有铁盔配方原料总重 12.2 kg，超过默认背包上限，默认背包无法凑齐该配方。
UI 可以换算 kg 显示；列表只读真实背包。

资源定义为普通 `struct Resource { keyid, name, category, weight }`，不使用资源子类或
`ResourceType` 枚举。内置定义集中在 `game/resources/catalog.hpp`，新增普通资源只需增加记录。
世界按 `keyid` 保存资源值，背包按 ID 索引数量与只读定义借用；资源定义必须比借用活得久，
入包后不能修改重量。名称由 `Resource` 的 `std::string` 拥有，内置定义使用 `inline const`，配方借用静态定义，世界定义在角色之前创建、之后销毁。
资源分类仍为枚举，配方用对应定义构造；列表显示定义名称，按 ID 排序。未引入 YAML 资源加载或新存档格式。
