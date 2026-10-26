# 对话、状态入口与 NPC 交流

## 当前玩法

所有非玩家角色都可交谈。靠近并面向对方按 F 发起，主键盘 1–4 选择回答。
交谈过程中再按 F 结束，双方距离超过 5 米或会话超过 90 秒也会结束。
失焦、Ui 输入上下文和 ImGui 键盘捕获会屏蔽玩家交互与回答按键；
NPC 对话的时间仍随世界推进；和物理一样，每帧最多推进 0.1 秒。

每次发起都按当前状态选择对话：可招募对象使用招募对话，同队其他对象使用队友对话，
其他情况使用普通问候。当前招募规则支持把自己队伍的俘虏转为普通成员，
以及把其他队伍的普通成员转入本队；只有队长可以招募，不能招募其他队伍的队长或俘虏。
手动招募流程是问候 → 邀请 → 确认 → NPC 回应；确认前可以返回或退出。
资格在执行动作前复查，过期选项只刷新当前节点，不误选隐藏后移位的回答。

NPC 也会主动交谈：加载后先安静 8 秒，在 4 米内寻找可交谈的对象。
NPC 可以向有资格招募自己的角色请求加入，对方是玩家时由玩家同意或拒绝；
其他情况发起闲聊。玩家移动、跳跃、奔跑、在空中或 UI 占用输入时，不接收新的主动请求。
其中“角色是否空闲”由 `Character::accepts_ambient_requests(intent)` 判断，UI/焦点门控由会话通过 `WorldInteraction::player_input_enabled` 提供，两者不再混在控制器里。
没有路径规划、为了聊天追赶远处角色或穿越场景的能力。

多组互不重叠的角色可以同时交谈，每个角色同时只参加一场。
参与交谈的 NPC 暂停原有巡逻并面向对方，重力等物理继续执行，结束后恢复同一个巡逻控制器。
成功招募会停止示例巡逻并默认跟随新队长。NPC 与 NPC 的台词显示在当前说话者头顶；
涉及玩家的对话显示在 HUD，并且只在轮到玩家决策时显示编号选项。

## 铁匠与打造

种子世界新增独立队伍的 `Blacksmith`（角色 4），出生在初始区域，使用普通 NPC 的
接近、面向与 F 交互规则。专属入口在通用招募规则之前，通过目录中的角色编号匹配。
询问打造后显示配方：每个 `Iron Helmet` 消耗 5 单位生铁、2 单位木材和 20 moneta。
材料、费用或背包空间不足时不显示确认打造选项，仍可退出；执行时再次检查，过期选项不扣款。
确认后产物作为背包物品增加一个，不自动替换当前头部装备；显示回答期间重复输入不重复制作。

背包使用资源类型作为 `unordered_map` 键，HUD 按固定顺序显示数量。世界产出的资源定义由世界拥有，
背包借用它们，重载时先销毁角色再清理资源；铁盔配方使用进程期只读定义。
背包容量和打造原子性见[资源契约](design/resources.md)。
保留原型每帧获得 moneta、木材和生铁的测试产出。背包和打造结果目前只在本次运行有效，
尚未接入 checkpoint；构建会覆盖更新种子副本；已有 checkpoint 不会自动添加新角色，需使用更新后的种子另开存档。

动作 `ForgeIronHelmet` 的 `recruiter` 字段指定顾客，动作通过铁盔配方取得输入与产物定义并独立复查配方。
CPU 角色测试覆盖精确扣款、材料不足、过期选项、容量不足、数量溢出和重复输入。

## 队员跟随与等待

队长与普通队员交谈时，问候节点最多四项：闲聊、`Follow me.`、`Wait here.`、结束。
条件 `CanCommand` 检查发起者是否为对方所在队伍的队长，且对方确实是普通成员；
俘虏、其他队伍和非队长看不到命令。动作 `Follow`／`Wait` 仍会独立复查资格，
过期选项不会执行；`recruiter` 对这两个动作表示命令发出方。

`Party::command` 用 `CompanionOrder` 安装/替换角色的 `AiCharacterController` 伴随模式，
因此旧巡逻会被丢弃；招募成功后自动变为 `Follow`。
跟随使用现有地面移动、重力与碰撞，水平距离到 2 米（或双方半宽之和，取较大者）时停下，
单步限制剩余距离，避免越过停止位置。没有绕障寻路、传送、追赶加速或编队槽位，
障碍挡路时会停住，多个队员可能重叠。`Wait` 停止自主水平移动，物理仍继续。
会话期间暂停跟随，结束后恢复原命令；玩家始终由玩家输入控制。
命令与存档恢复见[存档与继续游戏](save-data.md)。

## 三层职责与生命周期

- `DialogCatalog`：拥有不可变的内容目录、脚本、入口规则和时距配置。
- `Dialog`：一场会话，借用脚本和参与者，记录节点、等待谁回答、正在展示哪句台词。
- `DialogSystem`：拥有目录和多场会话，处理互斥、距离、时限、冷却和 NPC 发起。

这些类型位于游戏层，不依赖窗口、输入、渲染或 ImGui。
`GameSession` 门控玩家输入并协调本地会话；`GameWorld` 把交互输入交给当前对话、
暂停参与者的移动控制器，并推进 NPC 请求与冷却。`GameUi` 生成屏幕文字，`WorldPresentation` 生成头顶文字。
这些仍是本地原型，没有加入未来服务器／客户端网络类。

`Dialog` 的脚本和角色是非拥有指针，必须在会话使用期间存活。
`DialogSystem` 不可复制或移动，因为会话借用它自身目录中的脚本；目录先于会话构造、晚于会话销毁。
`find()` 返回的会话指针不可跨越下一次 `update()` 或该会话的 `interrupt()` 保存。
`GameWorld::load()` 在销毁角色表前调用 `reset()`，释放参与者借用并重置冷却和初始等待；
`clear()` 只清空会话和冷却，保持当前初始等待进度。
析构时对话只释放自身状态，不解引用参与者。

## 内容目录与入口规则

入口文件为 `runtime/dialogs/catalog.yaml`。简化例子：

```yaml
version: 1
settings:
  start_distance: 4.0
  end_distance: 5.0
  line_seconds: 1.5
  cooldown_seconds: 30.0
  initial_delay: 8.0
  max_duration: 90.0
scripts:
  - {id: recruit, file: recruit.yaml}
  - {id: companion, file: companion.yaml}
  - {id: greeting, file: greeting.yaml}
rules:
  - {trigger: Interaction, talker: 0, target: 0, conditions: [CanRecruit], dialog: recruit}
  - {trigger: Interaction, talker: 0, target: 0, conditions: [SameParty], dialog: companion}
  - {trigger: Interaction, talker: 0, target: 0, conditions: [Always], dialog: greeting}
```

`id` 是对话身份，文件名和节点 ID 只在各自层内有效。不同文件可以使用相同节点 ID。
所有字段必填；`scripts` ID 非空且唯一，入口规则必须引用已有对话。
脚本路径相对目录文件，拒绝绝对路径、盘符根和规范化后仍包含 `..` 的词法逃逸。
遵循资产 ID 的词法约定，不进行符号链接别名去重，也不作为不可信脚本的文件系统沙箱。

`rules` 按书写顺序检查，第一条匹配的规则胜出：

- `trigger`：`Interaction` 为主动交互入口，`Nearby` 为 NPC 邻近发起入口。
- `talker`、`target`：角色编号过滤，`0` 表示任意角色，不在 C++ 写死任何角色编号。
  可以在通用规则前加一个特定角色的规则。编号是内容过滤条件，不要求每份存档都包含该角色。
- `conditions`：非空列表，全部满足才匹配；多个候选规则可以表达“或”。
- `dialog`：选中的对话 ID。没有匹配时不发起交谈，不隐式加载后备脚本。

当前条件读取真实的角色和队伍状态：

| 条件 | 判断 |
|---|---|
| `Always` | 总是满足 |
| `CanRecruit` | `talker` 所在队伍能否由 `talker` 招募 `target` |
| `CanBeRecruited` | `target` 所在队伍能否由 `target` 招募 `talker` |
| `SameParty` | 双方属于同一个非空队伍 |
| `CanCommand` | `talker` 是队长且 `target` 是其普通成员 |

生产目录还包含 `request_recruitment` 和 `small_talk`，对应 NPC 请求加入与闲聊。
增加一份对话：编写 YAML → 加入 `scripts` → 在合适位置加入口规则。
同两个人在招募前后会选择不同对话。入口只在会话开始时计算，选定后不因状态变化而突然换脚本。
任务、好感、性格、战斗忙碌状态和首次见面记录尚不存在，因此没有虚构相应条件或存档字段。
以后实现真实状态后再扩展条件判断，入口与会话机制可复用。

## 多轮脚本与参与身份

每份脚本定义一张对话图。脚本格式已从最初的 `version: 1` 升至 `version: 2`，
新增了说话者、回答者、招募执行方和 NPC 选择优先级；旧格式会明确报错。
所有字段必填、名称区分大小写、内容使用英文。

`talker` 是发起交谈的角色，`target` 是被交谈的角色，整场会话中身份不变。
节点用 `speaker` 指定当前台词的说话者，用 `respondent` 指定谁做下一次选择；
两者都可取 `Talker` 或 `Target`，不由玩家／NPC 类型决定。

```yaml
version: 2
entry: request
nodes:
  - id: request
    speaker: Talker
    respondent: Target
    text: "{target_name}, may I join your party?"
    choices:
      - id: accept
        text: "Yes, join us."
        condition: CanBeRecruited
        action: Recruit
        recruiter: Target
        npc_priority: 10
        next: joined
      - id: decline
        text: "Not now."
        condition: Always
        action: None
        recruiter: Target
        npc_priority: 0
        next: end
  - id: joined
    speaker: Talker
    respondent: Target
    text: "Thank you. I am now a member of your party."
    choices:
      - id: goodbye
        text: "Welcome aboard."
        condition: Always
        action: None
        recruiter: Target
        npc_priority: 0
        next: end
```

发起者可以是想加入队伍的 NPC，回答者和招募者是玩家；由 `recruiter: Target` 明确动作身份，
不把发起者误当成招募者。动作仍调用 `Party::can_recruit/recruit`，脚本不能绕过队长或队伍规则。
`None` 不产生动作，此时 `recruiter` 只需填写一个有效身份。
`Follow`／`Wait` 用同一身份字段指定命令方，只能命令其普通队员；动作本身也复查队长资格。
条件和动作检查相互独立：`Recruit` 即使搭配 `Always` 也必须满足动作自身的资格。

- `entry` 引用开始节点；节点 `id` 非空且文件内唯一。`end` 是保留的结束目标。
- 每个节点有 1–4 个按书写顺序显示的选项，选项 `id` 在节点内唯一；重排不改变动作含义。
- 选项 `condition` 使用同一套真实状态条件，不满足时隐藏。
- `next` 跳转到同一份脚本内的节点，或 `end`；允许返回和循环，不支持跨脚本跳转。
- 每个节点至少有一项 `Always + None`，保证资格变化后仍有可用选择。
- 台词与选项支持 `{talker_name}`、`{target_name}`；直接插入姓名，插入结果不再解释。
  其他占位符、空文本或不匹配的花括号报错，不支持花括号转义。

玩家选中的回答和 NPC 选中的回答都会作为回答者的台词展示，再进入下一节点；
展示回答时没有可选项，重复输入不会重复执行动作。
动作在选择时执行，先验证资格再修改世界，之后才展示回答。
每句自动推进间隔由 `line_seconds` 控制；一次更新最多推进一次，长帧不会跳过整段对话。
玩家回答一直等待按键或会话被中断／超时，不会被 NPC 自动选择代答。

## NPC 回答与调度

NPC 每轮选择当前可用选项中 `npc_priority` 最高的一项；相同时选择书写顺序靠前的项。
这是可测试的基础策略，不是性格模拟或大模型生成。用正值鼓励一个回答、负值降低循环选项的优先级。
选择前重新计算资格，隐藏已经失效的选项；动作再按实际执行方检查。

调度按角色编号排序，NPC 从最近的可交谈对象中选择，等距时使用较小编号。
角色 → 会话的索引用于常数时间查找；邻近搜索在按 X 排序的角色表中二分定位半径内区间，
然后检查真实三维距离和规则，不改变原有排序策略。临时数组复用容量，返回前释放角色借用；
`GameWorld` 缓存固定角色表，若以后支持运行期生成／移除角色，必须同步更新。
最坏情况下所有角色挤在同一个 X 区间仍可能二次遍历，未加入空间网格或降低调度频率。
已有会话的角色不再参加另一场对话；玩家自己的交互在本帧 NPC 邻近调度之前处理。
`allow_player_requests` 仅控制新的 NPC → 玩家请求，不暂停 NPC → NPC 会话。
玩家的答复仍只能通过受输入上下文／焦点控制的 UI 提交。

结束、拒绝、距离中断和超时后，双方进入 `cooldown_seconds` 的主动交流冷却。
冷却只限制 `Nearby`，玩家可以立即再次主动交互。会话超时也限制错误循环脚本长期占用角色。
所有时距配置必须有限，时间参数非负，行间隔和冷却严格为正，结束距离不少于开始距离。

## 内容更新、存档与验证

`copy_dialogs` 在构建游戏和对话／角色测试时刷新构建树的 `runtime/dialogs/`。
编辑后运行 `make run` 会复制新内容，运行中的会话不热重载。
路径相对运行工作目录，与 `shaders/` 一致；`LC1_ASSET_ROOT` 不改变此目录。
脚本未知字段、缺失字段、未知枚举、重复 ID、无效节点引用、目录中不存在的对话 ID 和错误配置
都在开始游戏前报错，诊断包含文件路径。

内容和临时会话不写入存档；不保存节点、行计时、冷却或谈话历史。
实际招募结果和原有巡逻状态仍通过 F6 写入 `checkpoint.yaml`，重启后入口会使用恢复的队伍关系。
详见[存档与继续游戏](save-data.md)。没有实现任务进度、翻译键、语音、多人共同会话或演出。

验证分三部分：

- `character`：既有招募、过期选项、序列化、输入上下文和多轮节点回归。
- `dialog`：固定的 `tests/fixtures/dialogs/` 内容，测试状态入口优先级、角色过滤、双方轮流说话、
  NPC 主动请求、玩家拒绝、明确的招募方、NPC 自动回答、并行互斥、初始延时、冷却、距离／时限退出、
  暂停巡逻保留物理，以及错误目录和脚本路径诊断。
- `runtime_dialog_data`：加载实际构建树内容，校验所有脚本与入口规则。

以上均为 CPU 测试，不能替代窗口内交互和 GPU 验证。

## 调度测量（2026-10-22）

本机 Linux Clang Debug，512 个 NPC 在平面上以 10 米间距排列，无可交谈近邻，
先消耗 8 秒初始延时，再连续更新 120 帧：原实现约 11.47 ms／帧，
会话索引与 X 区间筛选后约 0.58 ms／帧。这只覆盖分散角色的 CPU 调度，
不代表密集人群、Release 或 GPU 帧率；不将耗时阈值作为测试通过条件。

复现命令（仓库根目录）：

```sh
build/linux-x86_64/Debug/lc1_dialog_tests tests/fixtures/dialogs/catalog.yaml --benchmark
```
