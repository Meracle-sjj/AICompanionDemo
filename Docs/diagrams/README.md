# 原创机制图与可维护源

四图为本项目原创规则示意，SVG保留真实文本与矢量几何；PNG用于GitHub稳定预览。前三张用 `build_diagrams.py` 生成，文字宽度审计写入 `layout-validation.json`；第四张用 `build_iteration_1_1.py`，复用前者样式。SVG生成仅用Python标准库，PNG另需Pillow及系统Microsoft YaHei或Noto Sans CJK字体；字体不随仓库分发。

| 图 | 阅读问题 | 图源 |
| --- | --- | --- |
| [核心循环](01-core-loop.png) | 自动战斗如何通过成长、资源出口与挑战时机持续产生选择 | [SVG](01-core-loop.svg) |
| [协作AI](02-companion-ai.png) | 谁提供候选信息，怎样评分，护卫／歼灭怎样改变行动 | [SVG](02-companion-ai.svg) |
| [反馈迭代](03-feedback-iteration.png) | 火球误伤反馈如何变成可见几何、实际扫掠与验收约束 | [SVG](03-feedback-iteration.svg) |
| [1.1反馈与奖励分工](04-iteration-1-1.png) | 异机反馈怎样先公开预期，奖励保存来源与发生等级后怎样轮换，Flip怎样只改本次 | [SVG](04-iteration-1-1.svg) |

实际代码与历史证据见[证据索引](../EVIDENCE.md)，三图与真实录屏的使用顺序见[110秒演示分镜](../VIDEO_SCRIPT.md)。图内数值是2026-10-10现役默认规则，不是性能数据、概率统计或最优性证明；图3的简化几何不按世界比例。

共同生成源见[build_diagrams.py](build_diagrams.py)，前三图文字宽度审计见[layout-validation.json](layout-validation.json)；第四图生成源见[build_iteration_1_1.py](build_iteration_1_1.py)，布局审计见[04-iteration-1-1-layout-validation.json](04-iteration-1-1-layout-validation.json)。README、系统说明、证据索引、视频分镜与本目录使用仓库相对路径。第四图标记1.1专项验收进行中，实际范围和后续发布状态以[验证记录](../RELEASE_VALIDATION.md)为准，机制图不替代运行截图。

视觉表达参考作者指定的[鸣潮系统拆解作品](https://github.com/Meracle-sjj/wuwa-roguelike-system-analysis)，实际查看了[图1](https://github.com/Meracle-sjj/wuwa-roguelike-system-analysis/blob/main/assets/01-mode-overview.png)及[图源说明](https://github.com/Meracle-sjj/wuwa-roguelike-system-analysis/blob/main/imagegen/README.md)。借鉴深底、金色主流、蓝色局部机制与中文标题层次；本项目内容、节点、路径和矢量图源重新制作，没有复用参考图片、角色素材或分析结论。
