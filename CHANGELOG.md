# 当前修订 · 1.2 候选

**状态：最新完整 Windows 构建成功，新独立包正常开局与玩家镜头外观通过；最终操作回归待完成，未发布。** [v1.2 发布页（待发布）](https://github.com/Meracle-sjj/AICompanionDemo/releases/tag/v1.2) · [玩家指南](PLAYER_GUIDE.md) · [系统设计](Docs/SYSTEM_DESIGN.md)

## 本轮修订

- 更新地图静态导航数据并重新保存，纳入候选封包；队友继续使用 NavMesh 与 MoveTo，保留正常导航约束。
- 重做测试面板布局、中文文本、视觉层级和按钮状态，整理技能及 Boss 测试入口。
- 显示设置改为同层模式按钮和分辨率列表，保留标题／暂停两入口、应用保存、取消及 ESC 返回。
- 免费初始箱改为柔和金色光晕与少量上浮光点，保留正常局一次免费、不扣金币、不影响付费箱价和离局清理。

最新光晕修订的 Editor 构建成功（24.20 秒）。受控 PIE 已核对暂停冻结、GC 后保留光晕与六个光点、一次免费领取后全部提示移除、重复领取拒绝、死亡清理和测试入口不生成初始箱。最新完整 Windows BuildCookRun 成功（101.30 秒、ExitCode=0），资源容器实际包含两份光晕材质与预制作狂暴 Niagara 系统；独立包正常开局的玩家镜头可见金环与小光点，无初始箱火柱，未出现此前复制崩溃。本包实际 E 键领取、正常／测试移动与跟随、Q 切换、菜单、多分辨率及暂停／重开仍待回归。当前未发布；通过后提供玩家包、PublicSource 评审源码包与 SHA-256 清单。

早先金色火焰方案及其运行故障补修保留在反馈轨迹中；初始箱当前方向以[柔和光晕与光点预期](https://github.com/Meracle-sjj/AICompanionDemo/issues/2#issuecomment-6098575577)为准。

1.1 曾被作者报告队友无法移动等严重故障；原反馈、1.1.1 历史预期和本轮 v1.2 轨迹保留在 [GitHub #2](https://github.com/Meracle-sjj/AICompanionDemo/issues/2)。

游戏运行使用离线 FSM／Utility AI。公开源码不含 Content 资产，供代码与设计评审，不能单独完整复建。资源来源见[素材说明](Docs/THIRD_PARTY.md)。

开发周期：国庆节整个假期＋后续 3 天，约 10 天；非假期利用空闲时间开发。
