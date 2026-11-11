# 历史设计与调查记录

这里保存特定日期的设计提案、测量结果与审查结论。正文中的“当前”“已实现”均以记录日期或段落批注为准；不能据此判断今天的代码状态。
现行说明从[文档导航](../README.md)进入。

| 记录 | 用途 | 现行说明 |
|---|---|---|
| [PBR 实施计划](pbr.md) | 了解早期材质、descriptor set 与分阶段实施方案 | [光照](../lighting.md)、[shader object](../shader-objects.md) |
| [引擎语义审查](engine-semantics-review.md) | 了解当时发现的问题、建议和后续批注；不是当前待办列表 | [代码边界](../code-layout.md)、相关源码 |
| [主场景性能定位](render-performance.md) | 查看记录配置下的测量与方法；数值不外推至当前版本 | 重新按目标场景测量 |
| [RenderGraph 验证记录](render-graph-validation-2026-10-09.md) | 保留一次 Linux GPU 验证及未完成项目 | [RenderGraph](../render-graph.md) |

- [Shader object 渲染：历史验证](shader-objects-validation.md)；现行说明：[shader-objects](../shader-objects.md)。
- [文字渲染：FreeType 字形图集与世界／界面文本：历史验证](text-rendering-validation.md)；现行说明：[text-rendering](../text-rendering.md)。
- [光照与 heap 迁移验证](lighting-validation.md)；现行说明：[lighting](../lighting.md)。
- [Ray-query 迁移验证](ray-query-validation.md)；现行说明：[ray-query-shadows](../ray-query-shadows.md)。

新增记录应注明日期、代码版本（可取得时）、环境、方法与验证边界。已结束的实验不追加到现行设计正文；仍影响代码的外部绕行保留在[外部缺陷记录](../external-defects.md)。

- [物品可选装备属性块：已替代提案](items-optional-equipment-proposal-2026-11-10.md)；现行说明：[资源与穿戴](../design/resources.md)。

- [DDGI 首版验证（2026-11-11）](ddgi-validation-2026-11-11.md)；现行说明：[光照](../lighting.md#ddgi-漫反射间接光)。
