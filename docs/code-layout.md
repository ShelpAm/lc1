# 应用、游戏与引擎的代码边界

目录按职责划分，CMake 目标按依赖方向连接：`lc1` → `lc1_game` → `lc1_engine`。
游戏类型沿用现有 `lc1` 命名空间；渲染场景类型使用 `lc1::scene`，应用入口使用 `lc1::app`。
模块依赖概览见 [模块依赖图](module-dependencies.md)。

| 位置 | CMake 目标 | 职责 |
|---|---|---|
| `app/main.cpp`、`app/application.*` | `lc1` | 进程入口、窗口和 Vulkan 生命周期、输入接入、交换链和每帧调度 |
| `include/lc1/game/`、`src/game/` | `lc1_game` | 按玩法功能域划分的规则、状态、控制与表现适配代码 |
| `game/debug/pbr-showcase.*`，分别位于上述头文件和源码目录 | `lc1_game` | 当前嵌入游戏世界的 PBR 展示，供游戏和 GPU 测试共同使用 |
| `include/lc1/platform/`、`src/platform/` | `lc1_engine` | 窗口、设备输入与通用输入路由；类型仍使用 `lc1` 命名空间 |
| 其余引擎模块，包括 `scene/`、`vk/` 及资源模块 | `lc1_engine` | 场景表达、渲染和平台能力 |

`include/lc1/game/` 与 `src/game/` 使用相同的功能域子目录，便于按“要改哪个玩法”而不是
按“controller/view/model”定位文件：

| 目录 | 职责 |
|---|---|
| `character/` | `Character`、`CharacterIntent`、`CharacterController`、AI 巡逻/伴随模式、装备、武器 |
| `player/` | 玩家动作到 intent 的解析、第一人称/斜视角状态与玩家相机跟随 |
| `camera/` | 可复用的地图/斜视角相机算法，不读取输入、不拥有角色 |
| `physics/` | 角色运动求解、静态碰撞查询、运动配置与重力常量；碰撞规则见[角色物理](physics.md) |
| `continent/` | 大陆数据与地理查询、布局/原型/生成数据、地形绘制 |
| `party/` | 队伍、阵营和队员命令 |
| `dialog/` | 对话脚本、单场会话、入口规则与冷却 |
| `world/` | `GameWorld` 装配与更新、`WorldPresentation`、可交互对象 |
| `session/` | `GameSession` 主线程协调、`GameSimulation` 固定步长线程以及 `Save`/序列化 |
| `ui/` | 会话内 ImGui 界面与音乐浏览器 |
| `debug/` | 嵌入世界的 PBR 展示，游戏与 GPU 测试共用 |
| `actions.hpp/.cpp` | 游戏自己的动作、context 和默认绑定；设备层是 `lc1/platform/input.hpp` |

`actions.hpp` 没有单独建目录：它只有一组小接口，但必须和引擎的 `lc1/platform/input.hpp` 明确区分。
引擎头文件只有 `InputState`、`Key` 和模板 `BasicInputRouter`；游戏的 `GameAction`、
`GameInputContext`、`GameInputRouter` 和 `default_game_bindings()` 都在 `game/actions.*`。

`vk/` 内部分为 `core`、`resources`、`presentation`、`render`；呈现与渲染各自依赖底层
资源，由应用组合。目录职责与依赖约定见 [Vulkan 代码分层](vulkan-layers.md)。

## 声明与实现

普通非模板函数（包括访问器、构造函数和辅助函数）的实现放在对应 `.cpp`，头文件保留声明、
类型定义和必要依赖。模板及 `constexpr`/`consteval` 定义仍保留在头文件中；
类内 `= default` 与 `= delete` 保持原位，避免改变平凡性和特殊成员语义。
普通函数的推导返回类型在拆分时写为等价显式类型；默认参数和调用点默认 `source_location`
保留在声明处。引擎和游戏新增源文件分别登记在原有 CMake 目标，不改变依赖方向和资源所有权。

## 本地会话的职责

`GameSession` 是本地原型的协调者，不是服务器 `GameMode`，也不是网络复制状态。
它组合以下对象；当前仍编入 `lc1_game`。本地模拟与表现通过值消息交换，支持多玩家绑定，尚无网络传输。身份、绑定和观察者约定见[玩家与多人模拟](players.md)。

| 对象 | 职责与边界 |
|---|---|
| `GameWorld` | 按存档标识和内容版本加载并拥有大陆、势力、装备、武器、队伍、角色、角色控制器和 `DialogSystem`；装配世界、推进规则、生成存档快照；不依赖 Window、ImGui、音频或 Vulkan |
| `GameSimulation` | 独占 `GameWorld`，以 60 Hz 固定步长推进规则；接收输入消息、发布不可变快照并处理存档快照请求 |
| `WorldPresentation` | 读取主线程的 `WorldSnapshot` 和只读大陆，拥有地形/角色/武器 GPU 资源、字体、持久 Scene 与音频 context/listener；更新场景实例、灯光并生成世界文字 |
| `PlayerView` | 拥有视角模式、记忆观察角和相机；准备视角输入，读取角色快照与模型挂点更新相机 |
| `GameUi` | 菜单、调试面板、保存反馈及屏幕文字；返回命名保存、读档请求和音量变化，由会话执行 |
| `MusicBrowser` | 音乐目录、文件浏览 UI、播放状态与音源；借用 `WorldPresentation` 的音频 context |
| `GameSession` | 读取输入、按焦点/context/ImGui capture 门控、协调各阶段、向 Application 提交命名保存及读档请求、直接设置窗口标题和光标捕获，并向 Application 提供退出状态和表现数据 |

`Character` 位于 `game/character/character.*`，内部唯一的 `scene::Transform` 保存脚底位置和身体朝向。
它不保存 Scene 或模型实例借用；接受攻击后推进近战轨迹与伤害规则，并发布表现所需状态。`Party` 同样只处理成员、招募和命令，不负责绘制。
`WorldPresentation::update()` 将快照中的角色变换提交给持久 Scene 的渲染实例，玩家是角色集合的一员，不重复追加。
Application 通过 `GameSession::world()`、`world_presentation()`、`player_view()` 和 `ui()` 的只读引用
查询各自的数据；`world()` 返回本帧的 `WorldSnapshot`，不暴露模拟线程的世界。快照内容在下一次会话更新时替换。会话负责更新协调、保存及反馈，并提供宽高比设置入口，
不为每个子对象查询重复提供转发方法。

角色与武器的模型、节点和材质由对应 YAML 记录指定，相同配置共享 GPU 资源；
种子中的 NPC 使用占位模型，玩家使用木桶，玩家和 NPC 的已装备近战武器跟随各自模型的持握挂点，
素材路径及动画行为见[角色文档](character.md#本地模型试用)。CPU 几何在上传后释放；Scene 为每个角色拥有独立的 `ModelInstance`；
`WorldPresentation` 保存角色 ID 到角色/武器场景实例的关联；`Character` 处理挥砍 intent 并发布攻击序号；
`WorldPresentation` 装配模型、设置初始待机动画并提交角色世界变换，Scene 推进动画、查询挂点并展开 DrawItem；角色仍是玩法变换的唯一权威。
模型与实例的索引及借用约定见[资源文档](resources.md#模型资源与实例)。`scene::Object` 仍可用于其他渲染场景，不再是角色世界姿态的所有者。

示例内容由 CPU 侧 `game/debug/world-setup.*` 集中装配，`GameWorld` 内部加载存档对应的
临时 `WorldSetup`：大陆含展示碰撞体，另带示例巡逻路线、出生点和初始物资。
这些演示内容不作为世界构造参数；以后移除时不影响调用方。
`GameWorld` 的对话目录路径由外部调用方显式传入，构造函数不提供默认参数。
展示绘制和碰撞共用 `showcase_blocks()`；原 `PbrShowcase::spawn_blocks()` 转发该 CPU 数据入口。

演示物资仅在从种子数据创建世界时发放一次：10 Moneta、2 木材和 3 生铁，共 8.1 kg，
适配默认 10 kg 背包。`update()` 不产生测试物资，checkpoint 精确恢复已消耗的数量。
背包格式和旧存档迁移见[存档](save-data.md)。

`DialogCatalog` 拥有 YAML 内容和状态入口规则，`Dialog` 表示一场交流，`DialogSystem` 管理
并发会话、NPC 发起和冷却。`GameWorld` 负责对话参与者注册、按玩家处理交互、向 AI 提供对话伙伴；AI 控制器决定对话期间的行为；
`WorldPresentation` 和 `GameUi` 只读这些状态来生成世界/屏幕文字。
玩家名与区域/地点 HUD 也由 `GameUi::screen_labels()` 统一生成，Application 只负责提交文本绘制。
`GameSession` 在构造及每帧更新中直接调用借用的 `Window` 设置标题和光标捕获；`Window::set_title()` 缓存标题，
相同内容不调用 GLFW，返回是否发生变化供会话按需记录日志。详见[对话与招募](dialog.md)。

## 每帧时序

带模拟时间的更新接口统一把 `delta_seconds` 放在第一个参数，其余参数保持职责顺序；
例如 `GameSession::update(delta_seconds, input)`、`Character::update(delta_seconds, continent)`。
`update_pointer()`、`update_camera()`、`walk()` 和 `pan()` 同样遵循此顺序；无时间参数的接口不添加占位时间。

Application 轮询事件、开始 ImGui 帧，然后调用一次 `GameSession::update()`：

1. 会话用原始 `InputState` 更新自己拥有的 `GameInputRouter`，每次会话更新只解析一次动作。
2. 根据菜单、窗口焦点、context 和 ImGui capture 计算输入门控，调用 `PlayerView::update_view_mode()` 处理视角切换、锁定与回中。
3. `PlayerView` 在切换帧丢弃旧光标模式下累计的鼠标位移，避免切入第一人称时镜头跳动。
4. `PlayerView::prepare_input()` 准备视角、观察增量和输入门控；会话用 `PlayerCharacterController::read_input()`
   把角色控制、交互和对话选项统一复制成不借用设备的 `PlayerControls`，提交给模拟线程。
5. 会话在本帧开始获取一次表现快照，保存到自己的 `WorldSnapshot`；UI、相机、表现和 Application
   都读取这一份状态，不访问模拟线程的角色、控制器、队伍或对话对象。
6. `PlayerView::update_pointer()` 在鼠标门控允许时处理滚轮与边缘平移；`WorldPresentation::update()`
   按渲染帧时间推进 Scene 动画，用快照更新角色和武器实例，再由 `PlayerView::update_camera()`
   读取角色快照与最终 `Eyes` 挂点更新相机。音频监听器随后读取最终相机。
7. `GameUi::update()` 构建 UI 并返回开始、退出、保存及音量请求；会话接通开始操作，
   会话把 F6/F9 转为打开保存/读档窗口，将确认的槽位请求交给 Application；Application 在下一帧开始执行宿主保存/读档并反馈结果，同时处理退出。主菜单/设置屏蔽玩法输入，世界继续模拟；返回主菜单会释放光标。
   菜单切换后的玩法门控在下一帧生效。
8. 会话在处理 UI 返回的开始、退出及音量请求后，直接调用 `Window::set_cursor_captured()`，按菜单和视角状态应用光标捕获。捕获变化由 Window 重置设备鼠标基线；
   本帧视角切换已丢弃旧位移，下一次光标位置事件建立新基线。边缘推屏仅在斜视角且窗口光标已释放时允许。

菜单之外的 ImGui 占用同样需要区分：会话读取的 capture 只表示“某个控件正在使用键盘或存在模态窗口”。
Esc 在 `game/actions.*` 中映射为 `QuitGame`；窗口有焦点且 ImGui 未占用键盘时，
会话将其转为 `quit_requested`，主菜单中也可退出。Application 统一消费退出请求。
Application 不启用 ImGui 键盘导航，并显式保留 `io.ConfigNavCaptureKeyboard = false`。导航会和角色
争用方向键与空格；默认设置下，仅被聚焦的窗口（常驻调试面板就是一例）也会让 `WantCaptureKeyboard`
每帧为真，使所有 `GameAction` 都解析为未按下。菜单与面板都由鼠标操作。

## 固定步长模拟与线程交换

`GameSimulation` 拥有一个 `std::jthread` 和世界，使用 `steady_clock` 的绝对截止时间，
每个 tick 调用一次 `GameWorld::update(1/60, controls)`。角色控制器、物理、AI、冷却和对话
在同一个固定步长推进；角色名单按 keyid 排序，非跟随者先更新、跟随者后更新。所有角色移动完成后结算近战命中，再推进交互与对话；战斗规则见[角色文档](character.md#近战命中受击与死亡)。
工作线程延误时按原有截止时间补步，不改变步长或丢弃模拟时间。主线程卡顿不会暂停世界；
若持续算不过来，模拟会落后于现实时间，不承诺跨机器浮点确定性。

输入通过已绑定玩家的 Connection 在下一次尚未开始的 tick 消费，各玩家分别计序，提交返回 `InputStamp { sequence, tick }`。
快照包含完成的 `tick`；每个 `PlayerSnapshot` 包含该玩家已消费的最新 `input_sequence` 及对应的 `input_tick`；tick 从会话零开始，
用于描述本次模拟进度，不写入存档格式，也不是网络协议。
多个渲染帧落在同一 tick 前时，保留最新移动/冲刺/门控状态，累加鼠标位移，
合并跳跃、攻击、点头、交互的按下请求并保留最后一个对话选项。同一 tick 内同类请求合并成一次；
一次性输入消费后清空，持续状态保持到新消息到达。视角切换清除旧视角累计的观察位移。
窗口没有可绘制的交换链图像时，Application 只释放当前本地玩家的持续控制输入，其他玩家和世界仍继续模拟。

输入、快照指针和存档请求由短时间持有的同一个 mutex 保护；推进世界、复制快照和序列化
在锁外执行。快照全部拥有值，不包含指向活动世界的借用；主线程可继续持有旧快照。
`WorldSnapshot::parties` 按 keyid 排序，保存模拟队伍的独立 `Party` 值副本，不另定义 `PartySnapshot`。
队伍成员关系使用 `CharacterId`，模拟与表现共用名册查询；具体规则见[角色身份与队伍关系](character.md#身份与队伍关系)。
人物面板从同一帧快照读取角色资料、装备值、背包与队员命令，不借用模拟中的角色、装备或队伍对象。
队伍关系与其他离散状态一样采用最新 tick，不插值。
大陆在会话内不变，允许两线程共享只读访问。模拟同时保留相邻两个已完成 tick 的不可变快照及最新 tick 的计划时间。
会话按 `steady_clock` 的渲染采样时间生成延后一 tick（约 16.7 ms）的表现快照：
位置线性插值、身体 yaw 按最短弧插值、相对观察角线性插值；比例限制在 [0, 1]，
尚无新 tick 时最多到达最新姿态，不外推。启动时两端均为初始快照；读档创建新模拟，自然重置两端。
角色名单在模拟生命周期内固定且按 keyid 排序，插值按对应索引进行。
模型、武器挂点、世界文字、光照和相机读取同一份表现姿态；背包、对话、攻击序号及输入确认取最新 tick，
插值结果不写回模拟或存档。尚无客户端预测或回滚。

存档请求在模拟线程完成一步后生成 `SerializedWorld`，主线程等待结果再写盘。
工作线程异常通过快照查询、输入提交或存档 future 重新抛给主线程，沿既有错误处理路径退出/反馈。
销毁时请求停止、唤醒并 join，之后才释放世界。`GameSession` 在入口校验渲染帧时间；
渲染和相机继续使用完整渲染帧时间，模拟时间始终由固定步长产生。

## 生命周期与资源

Application 负责窗口/Vulkan 生命周期、原始输入接入、交换链和 ImGui 后端；
`GameSession` 持有显式绑定的本地玩家 Connection，拥有游戏动作路由器并在更新入口统一解析，借用非空且比会话长寿的 `Window`，
查询窗口状态并直接设置标题和光标捕获。窗口由 Application 拥有，不限制会话调用其公开操作。
Application 分别通过 `unique_ptr` 拥有 `GameSimulation` 和 `GameSession`，会话借用非空的模拟对象。
Application 用临时 `Save` 构造模拟，再将模拟与存档目录传入会话；`GameWorld` 构造和 `load()` 时消费 Save，不保留借用。
世界可以只用 Save、大陆和对话目录构造、更新、生成快照，无需创建窗口或设备。

Application 按 `GameSimulation → GameSession → FrameLoop` 构造并逆序销毁；会话不拥有模拟。
会话成员按 `WorldSnapshot → WorldPresentation → PlayerView → MusicBrowser → GameUi` 构造并逆序销毁。
模拟对象拥有世界和线程；表现与 UI 只读主线程快照，音频 context 先于音源存在。
模型实例关联只保存在表现层，约束见[持久渲染场景](resources.md#持久渲染场景)。角色名单在两次 `load()` 之间固定，
不支持逐个添加/删除；`load()` 在销毁角色前清空活动对话并重建名单，旧对象借用全部失效。
游戏内读档在 GPU 空闲后构造候选模拟、会话和帧资源，成功后依次替换帧资源、会话和模拟，
确保旧会话在旧模拟之前销毁；构造失败保留原有对象。
世界层重新加载的失败语义及表现层协调要求见[存档与装配](save-data.md)。
`GameSession`、`GameSimulation`、`GameWorld`、`WorldPresentation` 和 `MusicBrowser` 禁止复制/移动。

Application 在正常/异常退出时先等 GPU 完成。`DrawItem` 借用 WorldPresentation 的资源；渲染 pass 持有注册时复制的
本帧相机、灯光与 draw 记录快照，直到图销毁；GPU 资源仍由 WorldPresentation 持有。设备和 Renderer 比会话长寿，
FrameLoop 的帧资源在会话之后构造、之前销毁。`GameLocation` 的字符串视图借用只读大陆或静态名称。

## 构建与验证

游戏规则和表现目前仍在同一个 `lc1_game` 静态库中；这次隔离的是源码依赖与运行期构造，
未声称已提供无图形依赖的独立构建目标。引擎不得包含游戏头文件，app/game 不包含引擎私有实现。

`lc1_game_simulation_tests` 覆盖固定 tick、无渲染推进、输入确认、一次性请求、存档、异常传递与停止回收。
`lc1_game_world_tests` 覆盖无窗口世界装配、交互招募、跟随、存档恢复、完整时间以及
玩家控制 → 物理 → 相机的时序。原有大陆、角色、对话和视角 CPU 测试继续运行。
GPU 渲染和窗口交互需要单独验证。动作枚举、context、默认绑定属于 `game/actions.*`；
引擎 `lc1/platform/input.hpp` 只保留设备状态和模板路由。`lc1_input_tests` 使用自定义动作/context 验证通用路由。

## 修改功能时从哪里开始

- 新增规则或世界状态：在 `GameWorld` 及相应领域对象中实现；需要存档时同步修改记录、校验和往返测试。
- 新增表现：让 `WorldPresentation` 或 `GameUi` 从世界读取所需数据。资源所有权留在表现层，角色不持有 GPU 对象。
- 新增玩家操作：补 `game/actions.*` 的 `GameAction` 和绑定，由 `PlayerCharacterController` 解释为
  `CharacterIntent`，再由 `Character::update(delta, continent)` 决定能否执行；本地输入先复制为消息提交给模拟线程。
- 新增角色规则（耐力、眩晕、攻击互斥等）：放在 `character/` 的 `Character` 或对应 gameplay
  subsystem 中，不要写进玩家或 AI 控制器；控制器只填 intent。
- 新增 AI 行为：实现 `CharacterController::update()` 并返回 `CharacterIntent`，不要直接调用
  `move`/`walk` 或修改角色状态。伴随/等待是 `AiCharacterController` 的模式，由队伍校验权限并下达命令、
  由存档中的 `companion_order` 恢复。
- 新增渲染能力：先检查 [Vulkan 分层](vulkan-layers.md)与 [RenderGraph](render-graph.md)，不要让帧循环识别游戏对象或具体 pass。

上述分工让规则可以在没有窗口和设备的情况下测试，也让表现资源的生命周期集中管理。

## 未来网络边界（未实现）

服务端 `GameMode` 拥有并推进权威规则和状态。客户端 `GameClient` 负责与服务端通信，
`ClientGameState` 只保存表现与允许的预测所需的本地副本；`GameModeView` 或等价表现代码
从副本生成 UI 与渲染数据，不直接与服务端通信。客户端发送动作请求，服务端校验后发布状态结果。
当前 `GameSimulation` 已提供固定 tick、玩家身份绑定与线程间值消息边界，但未实现网络连接、认证、权限或网络复制。
`GameWorld` 与 `GameSession` 不冒充上述网络对象；只有明确实施联网功能时才增加网络边界，
不预先添加占位路由或空类。

## 角色与背包界面

游戏内按 B 或点击调试面板的 `Character & inventory` 打开／关闭统一窗口。
窗口上排为队伍成员、角色资料、属性，下排为分类背包、人物轮廓、物品详情与装备槽；
布局随窗口尺寸调整，各区域独立滚动；窄窗口保留最小内容宽度并提供横向滚动。人物轮廓是 UI 占位图，不是 3D 模型预览。

队伍区域沿用 `GameUiInfo::player_parties`，显示队长、普通成员及俘虏；队长用 ★ 标识，
当前玩家保留 `(Me)` 标识。选择成员后，其余区域读取该角色的数据。
UI 仅保存队伍、角色及资源的 keyid，不跨帧保存借用指针；选择失效时回退到当前玩家或队伍首位成员。
`GameUiInfo::player` 是指向当前玩家的非空 `Character*` 借用，只在本次 `GameUi::update()` 中使用。
玩家信息面板依赖 UI 成员状态，保留成员函数；队伍绘制放在 `.cpp` 匿名命名空间。

背包提供全部、装备、道具（货币和材料）三类页签，默认按种类、单件重量、资源 keyid 排序。
列表显示占位图标、名称、数量和堆叠总重量，支持滚动、拖动排序；切换分类或角色后恢复默认排序，排序不写入存档。
物品右键详情显示数量及重量，选中防具或武器时定位对应槽位并显示属性。容量条显示背包重量，单位为 kg。
装备当前详情默认收起，点击循环部位列表后打开；部位以平滑滑动移到中间，首尾循环保持同一方向，连续点击从当前动画位置继续。
角色背包显式配置 10 kg 上限，目前不支持拖拽。
装备槽显示角色配置中的实际装备与武器属性；护甲为当前装备护甲值之和。
`SELECTED EQUIPMENT` 详情卡仅在选中背包中的装备类物品（武器或防具）时显示；未选择或选择货币、材料时隐藏。
生命值读取角色快照；力量、敏捷、背景仍显示缺省提示，不填入示例数值。
Equip / Unequip 通过会话向模拟线程提交值请求，规则与失败语义见 [资源与穿戴](design/resources.md)。
角色只有主手一个武器槽；转移、丢弃和预设操作保持禁用。UI 只读取快照，不直接修改角色配置或背包。

面板打开时 `gameplay_enabled()` 返回 false，暂停玩法输入并释放第一人称鼠标捕获。
B 的关闭操作不受 ImGui 普通键盘捕获影响，但文本输入、保存／读档窗口期间不会切换面板。

攻击时序由模拟拥有：`GameWorld` 装配时使用应用传入的资产根目录读取 CPU 动画时长，
`Character` 推进攻击进度并控制移动锁与命中窗口；快照携带归一化攻击进度，表现层按该进度采样身体和武器动画。
模拟不等待渲染播放完成回调，不创建 Vulkan 资源；详见[角色战斗规则](character.md#近战命中受击与死亡)。
