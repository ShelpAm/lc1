# 应用、游戏与引擎的代码边界

目录按职责划分，CMake 目标按依赖方向连接：`lc1` → `lc1_game` → `lc1_engine`。
游戏类型沿用现有 `lc1` 命名空间；渲染场景类型使用 `lc1::scene`，应用入口使用 `lc1::app`。
模块依赖概览见 [模块依赖图](module-dependencies.md)。

| 位置 | CMake 目标 | 职责 |
|---|---|---|
| `app/main.cpp`、`app/application.*` | `lc1` | 进程入口、窗口和 Vulkan 生命周期、输入接入、交换链和每帧调度 |
| `include/lc1/game/`、`src/game/` | `lc1_game` | 按玩法功能域划分的规则、状态、控制与表现适配代码 |
| `game/debug/pbr-showcase.*`，分别位于上述头文件和源码目录 | `lc1_game` | 当前嵌入游戏世界的 PBR 展示，供游戏和 GPU 测试共同使用 |
| 其余引擎模块，包括 `scene/`、`vk/`、窗口及资源模块 | `lc1_engine` | 场景表达、渲染和平台能力 |

`include/lc1/game/` 与 `src/game/` 使用相同的功能域子目录，便于按“要改哪个玩法”而不是
按“controller/view/model”定位文件：

| 目录 | 职责 |
|---|---|
| `character/` | `Character`、`CharacterIntent`、`CharacterController`、AI 巡逻/伴随模式、装备、武器、重力常量 |
| `player/` | 玩家动作到 intent 的解析、第一人称/斜视角状态与玩家相机跟随 |
| `camera/` | 可复用的地图/斜视角相机算法，不读取输入、不拥有角色 |
| `continent/` | 大陆数据、碰撞与查询、布局/原型/生成数据、地形绘制 |
| `party/` | 队伍、阵营和队员命令 |
| `dialog/` | 对话脚本、单场会话、入口规则与冷却 |
| `world/` | `GameWorld` 装配与更新、`WorldPresentation`、可交互对象 |
| `session/` | `GameSession` 协调以及 `Save`/序列化 |
| `ui/` | 会话内 ImGui 界面与音乐浏览器 |
| `debug/` | 嵌入世界的 PBR 展示，游戏与 GPU 测试共用 |
| `actions.hpp/.cpp` | 游戏自己的动作、context 和默认绑定；设备层是 `lc1/input.hpp` |

`actions.hpp` 没有单独建目录：它只有一组小接口，但必须和引擎的 `lc1/input.hpp` 明确区分。
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
它组合以下对象；当前仍编入 `lc1_game`，没有增加网络占位层。

| 对象 | 职责与边界 |
|---|---|
| `GameWorld` | 按存档标识和内容版本加载并拥有大陆、势力、装备、武器、队伍、角色和 `DialogSystem`；装配世界、推进规则、生成存档快照；不依赖 Window、ImGui、音频或 Vulkan |
| `WorldPresentation` | 借用只读世界，拥有地形/角色 GPU 资源、字体、持久 Scene 与音频 context/listener；更新场景实例、灯光并生成世界文字 |
| `PlayerView` | 拥有视角模式、记忆观察角和相机；生成世界空间控制意图，世界更新后再更新相机 |
| `GameUi` | 菜单、调试面板、保存反馈及屏幕文字；返回命名保存、读档请求和音量变化，由会话执行 |
| `MusicBrowser` | 音乐目录、文件浏览 UI、播放状态与音源；借用 `WorldPresentation` 的音频 context |
| `GameSession` | 读取输入、按焦点/context/ImGui capture 门控、协调各阶段、执行命名保存、向 Application 提交读档请求、直接设置窗口标题和光标捕获，并向 Application 提供退出状态和表现数据 |

`Character` 位于 `game/character/character.*`，内部唯一的 `scene::Transform` 保存脚底位置和身体朝向。
它不持有网格或材质，也不生成 `DrawItem`；`Party` 同样只处理成员、招募和命令，不负责绘制。
`WorldPresentation::update()` 将角色最终变换提交给持久 Scene 的渲染实例，玩家是角色集合的一员，不重复追加。
Application 通过 `GameSession::world()`、`world_presentation()`、`player_view()` 和 `ui()` 的只读引用
查询各自的数据；引用有效期不超过会话。会话负责更新协调、保存及反馈，并提供宽高比设置入口，
不为每个子对象查询重复提供转发方法。

角色当前共用占位模型定义（节点与动画）、GPU 网格和材质，CPU 几何在上传后释放；Scene 为每个角色拥有独立的 `ModelInstance`；
`WorldPresentation` 保存角色与场景实例的关联、选择动画并提交角色世界变换，Scene 推进动画、查询挂点并展开 DrawItem；角色仍是玩法变换的唯一权威。
模型与实例的索引及借用约定见[资源文档](resources.md#模型资源与实例)。`scene::Object` 仍可用于其他渲染场景，不再是角色世界姿态的所有者。

`DialogCatalog` 拥有 YAML 内容和状态入口规则，`Dialog` 表示一场交流，`DialogSystem` 管理
并发会话、NPC 发起和冷却。`GameWorld` 负责对话参与者注册、控制器暂停及交互动作执行；
`WorldPresentation` 和 `GameUi` 只读这些状态来生成世界/屏幕文字。
玩家名与区域/地点 HUD 也由 `GameUi::screen_labels()` 统一生成，Application 只负责提交文本绘制。
`GameSession` 在构造及每帧更新中直接调用借用的 `Window` 设置标题和光标捕获；`Window::set_title()` 缓存标题，
相同内容不调用 GLFW，返回是否发生变化供会话按需记录日志。详见[对话与招募](dialog.md)。

## 每帧时序

带模拟时间的更新接口统一把 `delta_seconds` 放在第一个参数，其余参数保持职责顺序；
例如 `GameSession::update(delta_seconds, input)`、`Character::update(delta_seconds, continent, intent)`。
`update_pointer()`、`update_camera()`、`walk()` 和 `pan()` 同样遵循此顺序；无时间参数的接口不添加占位时间。

Application 轮询事件、开始 ImGui 帧，然后调用一次 `GameSession::update()`：

1. 会话用原始 `InputState` 更新自己拥有的 `GameInputRouter`，每次会话更新只解析一次动作。
2. 根据菜单、窗口焦点、context 和 ImGui capture 计算输入门控，调用 `PlayerView::update_view_mode()` 处理视角切换、锁定与回中。
3. `PlayerView` 在切换帧丢弃旧光标模式下累计的鼠标位移，避免切入第一人称时镜头跳动。
4. `PlayerView::resolve_input()` 使用已更新的视角，把鼠标和 `GameAction` 交给 `PlayerController`；它只生成 `CharacterIntent`，不修改角色。
5. `GameWorld::update()` 先取得 intent：玩家来自 `PlayerController`，NPC 来自
   `CharacterController::update()`；随后统一调用 `Character::update(elapsed, continent, intent)`，最后处理交互和对话。
   对话暂停控制器时传入空 intent，物理仍继续。
6. `PlayerView::update_pointer()` 在鼠标门控允许时读取设备滚轮与逻辑光标坐标，
   解析边缘平移；`WorldPresentation::update()` 调用 Scene 推进模型动画、更新灯光与展示并提交世界矩阵，随后
   第一人称读取最终 `Eyes` 挂点，`PlayerView::update_camera()` 更新相机；最后
   `WorldPresentation::update_listener()` 使用最终相机位置和方向更新音频监听器。
7. `GameUi::update()` 构建 UI 并返回开始、退出、保存及音量请求；会话接通开始操作，
   会话把 F6/F9 转为打开保存/读档窗口，通过私有 `save(name)` 执行确认后的保存并反馈结果；Application 在下一帧开始处理读档并执行退出。主菜单/设置屏蔽玩法输入，世界继续模拟；返回主菜单会释放光标。
   菜单切换后的玩法门控在下一帧生效。
8. 会话在处理 UI 返回的开始、退出及音量请求后，直接调用 `Window::set_cursor_captured()`，按菜单和视角状态应用光标捕获。捕获变化由 Window 重置设备鼠标基线；
   本帧视角切换已丢弃旧位移，下一次光标位置事件建立新基线。边缘推屏仅在斜视角且窗口光标已释放时允许。

菜单之外的 ImGui 占用同样需要区分：会话读取的 capture 只表示“某个控件正在使用键盘或存在模态窗口”。
Esc 在 `game/actions.*` 中映射为 `QuitGame`；窗口有焦点且 ImGui 未占用键盘时，
会话将其转为 `quit_requested`，主菜单中也可退出。Application 统一消费退出请求。
Application 不启用 ImGui 键盘导航，并显式保留 `io.ConfigNavCaptureKeyboard = false`。导航会和角色
争用方向键与空格；默认设置下，仅被聚焦的窗口（常驻调试面板就是一例）也会让 `WantCaptureKeyboard`
每帧为真，使所有 `GameAction` 都解析为未按下。菜单与面板都由鼠标操作。

模拟时间要求有限且非负，每帧最多推进 0.1 秒；角色、对话、相机和展示使用同一份时间。
`GameWorld::update()` 在世界入口检查时间；随后执行的表现更新与相机控制器信任该前置条件，
不重复检查非有限时间，也不将非法时间静默替换为零。角色、对话及动画的独立调用入口仍验证各自输入。
当前没有固定步长或追赶长帧。角色名单按 keyid 排序，非跟随者先更新、跟随者后更新，
避免依赖 `unordered_map` 遍历顺序，也让跟随者读取本帧队长的位置。

## 生命周期与资源

Application 负责窗口/Vulkan 生命周期、原始输入接入、交换链和 ImGui 后端；
`GameSession` 拥有游戏动作路由器并在更新入口统一解析，借用非空且比会话长寿的 `Window`，
查询窗口状态并直接设置标题和光标捕获。窗口由 Application 拥有，不限制会话调用其公开操作。
Application 通过 `unique_ptr` 拥有 `GameSession`，通过临时 `Save` 提供初始数据；`GameWorld` 构造和 `load()` 时消费 Save，不保留借用。
世界可以只用 Save、大陆和对话目录构造、更新、生成快照，无需创建窗口或设备。

会话成员按 `GameWorld → WorldPresentation → PlayerView → MusicBrowser → GameUi` 构造并逆序销毁。
世界先于借用它的表现与控制器存在，音频 context 先于音源存在。角色名单在两次 `load()` 之间固定，
不支持逐个添加/删除；`load()` 在销毁角色前清空活动对话并重建名单，旧对象借用全部失效。
游戏内读档在 GPU 空闲后构造候选会话和帧资源，成功才替换，失败保留旧会话。
世界层重新加载的失败语义及表现层协调要求见[存档与装配](save-data.md)。
`GameSession`、`GameWorld`、`WorldPresentation` 和 `MusicBrowser` 禁止复制/移动。

Application 在正常/异常退出时先等 GPU 完成。`DrawItem` 借用 WorldPresentation 的资源；渲染 pass 持有注册时复制的
本帧相机、灯光与 draw 记录快照，直到图销毁；GPU 资源仍由 WorldPresentation 持有。设备和 Renderer 比会话长寿，
FrameLoop 的帧资源在会话之后构造、之前销毁。`GameLocation` 的字符串视图借用世界或静态名称。

## 构建与验证

游戏规则和表现目前仍在同一个 `lc1_game` 静态库中；这次隔离的是源码依赖与运行期构造，
未声称已提供无图形依赖的独立构建目标。引擎不得包含游戏头文件，app/game 不包含引擎私有实现。

`lc1_game_world_tests` 覆盖无窗口世界装配、交互招募、跟随、存档恢复、统一时间以及
玩家控制 → 物理 → 相机的时序。原有大陆、角色、对话和视角 CPU 测试继续运行。
GPU 渲染和窗口交互需要单独验证。动作枚举、context、默认绑定属于 `game/actions.*`；
引擎 `lc1/input.hpp` 只保留设备状态和模板路由。`lc1_input_tests` 使用自定义动作/context 验证通用路由。

## 修改功能时从哪里开始

- 新增规则或世界状态：在 `GameWorld` 及相应领域对象中实现；需要存档时同步修改记录、校验和往返测试。
- 新增表现：让 `WorldPresentation` 或 `GameUi` 从世界读取所需数据。资源所有权留在表现层，角色不持有 GPU 对象。
- 新增玩家操作：补 `game/actions.*` 的 `GameAction` 和绑定，由 `PlayerController` 解释为
  `CharacterIntent`，再由 `Character::update(delta, continent, intent)` 决定能否执行；保留捕获、输入、物理和相机的顺序。
- 新增角色规则（耐力、眩晕、攻击互斥等）：放在 `character/` 的 `Character` 或对应 gameplay
  subsystem 中，不要写进玩家或 AI 控制器；控制器只填 intent。
- 新增 AI 行为：实现 `CharacterController::update()` 并返回 `CharacterIntent`，不要直接调用
  `move`/`walk` 或修改角色状态。伴随/等待是 `AiCharacterController` 的模式，由队伍命令安装、
  由存档中的 `companion_order` 恢复。
- 新增渲染能力：先检查 [Vulkan 分层](vulkan-layers.md)与 [RenderGraph](render-graph.md)，不要让帧循环识别游戏对象或具体 pass。

上述分工让规则可以在没有窗口和设备的情况下测试，也让表现资源的生命周期集中管理。

## 未来网络边界（未实现）

服务端 `GameMode` 拥有并推进权威规则和状态。客户端 `GameClient` 负责与服务端通信，
`ClientGameState` 只保存表现与允许的预测所需的本地副本；`GameModeView` 或等价表现代码
从副本生成 UI 与渲染数据，不直接与服务端通信。客户端发送动作请求，服务端校验后发布状态结果。
当前 `GameWorld` 与 `GameSession` 不冒充上述网络对象；只有明确实施联网功能时才增加这些边界，
不预先添加占位路由或空类。

## 背包列表

进入游戏后，`GameUi` 通过现有 ImGui overlay 显示背包列表，按物品名称、数量、
占用重量分列，名称悬停显示说明及单件重量，单位为 kg。底部负重条显示背包实际总重量和负重上限。
会话向 UI 借用只读背包，按资源类型排序显示非空条目，空背包显示提示。
列表替代原有背包文字 HUD，不支持拖拽；角色背包显式配置 10 kg 上限。
