# 村庄蒙皮测量（2026-11-22）

Linux Release，基线为 `d4c414a` 工作区，RTX 4060 Laptop GPU，Vulkan validation 开启。
`lc1_pbr_tests --village` 以 1200×800 和 1× MSAA 渲染三个固定视角，每个视角 20 帧。
去掉各视角前五帧后，对剩余 45 帧计算中位数。每帧 CPU 变形约 30.5 万顶点，记录 141 次动态 BLAS 构建。

| 阶段 | 中位数（毫秒） |
|---|---:|
| CPU 蒙皮和输出分配 | 7.29 |
| CPU 写入暂存缓冲 | 0.71 |
| GPU 顶点复制 | 1.85 |
| GPU 动态 BLAS 构建 | 48.74 |
| GPU TLAS 构建 | 0.32 |
| 整帧 GPU 时间 | 70.19 |

CPU 使用稳态时钟，GPU 子阶段使用临时 Vulkan 时间戳。子阶段时间包含探针和相邻命令同步的开销，
CPU 与 GPU 数值不能相加。离屏测试逐帧等待读回，数值不能直接换算为游戏帧率。
测量结果促成对每帧重建的角色 BLAS 使用 `PreferFastBuild`。蒙皮仍在 CPU 执行。
