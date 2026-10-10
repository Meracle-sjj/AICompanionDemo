# 当前修订 · 1.2 候选

**状态：修订已实现，待最终独立 Windows 包验收，未发布。** [v1.2 发布页（待发布）](https://github.com/Meracle-sjj/AICompanionDemo/releases/tag/v1.2) · [玩家指南](PLAYER_GUIDE.md) · [系统设计](Docs/SYSTEM_DESIGN.md)

## 本轮修订

- 更新地图静态导航数据并重新保存，纳入候选封包；队友继续使用 NavMesh 与 MoveTo，保留正常导航约束。
- 重做测试面板布局、中文文本、视觉层级和按钮状态，整理技能及 Boss 测试入口。
- 显示设置改为同层模式按钮和分辨率列表，保留标题／暂停两入口、应用保存、取消及 ESC 返回。
- 免费初始箱改为贴近箱体的金色火焰提示，保留正常局一次免费、不扣金币、不影响付费箱价和离局清理。

完整 Windows 构建与打包已完成，Editor、导航和金焰专项已有验证；最终独立包的正常／测试移动、Q 切换、菜单输入、多分辨率、免费领取及暂停／重开实际键鼠验收尚未完成。通过后发布玩家包、PublicSource 评审源码包与 SHA-256 清单。

1.1 曾被作者报告队友无法移动等严重故障；原反馈、1.1.1 历史预期和本轮 v1.2 轨迹保留在 [GitHub #2](https://github.com/Meracle-sjj/AICompanionDemo/issues/2)。

游戏运行使用离线 FSM／Utility AI。公开源码不含 Content 资产，供代码与设计评审，不能单独完整复建。资源来源见[素材说明](Docs/THIRD_PARTY.md)。

开发周期：国庆节整个假期＋后续 3 天，约 10 天；非假期利用空闲时间开发。
