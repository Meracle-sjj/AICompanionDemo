# 1.1候选验证记录

日期：2026-10-10（北京时间）。**完整Editor构建、BuildCookRun和独立候选开始页已验证；1.1尚未发布，独立包核心键鼠游玩与最终ZIP核对待验。** 当前交付状态见[发布记录](RELEASE.md)。原首版文本保留于[1.0验证归档](RELEASE_VALIDATION_1.0.md)与[1.0发布归档](RELEASE_1.0.md)，不改1.0标签或附件。

## 取证方式与范围

本轮先在[GitHub #1](https://github.com/Meracle-sjj/AICompanionDemo/issues/1)发表反馈、方案与验收条件，后接真实源码入口。以下PIE结果来自真实World、角色、导演与Slate控件回读；自动化桥调用正式经验、授予、选卡和菜单处理入口。它验证状态与处理逻辑，不等于人在独立包内按键／点击，也不是自然随机获得全部卡或自然存活12分钟的记录。

本地证据保留原始JSON、逐文件摘要与Editor／UAT日志；公开文档只摘录可核对结果，不发布机器路径与完整私人日志。最初审计捕获74份JSON；后续正常Lv.9→10和显示设置的21份原始记录另核，不能把较早审计当作后续覆盖。失败请求、编译失败和既有启动错误均保留。

## 构建、Cook与独立启动

编辑器退出后完整编译，未热重载。最终 `AICompanionDemoEditor Win64 Development` 为 `Result: Succeeded`、21.54s。首次编译因移动回读局部变量隐藏Controller成员出现C4458；修正后重编成功，不删除这次失败。

独立候选复制完整工程，不覆盖原Content；BGM槽清空并NeverCook `/Game/BGM`。使用[Build-Windows.ps1](../Tools/Build-Windows.ps1)调用已核验UE参数：

```powershell
& "$EngineRoot\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun `
  "-project=$CandidateProjectFile" -noP4 -platform=Win64 `
  -clientconfig=Development -build -cook `
  -map=/Game/ThirdPerson/Lvl_ThirdPerson -stage -pak -compressed `
  -archive "-archivedirectory=$ArchiveDirectory" `
  -prereqs -nodebuginfo -utf8output -unattended
```

Editor＋Game构建阶段163.64s；完整BuildCookRun 225.27s，`BUILD SUCCESSFUL`，AutomationTool退出0（总耗时4m0s）。Cook摘要0 errors／5 warnings，但全文仍出现既有 `M_KeyGlow` 缺失Constructor Error；不能写成全文零错误。当前KeyPickup保留原材质回退。Editor运行全文还保留该错误与两条既有 `LogAutomationTest: Error: Condition failed`，没有删日志或把局部成功替换为整轮无错误。

| Cook目录 | 非空文件数（含拆分文件） |
| --- | ---: |
| UI/UpgradeCards | 58 |
| UI/StartScreen | 2 |
| UI/EndingScreen | 2 |
| UI/BossRelics | 18 |
| Bosses/Wurm | 15 |
| Environment/Desert | 13 |
| Voice | 117 |
| BGM | **0** |

新增 `voice_playerattackunlock` 的uasset／ubulk／uexp实际为801／92,581／130 B，均非空。数量只证明Cook收录或排除，不证明每种效果、主观声音及许可通过。

真实根bootstrap启动了Development游戏进程。全新用户目录未传 `-windowed` 或分辨率覆盖，`GETALL GameUserSettings` 运行对象回读1280×720、`FullscreenMode=2`；[原始开始页截图](screenshots/1.1-package-menu.png)尺寸1280×720，中文「显示设置」入口可见。另一启动经UE正常quit退出0。**仍未在独立包中实际点击开始／测试或操作战斗、选卡、ESC、AltTab、死亡、Boss／终局和显示切换。** 不把开始页截图称为核心玩法验收。

两份候选exe签名实测仍为 `NotSigned`。没有适用可信证书；发布者提示未解决。Development的Trace网络权限提示保留为已知限制，首版TCP1985诊断见[1.0归档](RELEASE_VALIDATION_1.0.md)；不以签名或无效Trace参数宣称消除提示。

## 开局、等级队列与Flip

| 实际操作 | 回读结果 | 覆盖边界 |
| --- | --- | --- |
| 同一初始箱回调依次远距、近距、重复打开 | `false / true / false`；前后金币／付费箱计数／下箱价均为`[0,0,3]`；领取后来源StarterChest且真实三候选 | 正常局单次领取、不扣费和不推高付费价格；测试入口没有Starter。早期另一次已有等级奖励待领时打开失败，原记录保留 |
| 正常Lv.1一次实际AddXp(178) | 升到实际Lv.9，先显示Lv.2 Mixed，排队Lv.3…9；依次领取7份后当前Lv.9 Player | 发生等级随队列保存；不是按最终Level将旧奖励全部改为玩家池 |
| Lv.9奖励待领时实际AddXp(77)，再选第三槽Flip | 实际Lv.10，队列保留Lv.10 AI；当前Lv.9 Player→AI，选择代次9→11；领取后显示Lv.10 AI | Flip只改当前奖励，代次失效旧回调，不吞队列／改变后续归属 |
| 正常Lv.10奖励领取完 | 无待领奖励，流速1、鼠标隐藏 | 正常局奖励结束输入恢复 |
| 测试局实际AddXp(259) | 当前Lv.11 Player、排队Lv.12 AI；Flip保持Lv.12且代次1→3；续领Lv.12仍AI | 测试与正常等级归属一致；末尾流速1、鼠标保留以操作测试面板 |

这些操作是正式流程桥接，不是自然拾取经验、全部前置组合或随机池统计。候选不足、真实敌人全部猎印叠层／消费、Boss奖励与终局同帧排队仍需更多覆盖。

## 重复成长、说明与收益限制

实际通过正式授予桥连续应用普通猎印四次，上限3→4→5→6；再应用猎杀大师，上限仍6、每层伤害15→22。导演说明与后续HUD悬停读到真实4次／6层／22伤；HUD存在0.15s轮询，刚授予的同帧缓存可能滞后，未宣称同帧全同步。该记录没有实际打满普通敌人和Boss的全部叠层／消费场景。

本体速射共尝试十次，第八次降至0.15s、收益条件false，第九／十次保持下限；后续HUD计数8。未据此证明实际射击时间或所有自然卡池已不出无收益卡。

陪伴疾行三次为500→550→605→665.5cm/s，次数3；第四次不变、收益条件false。测试当时玩家最高步速925.75cm/s，未撞到「不超过玩家」钳制边界；源码有该限制，不能将未触及的分支标实测通过。

## 暂停、菜单与显示设置

暂停、显示设置、关闭设置、恢复调用同一GameMode处理链。奖励中实测时间保持28s、当前Lv.3／选择代次3与队列保留；恢复继续维持原奖励流速0.0001，没有以恢复动作完成奖励。应用失焦处理入口实际将paused置true；**没有物理AltTab或独立包ESC操作记录**，按钮焦点与键盘冒泡不能据回调成功视为已验。

测试面板收起、重新展开、返回菜单处理入口已调用并回读；重载后为新世界Lv.1，奖励／代次／狂暴／账本归零，标题菜单出现。一次真实GC后HUD纹理缓存／有效／绑定均9，8张卡的图标仍有效；这不是独立进程退出重启或长期GC压力验收。

显示设置调用实际选择与Apply／Confirm／Save处理入口，1280×720窗口（mode2）与全屏（mode0）均保存，关闭重开能读到配置；PIE实际viewport仍为947×603、mode2，所以只证明配置处理与保存，不证明独立全屏切换。原WindowsEditor用户配置1,122 B逐SHA-256恢复，未永久覆盖开发者设置。

失败记录也保留：曾请求设备未列出的1707×1067全屏，`selected=false`，QA在Apply前断言失败；不是Apply执行成功或游戏崩溃证据。后续选择真实支持的1280×720后通过。独立全屏、无边框、保存后进程重启和两种试玩窗口布局继续待验。

测试面板27条有非零分配的可见文字回读为分配高度＝期望高度21.51px，Boss换行43.03px，undersized=0；32个升级／操作按钮存在。总标签中零分配／滚动区外标签没有算通过。真实SHOWUI图可见中文文字与顶部入口，但含Editor界面和私人日志，不作为独立包截图发布；另一窗口尺寸与主观下缘检查仍待验。

## 事件、解锁语音与移动

Lv.6实际Tick解锁自动射击时记录一次「看来你找回了一些自己的能力」，正常／测试均读到真实 `voice_playerattackunlock` SoundWave组件 `playing=true`、时长4.44s；记录沿本局保留，没有每Tick重复。没有人耳听音确认。人工推进导演时钟720s后，回读三个Boss祭坛调度成功和WorldEnrage，累计9条事件；这不是自然12分钟生存或独立Actor数量枚举，不能称完整时序体验通过。

5s坡地移动记录共249条样本，其中221条PathFollowing处于Moving状态，平均477.64cm/s（含一个零速样本），配置最高步速500cm/s，请求计数0→1。两轮9s采样的最后1秒速度0，请求数各稳定2和4。它证明本场景路径执行、请求去重与停止回读；急反向时玩家碰到队友后停留，完整转向／追击、人眼动画和所有地形尚未通过。

运行配置实际回读 `UseAccelerationForPaths=false`、`RequestedMoveUseAcceleration=true`；本轮保留原移动双驱动，只修当前路径段与重复请求，未以采样宣称全部顿步根因消除或性能提升。

## 尚需完成的交付与复验

最终玩家／源码ZIP的大小、SHA-256、逐条解码一致性与远端匿名下载；独立包真实键鼠、ESC／失焦、全屏、战斗／成长、死亡／Boss／终局；两种窗口的文字下缘；自然开局难度与完整成长组合；主观声音、全部地形转向和长期性能均未完成。签名凭证和第三方素材取得／分发记录仍是外部限制，见[第三方说明](THIRD_PARTY.md)。

公开源码不含Content或旧Git历史，能评审实现但不能独立恢复可打包工程。日志与审计只评价明确字段和所测场景，不作未知秘密格式、全部资源、全体验或全设备的绝对保证。发布附件完成后再追加实测数据和状态，原预期与失败记录保留。
