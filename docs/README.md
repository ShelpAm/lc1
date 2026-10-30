# 文档导航

lc1 是 C++23 / Vulkan 1.4 的本地 RPG / 策略沙盒原型。已有可行走大陆与城市场景、角色碰撞、双视角、对话招募、队员命令、本地存档和 PBR 渲染。
战斗、成长、完整寻路、城市经济与网络服务端尚未实现。玩法设计中的目标不代表当前功能。

## 从任务进入

| 想做什么 | 先读 | 再查 |
|---|---|---|
| 编译、运行、测试 | [构建指南](../BUILD.zh-cn.md) | [CI](ci.md)、[开发环境排障](development-environment.md) |
| 理解整体架构、决定代码放哪 | [代码边界](code-layout.md) | [模块依赖图](module-dependencies.md) |
| 修改角色、移动或视角 | [角色与坐标修正](character.md) | [大陆与相机设计](continent.md) |
| 修改设备输入、动作绑定或焦点行为 | [输入约定](input.md) | [会话时序](code-layout.md#每帧时序) |
| 修改资源加载、缓存或卸载 | [资源管理](resources.md) | [Vulkan 分层](vulkan-layers.md) |
| 修改重力、角色碰撞或运动步长 | [角色物理](physics.md) | [角色与坐标修正](character.md) |
| 修改地图内容或空间查询 | [世界地图结构](world-map.md) | [轮廓约定](continent-outline.md)、[地图工具](map/README.md) |
| 修改对话、招募、跟随 | [对话与招募](dialog.md) | [存档格式](save-data.md) |
| 修改存档或内容数据 | [存档格式与装配](save-data.md) | [对话目录](dialog.md) |
| 理解渲染调用链 | [项目 Vulkan 入门](vulkan-minimal.md) | [Vulkan 分层](vulkan-layers.md)、[RenderGraph](render-graph.md) |
| 修改材质、光照或阴影 | [光照](lighting.md) | [ray-query 阴影](ray-query-shadows.md) |
| 修改 GPU 接口或绑定 | [shader object](shader-objects.md) | [heap 结构生成器](heap-struct-generator.md)、[heap 测试](descriptor-heap-test.md) |
| 修改字体和文字绘制 | [文字渲染](text-rendering.md) | [RenderGraph](render-graph.md) |
| 排查工具链或驱动异常 | [外部缺陷记录](external-defects.md) | [开发环境排障](development-environment.md) |
| 了解玩法与世界设定 | [核心系统草案](content.md) | [世界观](mind.md)、[大陆设计](continent.md) |
| 查旧方案、实验和审查 | [历史索引](history/README.md) | 记录注明的环境与日期 |

## 文档职责

- **当前实现**：代码边界和各子系统文档描述已接入的行为、接口、所有权及限制。模块图只作导航，不重复维护接口清单。
- **玩法与未来设计**：核心系统、世界观、大陆设计保留目标与待定事项；实现状态应单独说明。
- **历史记录**：旧计划、审查与性能结果放在 `history/`，保留当时结论，不充当现行规范。
- **工作规则**：[AGENTS.md](../AGENTS.md) 只保留全局工作规则、编码约定与任务入口；子系统约束、操作步骤和故障背景由专题文档承载。

## 维护方式

每项约定选择一篇主文档，其他页面链接过去。修改实现时同步修改该主文档；目录和导航只在职责或入口变化时更新。
文档优先解释用途、边界、设计原因和修改入口，字段与算法细节以对应格式说明或源码为准。
验证记录明确区分编译、CPU 测试、GPU 执行和平台范围；不要把某次成功写成持续成立的保证。
发现规范与实现冲突时先确认预期，再修正文档或代码，不能仅以“代码如此”撤销工程约束。
