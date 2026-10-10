# 源码仓与下载包交付

审计日期：2026-10-10（北京时间），首版版本为 **1.0**。作者已授权直接发布GitHub，后续安排实验室Windows异机试玩反馈。真实仓库／Release地址、玩家ZIP元数据与构建实测集中见[RELEASE.md](RELEASE.md)，许可逐项来源见[THIRD_PARTY.md](THIRD_PARTY.md)。

## 推荐交付结构

| 交付物 | 内容 | 用途与边界 |
| --- | --- | --- |
| 公开源码 `AICompanionDemo-source-v1.0.zip` | `Source/` 的C++／构建规则、必要`Config/`、`.uproject`、README、`Docs/`和发布脚本 | 可以评审系统与实现；不含`Content/`，不是下载后直接可玩或可完整复建的工程 |
| Windows玩家 `AICompanionDemo-Win64-v1.0.zip` | UE Cook后的程序、引擎运行依赖与游戏数据、玩家说明和版本证据 | 在GitHub Releases分发；不用下载UE编辑器；ZIP不提交普通Git或LFS |
| 私存工程快照候选 | 当前 Source、Config、完整 Content 和文档；排除缓存、构建物、个人配置 | 仅保全本地工程依赖；含第三方原始资产，不能当作公开源码附件 |
| 既有本地Git | 真实开发历史及未提交工作树 | 原样保留，不改索引／HEAD或重写历史；按作者授权从审核后的独立展示副本发布 |

发布脚本按白名单读取**当前工作树**，包括尚未提交的最新代码；不会使用 `git archive HEAD` 丢掉最新改动。现有 `.gitignore` 继续保护本地工作，公开范围由导出脚本明确决定。单纯追加 `.gitignore` 不会使已跟踪 Content 或历史资产退出 Git。

公开快照没有原始地图、OFPA 外部 Actor、角色 Blueprint、输入资产、骨骼动画、UI 图片和音频；`DefaultEngine.ini` 指向的 `/Game/ThirdPerson/Lvl_ThirdPerson` 与游戏模式 Blueprint 也属于缺失资产。复建完整视觉与流程需要原工程资产及其有效取得记录；第三方素材必须由拥有适用许可的开发者从原渠道取得。只下载源码 ZIP 无法重现本次试玩。不要用空资源回退代码推断整工程能正常运行。

不替用户选择开源许可证。项目自身源码、Epic 模板代码及第三方资产的权利边界不同；公开展示与开源许可是两个决定。源文件已有 Epic 版权头保留，不把整个项目擅自标成 MIT/CC0。

## 真实体积与历史

以下为开始整理时的磁盘测量，不是 GitHub 配额推算；新增文档与当前构建会使交付体积发生变化。最终 ZIP 以输出的 `.snapshot.json` 与 `.sha256` 为准。

| 目录 | 文件数 | 字节数 | 说明 |
| --- | ---: | ---: | --- |
| `Source/` | 196 | 1,273,156 | 模块、目标与文档链 |
| `Config/` | 5 | 90,046 | 包含无需公开的编辑器预览配置 |
| `Content/` | 3,596 | 5,541,957,643 | 约 5.16 GiB 原始 UE 资产 |
| `.git/` | 6,699 | 5,131,520,591 | 原开发历史，不加入公开快照 |
| `Saved/` | 24,821 | 20,505,940,519 | 编辑器/运行/构建输出，不是源码依赖 |
| `Intermediate/` | 1,174 | 4,954,334,655 | 编译与生成文件 |
| `Binaries/` | 14 | 851,998,377 | 本地编译物 |
| `Builds/` | 53 | 1,073,359,853 | 原有历史包；不代表最新版本 |

Content 中 `ParagonGrux` 为 3,110,644,727 B，`ParagonSevarog` 为 1,448,860,252 B，`Fab` 为 466,708,489 B，三者合计约占原 Content 的 90.7%。原始资产体积不能直接当成 Cook 后试玩包体积。

已用 `git rev-list --objects --all` 与 `git cat-file --batch-check` 读取本地可达对象：4,714 个 blob，总原始内容 5,792,227,319 B，其中 7 个 blob 超过 50 MiB，0 个超过 100 MiB。历史中还含已更名的 Fab 素材路径。`git count-objects -vH` 显示 6,661 个 loose 对象、4.77 GiB；未做 `gc`、`lfs migrate`、过滤或改写。

首次审计还发现 `Config/DefaultEngine.ini` 的 AndroidFileServer 段存在非空生成令牌；本次已清空令牌并关闭该 Win64 无需的插件/网络配置。原历史可能仍含旧值，本次既不输出其值，也不把 `.git/` 带入公开快照。公开白名单预检未命中既定凭据或个人目录规则，不代表对未知格式秘密的完整证明。

本次按作者授权从审核后的公开快照建立**独立展示仓**并直接发布，开发历史保留在现有本地仓，不重写原历史。实际远端地址和上传结果集中写入[发布记录](RELEASE.md)，核心游玩未验与素材许可缺口继续公开记录。

## GitHub 官方限制

2026-10-10 查询 GitHub 官方文档所得：

| 方式 | 官方限制/配额 | 本项目选择 |
| --- | --- | --- |
| 普通 Git 文件 | 大于 50 MiB 警告，大于 100 MiB 拒绝；网页直接上传最多 25 MiB | 仅源码与展示文档 |
| 仓库健康建议 | 理想小于 1 GB，强烈建议小于 5 GB | 原历史与裸资产不作为公开展示仓起点 |
| Releases | 每附件必须小于 2 GiB；每次 release 最多 1,000 个附件；总附件大小与带宽无总额限制 | 玩家包优先。最终包小于 2 GiB 可单 ZIP；超出时分卷，并提供合并/解压与逐卷校验 |
| Git LFS 单文件 | Free/Pro 2 GB，Team 4 GB，Enterprise Cloud 5 GB | 当前公开快照没有需要 LFS 的原始资产，不启用 LFS |
| Git LFS 免费用量 | Free/Pro 各有 10 GiB 存储与每计费周期 10 GiB 下载带宽；Team/Enterprise 各 250 GiB | 玩家下载走 Releases，避免每次下载占资产仓 LFS 配额 |

官方出处：[普通 Git 文件与仓体积](https://docs.github.com/en/repositories/working-with-files/managing-large-files/about-large-files-on-github)、[Releases 存储和带宽](https://docs.github.com/en/repositories/releasing-projects-on-github/about-releases)、[LFS 单文件限制](https://docs.github.com/en/repositories/working-with-files/managing-large-files/about-git-large-file-storage)、[LFS 计费与免费配额](https://docs.github.com/en/billing/concepts/product-billing/git-lfs)。Releases 上限采用专门 Releases 页的明确 2 GiB 数值；最终发布仍需核对目标账号状态。未购买存储或扩大付费预算。

## 重复执行导出

在 PowerShell 中从工程根执行，输出目录显式指定到本次交付目录或自己的窄目录：

```powershell
# 先生成仅含清单的候选计划，不生成 ZIP。
.\Tools\Make-ReleaseSnapshot.ps1 -OutputDirectory .\Builds\ReleaseExports -Mode PublicSource -Name AICompanionDemo-source-v1.0 -PlanOnly

# 工作树和文档冻结后生成 ZIP，并逐条解码核 SHA256。
.\Tools\Make-ReleaseSnapshot.ps1 -OutputDirectory .\Builds\ReleaseExports -Mode PublicSource -Name AICompanionDemo-source-v1.0

# 仅私存：完整工程依赖计划；不要把其结果上传为公开资产。
.\Tools\Make-ReleaseSnapshot.ps1 -OutputDirectory .\Builds\PrivateExports -Mode PrivateProject -Name AICompanionDemo-private-20261010 -PlanOnly
```

脚本只读 Git HEAD，不执行 Git stage/commit/push。不会删除或覆盖已有输出：同名、同清单、同 ZIP 哈希返回 `ReusedVerified`；同名内容变化时拒绝，请使用新的版本名。它在出包前扫描明确的令牌格式、非空敏感赋值和个人用户目录，只显示文件位置/行号/类型，发现后拒绝出包；这是有限规则审计，不是“保证没有任何秘密”。

`PublicSource` 只收入 `.h/.cpp/.cs` 源码、源码 Markdown 文档链、四个明确默认运行配置、README、玩家说明、Docs 的文档/图表/截图/生成源及两个发布/构建脚本；不收入编辑器遥控、生成语音或下载工具，不复制 `.git/`、`Content/`、原图源仓、缓存与构建物。`PrivateProject` 增加完整 Content 及默认配置，但仍不包含 Git 历史或任务书/美术源工具仓，所以它是当前游戏工程快照，不是全部个人资料备份。生成公开 ZIP 后还会复验源文件未在打包期间变化，并解码每个 ZIP 条目核对清单 SHA256。

## 环境与构建依赖

实装工程为 Unreal Engine 5.6.1、Win64 C++，本机工具链 Visual Studio 2022 / MSVC 14.44。`.uproject` EngineAssociation 为 `5.6`；模块使用 `PostDefault`，是已有 FireMid/ChaosNiagara Cook 加载顺序修复，不要为了精简发布而改回默认阶段。

插件声明包含 ModelingToolsEditorMode（Editor only）、StateTree、GameplayStateTree、RemoteControl、PCG、ChaosNiagara；Build.cs 还依赖 AI、导航、EnhancedInput、UMG/Slate、Landscape、AudioMixer、Niagara 等引擎模块。核心 AI 玩法使用 C++ FSM/Utility；插件声明不等于玩法用了所有这些框架。Epic `Variant_*` 模板源码保留，删掉可能破坏其资产类解析。

完整已授权资产工程中，先关闭编辑器，再调用引擎官方批处理编译，禁止热重载：

```powershell
# 将路径改为自己安装的 UE 和完整资产工程。
$EngineRoot = 'A:\UE_5.6'
$ProjectFile = (Resolve-Path .\AICompanionDemo.uproject).Path
& "$EngineRoot\Engine\Build\BatchFiles\Build.bat" AICompanionDemoEditor Win64 Development "-project=$ProjectFile" -WaitMutex
```

构建脚本只读配置，原工程保留的非空 BGM 槽会触发拒绝；先在独立候选副本清空 `BGM_Track` 并明确 `+DirectoriesToNeverCook=(Path="/Game/BGM")`，再执行下面的入口。它不自动改变原工程音乐：

```powershell
.\Tools\Build-Windows.ps1 -ProjectRoot 'A:\ReleaseCandidate\AICompanionDemo' -EngineRoot $EngineRoot -ArchiveDirectory 'A:\ReleaseBuilds\AICompanionDemo-20261010'
```

脚本实读 `Engine/Build/Build.version` 核 UE 5.6.1，检查编辑器已关闭、入口地图存在、AFS 令牌为空和音乐 Cook 排除规则。只向新归档目录运行已核本机官方 UAT 源的 `BuildCookRun` 参数，不覆盖旧包，并保存日志及配置 SHA256。`-PlanOnly` 只回显计划，不开工。构建成功与能启动、能完成玩法是分开的验证。

本次 RunUAT 的实际配置、许可处理副本、成功或失败日志与玩家包版本，以 [RELEASE.md](RELEASE.md) 为准。公开源码保存原工作树配置，试玩候选的音乐差异在该验证记录与第三方说明中明确；不要把旧历史包或编辑器专项 Cook 宣称为当前完整玩家包通过。
