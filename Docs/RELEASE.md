# 1.0首版发布记录

版本：`1.0`，2026-10-10，北京时间。UE 5.6.1 / Windows x64 / Development。作者已授权直接发布GitHub，并将本版标记为1.0首版，后续将在实验室Windows机器试玩并反馈。1.0标号不表示独立包核心游玩、长期性能或素材许可已完成全部验收；实际上传结果由本页元数据记录。

## 下载与启动

玩家文件名为 `AICompanionDemo-Win64-v1.0.zip`，公开源码文件名为 `AICompanionDemo-source-v1.0.zip`。源码包不含Content，不能直接游玩或完整恢复地图与表现。

公开源码仓：[Meracle-sjj/AICompanionDemo](https://github.com/Meracle-sjj/AICompanionDemo)。1.0发布入口：[GitHub Releases](https://github.com/Meracle-sjj/AICompanionDemo/releases/tag/v1.0)；玩家请下载 [Windows试玩ZIP](https://github.com/Meracle-sjj/AICompanionDemo/releases/download/v1.0/AICompanionDemo-Win64-v1.0.zip)，校验文件为 `SHA256SUMS-v1.0.txt`。版本标签为 `v1.0`。源码ZIP生成后的自身字节数与SHA-256留在包外的`.snapshot.json`／`.sha256`、Release正文及远端资产记录，避免将自身哈希写回源码ZIP造成循环变化。

玩家ZIP为 **1,191,367,670 B**（约1.19 GB / 1.11 GiB），46个文件解压合计 **1,490,587,525 B**。SHA256：`24699e004d99cadb3010851ed36671e0b78011080959a3df689b4f584d707515`。`7z t`退出0，46个条目全解码SHA与归档源一致；程序与Cook数据逐SHA匹配上一候选，本次只更新 `VERSION.json`、`READ_ME.txt` 和 `PLAYER_GUIDE.md`。

完整解压后运行根目录 `AICompanionDemo.exe`，保留旁边的引擎与游戏目录。无需安装 UE 编辑器、配置 LLM API 或联网。详细操作、依赖与排障见 [玩家指南](../PLAYER_GUIDE.md)。首次缺少 Visual C++ 运行库时，包内附有官方 x64 安装程序。Development 的 UE Trace 服务可能触发 Windows 网络权限提示；离线玩法无需允许网络访问。

| 项目 | 实测结果 |
| --- | --- |
| ZIP完整性 | 1.0的`7z t`退出0，46个文件全解码SHA通过 |
| 入口地图 | `/Game/ThirdPerson/Lvl_ThirdPerson` |
| 构建／Cook／Stage／Archive | 完整 `BuildCookRun` 成功，退出 0 |
| 独立启动 | 真实启动到开始界面，封面与按钮已显示 |
| 核心游玩 | **待验**：本机Windows权限弹窗挡住输入，未完成开始、战斗、成长、死亡或Boss／终局的独立包验收；后续由作者实验室Windows异机试玩反馈 |

本次不把此前编辑器内专项验证替代独立包测试。构建参数、资源清单、日志问题和待验项目见 [验证记录](RELEASE_VALIDATION.md)。

## 版本内容与差异

1.0取自蠕虫焦痕用户验收后的真实工作树，游戏代码与内容基于本地检查点 `beece1d73316148037675d5357dbaf22dd96f845`；使用当前工作树而非旧普通HEAD导出，保护了尚未提交的最新成果。此次新增仓库说明、系统图解、玩家指南与发布脚本，清空AndroidFileServer生成令牌并关闭Win64无需的文件服务配置。没有改变战斗规则。原本地仓的HEAD、索引与开发历史保全，发布使用审核后的独立展示副本。

当前游戏运行时采用离线 C++ FSM／Utility AI，台词和语音均为固定内容。没有运行时 LLM、Embedding 或联网聊天要求，也没有玩家 API 密钥配置步骤；本次未新增智谱或其他模型调用。

原工程音乐来源记录不足以证明当前 Suno BGM 的成品分发权限。本次在**独立候选副本**清空 `BGM_Track`，明确 `/Game/BGM` 为 NeverCook；实际 Cook 目录中 BGM 文件数为 0。原项目音乐及全部用户资产保留。游戏语音与音效仍在候选中，其具体来源与尚缺的授权凭证见 [第三方资源说明](THIRD_PARTY.md)。当前不能宣称全部素材已完成公开分发清权。

## GitHub 是否装得下

GitHub Releases的单附件必须小于2GiB（2,147,483,648B）；1.0玩家ZIP的最终字节数在本页实测元数据中核对。每个Release最多1,000个附件，发布资产总量和带宽没有总额限制。[GitHub Releases官方说明](https://docs.github.com/en/repositories/releasing-projects-on-github/about-releases)

作者已授权将审核后的源码、配置、文档、图源和脚本直接推送至独立GitHub展示仓，并把玩家ZIP与SHA-256放Releases。源码附件使用 `AICompanionDemo-source-v1.0.zip`。原始Content约5.16GiB，既有`.git`约4.78GiB，含第三方裸素材和旧生成令牌历史，保留在原本地仓。公开源码包不含Content，能够审阅系统实现，无法单独恢复原地图和完整游戏表现。

普通 Git 大于 50 MiB 的文件会警告，大于 100 MiB 会拒绝；仓库理想小于 1 GB、强烈建议小于 5 GB。因此玩家 ZIP 不进入普通 Git，当前公开代码也没有启用 LFS 的必要。[GitHub 大文件官方说明](https://docs.github.com/en/repositories/working-with-files/managing-large-files/about-large-files-on-github) 原仓保全、LFS 配额、资产恢复范围和导出命令见 [仓库说明](REPOSITORY.md)。

## 首版已知限制与后续反馈

1. 作者将在实验室Windows机器测试开始→战斗／Q模式→成长选择→死亡重开，并通过测试入口检查Boss／终局资源。反馈附1.0版本、机器与复现步骤；自然整局与长期性能另记，不冒充已验。
2. 补齐 Fab 当前使用条目的取得记录及许可版本、固定 TTS 输出分发凭证；原 BGM 继续排除，直到授权链明确。源码与 Cook 成品的分发范围不同，不能公开第三方裸资产。
3. 源码和玩家包分别通过上述仓库与Releases交付；后续试玩反馈记入新的版本记录。1.0首版发布同时保留上述待验与许可缺口，不重写原本地历史，也不擅自指定开源许可证。
