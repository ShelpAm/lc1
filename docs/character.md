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
| 玩家输入和控制决策 | `PlayerCharacterController` 解释输入并产生 `CharacterIntent`；角色不包含 `GameInputRouter`、`Window` 或相机 |
| 动作请求、期望速度与朝向 | `Character::update(delta, continent, intent)` 及其 gameplay 规则 |
| 竖直速度、接地与碰撞求解 | 角色独占的 `CharacterMotion`，借用角色位置完成本次计算 |
| AI 巡逻目标与等待 | `AiCharacterController` 的巡逻模式，只通过 `CharacterIntent` 影响角色 |
| 队员跟随/等待 | `AiCharacterController` 的 `CompanionOrder` 模式，由队伍校验权限并下达命令 |
| 当前观察画面的相机 | 应用/场景，按需要读取角色眼睛位置和观察方向 |

角色世界坐标采用右手系、Y 向上，位置约定为脚底。身体 yaw 为绕 +Y 的右手旋转角，
单位为度，零角度朝向 −Z。观察 yaw 相对身体，正数向左；观察 pitch 正数向上。
当前分别限制在 ±85°、±89°，防止视线基向量退化。

`heading()` 是不包含俯仰的观察方向，`body_heading()` 是身体朝向，
`look_direction()` 是包含俯仰的完整观察方向。它们在抬头或向侧面观察时有意不同。

## 身份与队伍关系

`Character::config()` 只返回只读配置，外部不能改写角色编号、所属队伍或装备槽映射。
角色和队伍禁止复制、移动；注册表、队伍和对话中的借用因此保持对象地址稳定。
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

巡逻控制器构造时接收一组可选的 `action_targets` 借用，用于现有近距离挥砍演示；所有目标必须比控制器长寿。世界装配时传入全部玩家角色，不再指定唯一玩家。
巡逻 AI 在任一演示目标距离不超过 3 米时持续请求挥砍，由 Character 决定执行时机；等待或抵达路点也执行该判断。
Follow/Wait 通过现有 AI 的 `set_order()` 切换，跟随时从队伍读取当前队长。
控制器用 `State` 枚举区分待机、巡逻、跟随、等待和对话，通过 `switch` 执行对应行为；
构造时绑定只读 `DialogSystem`，每个模拟 tick 自行查询该角色的活动对话，进入对话或恢复原状态；
对话期间暂停原行为和攻击，巡逻等待计时也暂停。只在本次更新内借用对话和伙伴。
对话系统必须比控制器长寿。世界先安装 NPC 的 AI，再恢复队员命令。
`Party` 只校验权限并向现有 AI 下令，不创建或清空控制器，也不依赖对话系统。
对话中下令更新恢复状态，结束后执行最新命令；清除命令进入待机，不恢复旧巡逻。

控制流程是 `PlayerCharacterController / AiCharacterController -> CharacterIntent -> Character`。
控制器只表达“想做什么”，不直接移动角色、扣状态或决定最终速度。

`PlayerView` 在主线程拥有视角状态、记忆观察角和相机，读取 `CharacterPose` 值副本。
它准备 `PlayerCharacterInput`，主线程通过 `PlayerCharacterController::read_input()`
把 `GameInputRouter` 的动作复制为 `PlayerControls`。输入消息不借用路由器、窗口或相机。
模拟线程把消息写入已绑定的输入值，玩家控制器在 `Character::update()` 中读取它：

- 第一人称：鼠标增量转绝对 yaw/pitch，移动方向跟随观察水平方向，身体随视角转向。
- 第三人称：WASD 按相机水平方向解释，角色朝移动方向转身。
- Sprint 为持续状态，Jump、SwingWeapon 和 Nod 为一次性请求；Interact 和对话选项由世界处理。

相机锁定、回中、缩放和边缘平移留在主线程的 `PlayerView`/`MapCameraController`。
M 切换只改变视角状态，身体/观察角由下一个模拟 tick 的 intent 更新；
切换帧丢弃观察位移。主线程从已完成 tick 的快照跟随角色，不直接修改模拟中的玩家。
`GameWorld::update()` 对所有角色统一调用 `Character::update(elapsed, continent)`；
角色通过 `CharacterController::update()` 取得 intent，执行角色规则和物理。
没有控制器时使用空 intent，物理仍然推进；测试通过绑定控制器提供动作。
固定 tick、输入合并和线程生命周期以[代码边界](code-layout.md#固定步长模拟与线程交换)为准。

环境对话请求也由角色规则决定：`Character::accepts_ambient_requests()` 要求角色着地、
本帧没有移动、冲刺或跳跃请求；会话另外用每个玩家的 `PlayerControls::view.keyboard_enabled` 表示 UI/焦点
是否把输入交给玩家。两者分开，避免把输入门控写进角色规则。
AI 从绑定的对话系统查询活动对话，按内部状态暂停巡逻、跟随与攻击，
生成面向伙伴的 intent。没有巡逻或队员命令的 NPC 也使用静止 AI。
`CharacterController` 只保留通用的 intent 更新接口，AI 的巡逻状态、队员命令和上下文不进入该基类。

会话按菜单状态、Gameplay/UI context、窗口焦点和 ImGui capture 决定是否提供控制输入。
鼠标捕获由 Application 执行，菜单打开时释放，不用于反推是否启用鼠标观察。

## 动画与后续蒙皮边界

Scene 拥有每个角色的 `ModelInstance`，借用共享 `ModelDefinition` 中的节点和动画片段；
`Character` 不借用 Scene；接受挥砍请求后增加 `attack_sequence`，由表现层读取快照触发武器动画。
`WorldPresentation` 装配模型、设置初始待机动画并提交世界变换，Scene 负责推进动画、挂点查询与绘制展开。
NPC 循环播放头部左右转动的 `Idle`，玩家默认保持静止，按 F2 经角色 intent 增加 `nod_sequence`，所有观察者播放一次 `Nod`（再次按下从头播放）：
前 1.2 秒向下点头两次（25°），随后保持直立，片段总长 3 秒且不循环。均为刚性节点动画；角色世界姿态仍只属于 `Character`。
这不是骨骼蒙皮，尚未实现行走/攻击动画选择、动画混合、root motion、死亡或布娃娃。
占位模型的 `Eyes` 是 `Head` 下的无网格挂点，默认世界位置等于脚底加身体配置的眼睛高度。
第一人称相机读取本帧动画后的挂点世界变换；位置和观察方向都跟随挂点，鼠标观察角在身体局部空间叠加到挂点旋转。
斜视角忽略眼睛挂点。`Character::eye_position()` 仍是玩法层的配置高度查询，未被动画改写；
不提供挂点的 `PlayerView` 调用方继续使用这一回退值。
占位几何由 `make_character_model(CharacterBody)` 构造，默认姿态尺寸与身体配置一致。

## 本地模型试用

玩家当前使用 `runtime/assets/lc-res/Modular Village/Prop_Barrel_1.obj`，NPC 保留方块占位模型。
木桶的静态几何通过通用模型加载器读取，`characters.yaml` 中的节点局部变换把底部放到脚底、
水平居中并设定尺寸；运行时不再自动按包围盒拟合。`Eyes` 和持握挂点也在角色 YAML 中以米配置。
模型外观变换不修改玩法的 `CharacterBody` 或碰撞体。
木桶挂在原有 `Head` 下，F2 会带动整个桶身摇动并保留相机挂点动画。
木桶使用统一棕色材质，未导入原素材的 MTL 材质分组。

武器使用本地 `weapon_set_kingdom.blend` 中 pfunked 的 `KingdomLongsword`，
经 `scripts/export-kingdom-longsword.py` 提取为同目录的 `KingdomLongsword.obj`。
导出仅包含静态网格；`weapons.yaml` 中的模型节点定义局部旋转、缩放和握柄对齐，材质与挥动动画也由 YAML 指定。
Scene 独立拥有武器实例，表现层按角色 ID 保存关联；`Weapon` 不保存实例借用。没有装备或没有模型配置时不创建武器实例。
玩家和 NPC 使用同一套装配与更新逻辑，远程武器尚未接入持握显示。
`WorldPresentation` 拥有 GPU 资源，并在角色动画和世界矩阵更新后，将各自角色模型的持握挂点世界矩阵提交给武器；
武器不改变 Character 的位置、碰撞或眼睛挂点。
玩家鼠标左键按下生成 `CharacterIntent::swing_weapon`，AI 控制器也可填写同一请求。
`Character::update()` 在冷却允许且装备近战武器时增加攻击序号，无需加载模型即可执行；
玩家和 NPC 使用相同规则。表现层检测快照中的序号变化后播放配置的单次动作动画，
没有显示模型或动作片段时不播放。Scene 每个渲染帧统一推进动画时间并采样姿态。
默认 `Swing` 长 0.55 秒，包含蓄力、向前挥动和回位；Character 按 2 秒冷却接受新请求。
冷却随模拟时间推进，无请求时也计时，冷却期间的连续请求不会重置动画。
角色和武器各自的 YAML 选择外观；尚无命中、伤害或连击。
字段、默认值和旧存档兼容行为见[模型配置](save-data.md#角色与武器的-model)。
角色与武器没有 `model()` 接口；模型查询属于 `WorldPresentation`，Scene 所有权与借用生命周期见[资源文档](resources.md#持久渲染场景)。

上述素材仍属于不纳入 Git 的外部 `runtime/assets/`，缺失时加载明确失败；
运行前需准备村庄素材，并执行 `python3 scripts/export-kingdom-longsword.py` 生成武器 OBJ。
CPU 测试使用仓库内的 YAML 和独立几何夹具验证局部变换、挂点、武器跟随、单次播放、
法线贴图所需切线生成、非法配置拒绝及存档往返，不依赖外部素材。
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
移除按调用方传入的定义扣除重量。制作在候选背包先消耗再添加，成功才提交数量和重量，
失败保持原状，因此满载时也可以制作减轻负重的配方。配方输入／输出重量以克记录。
现有铁盔配方原料总重 12.2 kg，超过默认背包上限，默认背包无法凑齐该配方。
UI 可以换算 kg 显示；列表只读真实背包，按编号从内置目录取名称和重量。

资源定义为普通 `struct Resource { keyid, name, category, weight }`，不使用资源子类或
`ResourceType` 枚举。内置定义集中在 `game/resources/catalog.hpp`，新增普通资源只需增加记录。
`Bag` 的条目只保存 `ResourceStack { keyid, quantity }`，不保存定义借用：`add` 和 `remove`
按调用方传入的只读定义计算重量，所以同一编号的定义必须一致，背包因此不附带任何生命周期要求，
条目可以直接比较、复制和写入存档。`catalog.hpp` 的 `find_resource(keyid)` 是唯一的编号到定义
解析入口，编号按升序排列并由 `static_assert` 保证唯一，解析使用二分查找；调用方在入口校验编号
存在，之后可以假定解析成功。名称由 `Resource` 的 `std::string` 拥有，内置定义使用 `inline const`，
配方仍借用这些进程期常量。
资源分类仍为枚举，配方用对应定义构造；列表显示定义名称，按 ID 排序。未引入 YAML 资源加载或新存档格式。
