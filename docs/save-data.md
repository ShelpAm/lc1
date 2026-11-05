# 存档与继续游戏

角色、队伍、势力、装备和武器从 `runtime/saves/01/` 下的 YAML 读取。大陆地形、区域、地点和道路
仍由 `Continent` 生成；存档通过 `continent` 的稳定字符串 `id` 和内容 `version` 选择大陆，不重复保存完整地图。文件相对工作目录打开，和 `shaders/` 一致；
CMake 目标 `copy_saves` 每次构建都将种子文件复制进运行树，覆盖已有同名文件，
使源码中的角色和队伍修改在 `make run` 时生效。脚本只复制七类初始数据，
不复制或覆盖 `checkpoint.yaml`；已有进度仍优先加载，因此有进度时不会自动改用新种子。

按 F6 或调试面板里的 Save game 按钮打开命名保存窗口，输入名称后点击 Save；
同名存档显示覆盖提示和 Overwrite save 按钮，保存成功后当前槽位切换到该名称。
按 F9、主菜单 Load Game 或调试面板 Load game 打开存档列表，选择一项 Load 即可读档。
窗口打开时屏蔽玩法输入并释放第一人称光标，世界仍继续模拟。HUD 和窗口显示操作结果，详细错误写入日志。
列表包含完整的七文件种子槽和带 checkpoint 的进度槽，按名称排序，Refresh 重新扫描。
不符合存档命名规则的目录通过普通条件过滤，不以异常控制列表扫描；显式提交非法名称时仍报告错误。
损坏的 checkpoint 仍列出，选择后报告错误，不静默隐藏或回退。
启动时，目录里存在 `checkpoint.yaml` 就优先读取完整进度，否则读取七个初始 YAML。
`LC1_SAVE_ROOT` 可以覆盖启动槽位，默认为 `runtime/saves/01`；游戏内列表和命名保存使用
该槽位的同级目录。例如默认新建 `Trip` 写入 `runtime/saves/Trip/checkpoint.yaml`。
名称为 1–128 字节，不能包含路径分隔符、控制字符、跨平台保留标点，也不能以空格开头或以空格、句点结尾；
不允许通过名称写到上级或嵌套目录。退出不会自动保存。

`Application` 用临时 `Save` 构造自己拥有的 `GameSimulation`，再将模拟借给 `GameSession`；模拟拥有 `GameWorld`，后者在构造或调用 `load(Save const&)` 时消费 Save，
按依赖顺序装配势力、装备、武器、队伍和角色，不保留对 Save 的引用。
Save 只负责 YAML 记录、文件读写及引用校验；GameWorld 负责运行对象和快照。
两者都可以脱离 device、窗口和音频单独构造与测试。`session.yaml` 保存玩家到角色的绑定及 AI 示例；本地观察者由应用配置选择，见[玩家与多人模拟](players.md)。

## 文件与记录

| 文件 | 记录类型 | 引用方 |
|---|---|---|
| `factions.yaml` | `SerializedFaction` | 队伍 |
| `equipments.yaml` | `SerializedEquipment` | 角色 |
| `weapons.yaml` | `SerializedWeapon` | 角色 |
| `parties.yaml` | `SerializedParty` | 角色 |
| `characters.yaml` | `SerializedCharacterConfig` | 队伍、`session.yaml` |
| `session.yaml` | `SerializedSession` | 角色 |
| `continent.yaml` | `ContinentIdentity` | 大陆内容 |

七个文件是初始数据。实际写回使用单个 `checkpoint.yaml`，包含版本号 `version: 5`、
`continent`、`session`、`factions`、`equipments`、`weapons`、`parties`、`characters` 和 `states`，
将角色所属队伍和双方成员列表一起提交，避免多文件写到一半形成矛盾数据。
每类记录按编号排序输出。进度文件损坏时明确报错，不静默回退到初始世界。

世界数据文件是 YAML 序列，每条记录一项；`session.yaml` 是单个 YAML 映射。
字段必须写全：读取使用
`rfl::NoOptionals`，缺少原有玩法字段会解析失败，空列表要显式写成 `[]`。
角色和武器新增的 `model` 及其可选子字段通过 `rfl::DefaultVal` 提供默认值，不放宽原有字段检查；
模型配置是版本 4 引入的兼容扩展，旧 checkpoint 不会从种子文件自动补入新模型配置。

`keyid` 是身份，名称只是显示。同类编号非零、在本文件内唯一；重排文件或改名不影响引用。
编号重复时 `Save` 在读取阶段就明确报错，不会静默保留其中一条。

### 势力 `factions.yaml`

| 字段 | 含义 |
|---|---|
| `keyid` | 势力编号 |
| `name` | 势力名称 |

设计里的“独立势力”是分类而不是势力，因此每个具体势力各自一条记录、各自一个编号。

### 装备 `equipments.yaml`

| 字段 | 含义 |
|---|---|
| `keyid` | 装备编号 |
| `type` | `Head`、`Body`、`Foot` |
| `armor` | 护甲值 |

### 武器 `weapons.yaml`

| 字段 | 含义 |
|---|---|
| `keyid` | 武器编号 |
| `type` | `MeleeWeapon`、`RangedWeapon` |
| `attack` | 攻击力 |
| `range` | 射程，米 |
| `model` | 模型节点、动画、局部变换和材质，见下文 |

### 队伍 `parties.yaml`

| 字段 | 含义 |
|---|---|
| `keyid` | 队伍编号 |
| `faction` | 所属势力编号 |
| `leader` | 队长角色编号；`0` 表示暂时没有队长 |
| `members` | 普通成员角色编号列表，不含队长 |
| `captives` | 俘虏角色编号列表 |

队伍保存的是具体角色的编号，不保存“士兵数量”；每个角色仍然只保存自己那一份数据。
队长单独放在 `leader` 中，不得同时出现在 `members` 或 `captives`。
两个列表各自无重复且互不重叠，列表内角色及队长的 `party` 必须指回本队。
每个角色必须且只能作为所属队伍的队长、普通成员或俘虏出现；不再允许未部署状态，也不会推断或自动补齐成员。
种子角色 3 明确列为队伍 1 的普通成员；已有自定义种子／旧进度若存在未列出的角色，需显式修正其队伍记录。
运行期通过一次性的 `Party::restore_roster()` 整体链接，先校验所属队伍、队长重叠、空指针和重复成员，
成功才提交；失败不提交部分名单，成功后不能再次装配覆盖。运行期变更见[角色](character.md#身份与队伍关系)。

### 角色 `characters.yaml`

| 字段 | 含义 |
|---|---|
| `keyid` | 角色编号，非零且唯一 |
| `name`、`sex`、`age` | 基础信息 |
| `party` | 所属队伍编号 |
| `level` | 等级 |
| `equipment` | 装备槽位，每项为 `[装备类型, 装备编号]` 对 |
| `weapon` | 武器槽位，每项为 `[武器类型, 武器编号]` 对 |
| `model` | 模型节点、动画、局部变换和材质，见下文 |

玩家 ID 与角色编号独立。`session.yaml` 的每条绑定指定一个玩家及其受控角色，
玩家可绑定任意已有角色；种子绑定玩家 1 → 角色 1，并指定角色 2 为 AI 示例。
角色 1、2 都不是引擎特殊编号。

### 角色与武器的 `model`

配置直接属于对应记录，没有单独的武器定义表。
配置了模型的武器是具体实例，一个武器编号只能被一个角色装备；多个角色使用相同外观时，
各自使用独立编号，YAML 可用锚点共享模型配置，运行时共享 GPU 资源但分别拥有场景实例。
旧数据中未配置模型的武器引用继续兼容。当前种子完整示例见
[`characters.yaml`](../runtime/saves/01/characters.yaml) 和 [`weapons.yaml`](../runtime/saves/01/weapons.yaml)。

| 字段 | 含义与默认值 |
|---|---|
| `path` | 相对 asset root 的 OBJ/glTF/GLB 路径；空路径为角色占位几何，武器空路径不装配显示实例 |
| `transform` | 整个模型的局部变换，默认单位变换 |
| `nodes` | 可选节点数组；非空时替换文件节点层级，复用文件中的网格 |
| `animations` | 与显式节点配套的动画片段数组；未提供节点时保留文件自带节点和动画 |
| `material` | 覆盖整个模型的 PBR 材质，不自动读取 OBJ MTL 或 glTF 材质 |
| `eyes` | 相机挂点名称，默认 `Eyes`；空字符串使用角色配置眼高 |
| `weapon_mount` | 角色模型的近战武器挂点名称，默认 `RightHandGrip` |
| `idle_animation` | NPC 循环动画名称，默认不播放 |
| `action_animation` | 玩家 F2 / 武器左键单次动画名称，默认不播放 |

`transform` 包含 `translation`、`rotation_degrees`、`scale`，均为三元素数组，默认分别为
`[0, 0, 0]`、`[0, 0, 0]`、`[1, 1, 1]`。旋转使用 XYZ Euler 角并转换为四元数。
节点包含必填 `name`、可选 `parent_index`、`mesh_index` 和 `transform`；索引从 0 开始，
父节点必须在子节点前面。无 `mesh_index` 的节点是挂点。局部变换属于节点，没有独立 correction 字段。

动画片段包含 `name`、`channels`；每个通道包含 `node`、`path`、`times`、`values`，
`path` 为 `Translation` / `Rotation` / `Scale`，值是三元素数组，旋转值单位为度。
可选 `interpolation` 为 `Step` / `Linear`，默认 `Linear`。时间必须非负且严格递增，
通道与节点约束沿用[模型动画契约](resources.md#模型资源与实例)。

材质支持 `base_color`（RGBA，默认全 1）、`metallic`（默认 0）、`roughness`（默认 0.8）、
`emissive`（RGB，默认全 0）、`normal_scale`（默认 1），以及可选贴图路径
`base_color_texture`、`metallic_roughness_texture`、`normal_texture`、`emissive_texture`。
贴图路径相对 asset root；底色和自发光使用 sRGB，MR 和法线使用 Linear，MR 的 G/B 通道分别为粗糙度/金属度。
配置法线贴图时自动生成切线；缺少有效 UV 的模型会明确失败。AO 尚不支持。
各贴图缺省时使用渲染器对应的回退行为，详见[光照](lighting.md)。

Save 在入口校验配置数值、显式节点和动画，不加载外部素材。实际装配时再校验文件中的网格索引、
所选动画和挂点是否存在；失败不发布模型借用。运行对象与快照都保留完整配置，checkpoint 会原样保存。

### 会话 `session.yaml`

`SerializedSession` 保存持久身份绑定，不指定谁是本地观察者：

```yaml
players:
  - keyid: 1
    character: 1
  - keyid: 2
    character: 100024
example: 2
```

`players[].keyid` 是非零且唯一的玩家编号，`character` 必须指向已有角色，
同一角色不能重复绑定。`players: []` 是合法的纯 NPC 世界。
`example` 是 AI 演示角色编号，`0` 表示没有；它不能同时被玩家控制。
文件仍为单个映射，世界记录仍为各自的序列。新种子必须使用这个格式。
checkpoint 版本 1–4 保留读取迁移，旧 `player` 角色编号迁为玩家 1 的角色绑定，
其余世界、队员命令、背包和模型数据按各版本既有规则恢复。新写入统一为版本 5。

## 读取、校验与装配

`Save` 先把七个文件全部读成记录，再校验，最后才由 `GameWorld` 建对象：

1. `Save` 读七个文件，检查同类编号重复。
2. `Save::validate_references` 检查每个引用：会话的各条 `players` 绑定、`example`，队伍的 `faction`、
   `leader`、`members`、`captives`，以及角色的 `party`、`equipment`、`weapon`。玩家编号为零或重复、角色重复绑定或不存在、`example` 与任一玩家角色相同，都在这一步失败。悬空引用是数据错误，
   在碰任何运行期对象之前就失败。
3. `GameWorld::build_world` 按依赖顺序建对象：势力 → 装备/武器 → 队伍 → 角色。因为第 2
   步已经保证引用成立，这里的查表不会再失败。
4. `GameWorld::link_party_members` 最后通过 `Party::restore_roster()` 一次性链接每队的队长、成员和俘虏。
5. `GameWorld` 从玩家绑定表装配各自的控制器，用 `save.example()` 取出 AI 示例（`0` 时跳过）。
   `GameSession` 根据应用选定的玩家建立 Connection，并以该角色的位置构造本地 `PlayerView`。

`GameWorld::load(Save const&)` 先清空对话、冷却和交互目标，释放角色名单借用，再按依赖的
逆序销毁旧角色、队伍及其资源，随后执行与构造相同的完整装配和状态恢复。
大陆和对话目录保持不变，NPC 主动对话的初始等待重新计时；不保留对 Save 的引用。
旧角色、队伍、装备、武器、势力及活动对话的指针和引用均失效，调用方必须重新查询。
这是先清空再构建的接口：构建失败不恢复旧世界，必须成功重试 `load()` 后才能查询或更新世界。
游戏内读档由 Application 在下一帧开始、取得交换链图像前执行：先等待 GPU 空闲，
再构造完整候选 GameSimulation、GameSession 和对应容量的帧资源。候选成功后先释放旧帧资源，再替换会话及模拟，
最后安装新帧资源；相机恢复默认斜视角并设置当前窗口宽高比，字体按新角色名单重建。
因此 UI 读档也支持不同大陆，不使用同大陆限定的世界层 `load()`。
解析、世界装配或候选资源构造失败时保留旧会话并显示错误；Vulkan 系统错误沿现有异常路径退出。

角色借用模拟侧队伍，队伍名册只保存角色 ID；队伍先建、成员后装配，`Party` 因此允许暂时没有队长。

除了编号和引用，读取还检查装备／武器类型与所在槽位一致、队伍列表无重复、队长与普通成员
及俘虏互斥、角色与队伍关系双向一致。

## 运行进度与写入

`GameWorld::snapshot()` 从当前运行对象生成 `SerializedWorld`，不读取启动时的旧 `Save`。
运行中的保存由主线程提交请求，模拟线程在完整 tick 结束后生成该快照，再由主线程写盘；
不从表现快照拼装存档，也不跨线程遍历运行对象。tick 和输入确认编号属于本次会话，不改变 checkpoint 格式。
`GameSession` 将 F6 转为打开命名窗口，窗口确认后向宿主 Application 提交槽位路径；Application 请求 checkpoint 并交给 `Save::write`，
并把成功或失败反馈给界面；只有确认按钮产生实际写入。
会话持有存档目录的副本，不借用 `Save`。

每个角色必须有且只有一条 `states` 记录：

| 字段 | 含义 |
|---|---|
| `keyid` | 角色编号 |
| `x`、`y`、`z` | 脚底世界坐标，米 |
| `body_yaw` | 身体朝向，度 |
| `look_yaw`、`look_pitch` | 相对身体的观察角，度 |
| `patrol` | 是否恢复示例 NPC 的巡逻控制器；只有 `session.example` 可启用，伴随控制器不算巡逻 |
| `companion_order` | `None` 保持原型控制行为，`Follow`/`Wait` 恢复为对应模式的 `AiCharacterController` |
| `bag_capacity` | 背包重量上限，单位克 |
| `bag` | `{keyid, quantity}` 列表，记录资源编号和数量；空背包必须写 `[]` |

背包堆叠按资源编号排序写出。读取和写入均拒绝未知资源、重复编号、零数量和超重，
重量使用内置资源目录计算，不信任存档提供的重量或定义。世界恢复时以保存容量重建背包，
恢复所有角色的物品；背包条目只保存编号和数量，读取时按编号经 `find_resource` 取定义。
种子存档没有运行状态，只有显式场景装配可以发放初始物资；加载 checkpoint 不补发。

读取检查坐标与角度有限、观察角在角色允许范围内；运行期构建角色时再检查大陆边界和碰撞。
招募会停止示例巡逻并设为 `Follow`；跟随／等待与巡逻分别保存，重启后恢复同一命令。
命令只允许用于有队长的队伍中的普通成员，不能与巡逻同时启用；队长、俘虏及无队长队伍不可携带命令。
玩家角色仍由玩家控制；入口拒绝为玩家角色恢复 AI 队员命令。招募玩家只变更队伍关系，不替换控制器或添加跟随命令。

读取 `version: 1` 时按旧的必填字段解析，保留姿态和巡逻，并将所有命令迁移为 `None`；
下次写入为 version 5。version 2 的 `companion_order` 必填，version 3 另需大陆字段；version 4 还要求每个角色的背包容量和堆叠列表，version 5 使用玩家绑定表，其他版本明确拒绝。
旧 version 1–3 按各自完整的必填结构解析，迁移为空背包、10 kg 容量，不补发演示物资。
成员关系校验适用于所有版本进度，不自动修正不一致的历史数据。
当前不保存跳跃速度、巡逻当前路点、对话和相机模式／缩放；重启默认使用斜视角，空中角色以
零初始速度继续下落，现世房屋巡逻从最近的三维路点接回路线，未启用最近路点恢复的场景巡逻从初始路点重新开始；具体路线见[大陆设计](continent.md#可进入的巡逻房屋)。

写入先校验完整快照，再写 `checkpoint.yaml.tmp`，检查关闭文件成功，最后重命名替换
`checkpoint.yaml`。校验和临时文件写入失败不会改动旧进度；启动时忽略遗留的临时文件。
这里没有实现文件系统同步或断电持久性保证，也不支持多个进程同时写同一存档目录。

## reflect-cpp 约束

`std::map` 只有在键是字符串或数值时才编码成 YAML 映射（对象形式）。键不满足这个条件时
（枚举键就是这一类），reflect-cpp 改用 pair 序列，因为 YAML 映射的键必须是字符串。
角色的 `equipment`、`weapon` 因此写成 `- [Head, 1]`：

```yaml
  equipment:
    - [Head, 1]
    - [Body, 2]
  weapon:
    - [MeleeWeapon, 1]
```

`Head: 1` 这种映射写法会被拒绝（`Could not cast to sequence`）。枚举本身按名字写，
例如 `[Head, 1]` 里的 `Head`。map 形式也保证每种槽位至多出现一次。

## 尚未实现

- 自动保存、启动时记住上次选择的槽位，以及存档删除／改名。启动仍使用配置指定的槽位，可在主菜单手动选择其他存档。
- 除上述旧进度升级之外的世界版本迁移、角色存活状态，以及完整控制器／物理状态的恢复。

## 测试数据

`tests/fixtures/saves/01/` 保存固定的测试世界，不依赖可编辑的运行存档。
`save_data` 校验这些夹具、语义错误、进度往返、连续替换、写入失败保留旧进度及损坏进度诊断；
命名存档回归检查槽位枚举、排序、Unicode 路径、同名覆盖、槽位独立性、非法名称与损坏进度诊断。
`runtime_save_data` 另行检查构建树中的 `runtime/saves/01/` 能否完整加载。
`character` 检查运行对象招募后的序列化结果，`seed_save_refresh` 检查重复安装更新种子文件，
同时保留玩家进度文件。以上均为 CPU 测试，不能替代窗口内保存按钮和重启画面的实机验证。

## 大陆选择与内容版本

种子存档的 `continent.yaml` 为单个映射：

```yaml
id: present
version: 1
```

进度文件在顶层 `continent` 字段保存相同结构。`id` 是内容标识，不是显示名称；
内容版本与进度文件格式版本互相独立。当前支持 `present` 和 `prototype`，均为 version 1。
未知标识、不支持的内容版本和缺失字段明确报错，不回退到默认地图。

`GameWorld` 按存档标识构造并拥有大陆，快照从实际大陆读取标识。
`load()` 只接受与现有大陆相同的标识和内容版本；不匹配时在清理旧世界前拒绝。
切换大陆需重建会话，以协调地图显示与相机借用。

旧 version 1、2 进度按原有必填结构解析，迁移为 `present` version 1，写回为格式 version 5。
version 3 必须显式包含大陆字段。种子安装会覆盖运行树的 `continent.yaml`；已有 checkpoint 中的大陆选择仍优先生效。
当前未实现随机生成、地图文件加载或内容版本迁移；改变地图空间布局时需显式考虑内容版本兼容性。
