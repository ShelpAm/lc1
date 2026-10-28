# 输入边界与事件语义

会话更新和视角切换时序以[代码边界](code-layout.md#每帧时序)为准；本文规定设备与动作层的接口。

- `Window` 拥有原始 `InputState`，不解释游戏按键。`Window`/`input.hpp` 不包含 Vulkan，输入头也不包含 GLFW。
- `lc1/input.hpp` 只提供设备状态和 `BasicInputRouter` 模板；游戏动作、context、类型别名和默认绑定属于
  `game/actions.*`。游戏通过 `GameAction` 查询动作；`GameInputContext::Ui` 刻意无绑定，用来屏蔽玩法。
  Escape 映射为 `QuitGame`，由 GameSession 请求退出、Application 结束循环。
- 路由器只解析栈顶 context，每次 update 从头计算，重复调用保持幂等。多个键绑定同一动作时，
  `released` 必须与 `!held` 相与；`pressed` 不做同样屏蔽。保留 GLFW ↔ Key 映射的完整性和唯一性断言。
- `poll_events()` 先调用 `begin_frame()` 再处理 GLFW 事件；`wait_events()` 不构成帧边界。
  游戏动作忽略 `GLFW_REPEAT`；失焦释放全部按键；捕获变化、重新聚焦、指针重新进入均重置鼠标位移基线。
- GameSession 在更新入口解析一次动作，先处理视角切换，再解析玩家 intent；切换帧丢弃观察位移。
  GameSession 根据菜单和视角状态直接调用 `Window::set_cursor_captured()`，由 Window 在捕获变化时重置基线。控制模式、焦点和 context 分别参与玩法门控，
  不能用是否捕获光标来反推输入是否启用。
- F2 按下时触发玩家单次点头，按住不重复；沿用玩法键盘输入门控。
- F6 打开命名保存，F9 打开存档选择；窗口打开时禁用玩法输入并释放第一人称光标，取消后恢复。
- GLFW 光标位置是逻辑屏幕坐标；拾取在 framebuffer 边界转换为像素坐标。
  视口尺寸来自窗口整数尺寸，不重复检查浮点有限性；边缘推屏仍检查有效尺寸、光标有限性及范围。
  地图相机接收有限的方向／滚轮和已验证的模拟时间，不在控制器中吞掉 NaN／Inf。

通用路由由 `lc1_input_tests` 验证，会话与视角时序由 `lc1_game_world_tests`、`lc1_player_view_tests` 验证。
