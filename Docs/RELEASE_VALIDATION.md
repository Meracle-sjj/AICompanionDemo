# 1.0首版本地验证记录

日期：2026-10-10，北京时间。完整构建与首次本地候选ZIP完整性通过；1.0重新封包后的完整性与哈希以[发布记录](RELEASE.md)为准。独立包核心游玩仍待验，作者已决定按1.0首版直接发布，随后在实验室Windows机器试玩并反馈。

## 输入与构建

先实读当前任务书、STATUS、决策／验证记录、入口配置、代码与 Git 状态，确认最新蠕虫焦痕检查点已获作者实玩确认，另一修改聊天已结束。本次没有重复改动已验收的战斗模块。

编译前通过编辑器保存当前关卡和脏包，两次返回 true，再正常 `QUIT_EDITOR`，确认进程已退出。未使用热重载。原工作树 HEAD 与索引保持原样；候选另复制完整 Source、Config、Content 和项目声明，避免覆盖原资产或历史包。

候选与原项目差异：原配置中的 AndroidFileServer 令牌清空、插件／网络连接关闭；仅候选的 BGM 槽清空，添加 `/Game/BGM` NeverCook。入口地图保留当前 `Lvl_ThirdPerson`，模块加载阶段保留 PostDefault。

本机 UAT 源码已核对以下参数，实际执行：

```powershell
& "$EngineRoot\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun `
  "-project=$CandidateProjectFile" -noP4 -platform=Win64 `
  -clientconfig=Development -build -cook `
  -map=/Game/ThirdPerson/Lvl_ThirdPerson -stage -pak -compressed `
  -archive "-archivedirectory=$ArchiveDirectory" `
  -prereqs -nodebuginfo -utf8output -unattended
```

`BuildCookRun time: 220.34 s`；`BUILD SUCCESSFUL`；AutomationTool 总耗时 3m59s，退出 0。Editor／Game 构建、Cook、Stage、Archive 均实际执行。可重复入口与前置检查见 [Build-Windows.ps1](../Tools/Build-Windows.ps1)；候选必须包含合法取得的完整 Content。官方打包流程参考 [Epic UE5.6 文档](https://dev.epicgames.com/documentation/en-us/unreal-engine/packaging-your-project?application_version=5.6)。

## 日志与资源

Cook 摘要为 0 errors、5 warnings，但全文仍出现既有 `M_KeyGlow` 缺失 Constructor Error；摘要不能证明日志没有 Error。真实 `KeyPickup.cpp` 在缺失时回退到引擎 BasicShapeMaterial，本次未为消除日志改写现有逻辑。另有既有 Paragon Rig 警告。这些记录保留，没有删除失败日志或称无警告。

| Cook 目录 | 非空文件数（含 uasset／uexp 等拆分文件） |
| --- | ---: |
| UI/UpgradeCards | 58 |
| UI/StartScreen | 2 |
| UI/EndingScreen | 2 |
| UI/BossRelics | 18 |
| Bosses/Wurm | 15 |
| Environment/Desert | 13 |
| Voice | 114 |
| BGM | **0** |

这些数量证明列出的资源进入候选／BGM 被排除，不能证明每件资源的独立运行、主观音效或全部构筑正确。包内采用 IoStore／Pak 数据，没有将第三方源 `.uasset` 作为玩家附件公开。

## 包与启动

本次构建的随包材料为 `PLAYER_GUIDE.md`、`Docs/THIRD_PARTY.md`、`READ_ME.txt` 和 `VERSION.json`，并保留UAT附带的Visual C++运行库安装文件。压缩排除测试启动产生的Saved和PDB。首次本地候选`7z t`解码退出0；1.0随包版本与说明更新后重新封包，最终文件为 `AICompanionDemo-Win64-v1.0.zip`，字节数、SHA-256和新的完整性结果统一见[发布记录](RELEASE.md)。输出另附校验文件与逐文件完整性报告。

通过根 bootstrap 启动到真实 Development 游戏进程，开始界面、中文按钮与封面正常显示。运行日志显示 Standalone，RemoteControl Web 服务在非编辑器环境默认关闭。启动后 Windows 安全中心询问网络权限，挡住界面；没有自动处理系统安全许可，没有修改防火墙策略。

只读诊断表明 Development 引擎 TraceControl 监听 TCP 1985；它与运行时 LLM 无关。尝试 `-traceautostart=0 -notracethreading -notraceserver` 后仍有监听和提示，因此不把这些参数写成有效解决办法，也没有插入不安全的 Trace Shutdown 或改引擎来绕过弹窗。

本机测试止于上述提示。作者随后明确授权直接推送GitHub，将本版标记为1.0，并计划在实验室Windows机器试玩反馈；发布安排不再等待本机提示处理。当前尚未完成独立包中点击开始、实际移动、Q模式、自动战斗、成长选择、死亡重开、Boss与终局流程；之前的PIE专项证据单独保留，不能替代这些项目。长期性能、全部自然构筑、多分辨率和无开发环境机器运行也未验。

## 证据保全

完整 UAT／ZIP 测试日志、候选 Cook 清单、编辑器保存退出回读、仓库体积与历史审计保存在本次本地工作记录中；原始日志含机器路径，不直接放入公开源码 ZIP。公开说明只摘录真实结果，交付目录提供去除机器路径的完整性报告。

公开代码导出按白名单、不含`.git/`或`Content/`，文件名为 `AICompanionDemo-source-v1.0.zip`；出包前检查常见敏感赋值及个人目录，出包后解码每个条目核SHA-256并复验源文件未变。该审计针对明确规则，不能表述成任意未知秘密格式的绝对保证。许可凭证缺口和未验项目作为1.0首版的公开限制保留，实验室异机反馈取得后另记实际结果。
