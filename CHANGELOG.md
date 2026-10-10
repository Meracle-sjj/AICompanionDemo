# 当前修订 · 1.2 候选

**状态：2026-10-11 最新修订已接入并通过统一 Editor 编译（45.66 秒）；新独立包构建与最终回归待完成，未发布。** [v1.2 发布页（待发布）](https://github.com/Meracle-sjj/AICompanionDemo/releases/tag/v1.2) · [玩家指南](PLAYER_GUIDE.md) · [系统设计](Docs/SYSTEM_DESIGN.md)

## 本轮修订

- 更新地图静态导航数据并重新保存，纳入候选封包；队友继续使用 NavMesh 与 MoveTo，保留正常导航约束。
- 重做测试面板布局、中文文本、视觉层级和按钮状态，整理技能及 Boss 测试入口。
- 显示设置改为同层模式按钮和分辨率列表，保留标题／暂停两入口、应用保存、取消及 ESC 返回。
- 免费初始箱改为柔和紫色光晕与少量上浮光点，保留正常局一次免费、不扣金币、不影响付费箱价和离局清理。
- 玩家等级奖励提供三张真实强化卡，Flip 改为额外按钮／F 键，不占 1／2／3 卡槽；只转换当前奖励，不改变排队奖励或下次归属。
- 测试模式按 F1 切换自由点选与锁定战斗；点选时鼠标不旋转视角，暂停／奖励结束后恢复此前模式，收起面板仍显示提示。
- 特效库的跨地图裸类缓存改为 GC 安全弱缓存，失效后按原资源路径重新加载，避免再次开始时使用已回收类。
- 信标两句与 Lv.6 射击解锁一句已按原豆包参考音色重新生成并导入 UE，原播放设置保留；Cook、音色一致性与人耳试听尚未验收。

当前统一 Editor 构建为 Succeeded／45.66 秒。GC 后五类样条重建、回主菜单再开始、保留 Saved 设置的连续冷启动，以及紫光、三卡／Flip、F1 与菜单交叉路径仍待真实专项和新独立包回归；编译不代替这些验收。通过后提供玩家包、PublicSource 评审源码包与 SHA-256 清单。

2026-10-10 金色光晕版本的历史记录保留：Editor 构建 24.20 秒、受控 PIE 暂停／GC／领取／清理专项，以及完整 Windows BuildCookRun 101.30 秒／ExitCode=0 和自然玩家镜头开局曾通过。后续两次 Fatal Error 的匹配 PDB 栈指向 VfxLibrary 静态裸类缓存；该失败候选不可发行，旧首启画面不能证明重开正常。新修复的[公开根因与预期](https://github.com/Meracle-sjj/AICompanionDemo/issues/2#issuecomment-6100130600)保留完整轨迹。

早先金色火焰与金色光晕方案及其故障补修保留在反馈轨迹中；初始箱当前紫色方向、额外 Flip、F1 与语音修订见[后续实玩反馈预期](https://github.com/Meracle-sjj/AICompanionDemo/issues/2#issuecomment-6100056910)。

1.1 曾被作者报告队友无法移动等严重故障；原反馈、1.1.1 历史预期和本轮 v1.2 轨迹保留在 [GitHub #2](https://github.com/Meracle-sjj/AICompanionDemo/issues/2)。

游戏运行使用离线 FSM／Utility AI。公开源码不含 Content 资产，供代码与设计评审，不能单独完整复建。资源来源见[素材说明](Docs/THIRD_PARTY.md)。

开发周期：国庆节整个假期＋后续 3 天，约 10 天；非假期利用空闲时间开发。
