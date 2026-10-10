# UI/
> L2 | 父级: ../AGENTS.md

纯C++ UMG/Slate界面层，不建WidgetBlueprint；UUserWidget子类在RebuildWidget搭Slate，UpgradeCardView为宿主UPROPERTY持有的独立视图。展示资源包含29升级图、九遗物图和开始封面；图片与标题/参数/稀有度分离。UObject回调用CreateUObject/CreateWeakLambda，不能用要求TSharedFromThis的CreateSP；需要暂停中推进的幕布/气泡/闪屏使用真实时钟。HUD使用NativeTick传入DeltaTime做0.15s显示轮询，世界节奏仍由导演TimerManager负责。Slate路径：SProgressBar在Widgets/Notifications，SBorder/SBox在Widgets/Layout。

美术资源地图：Content/UI/UpgradeCards/AGENTS.md为29升级图，Content/UI/BossRelics/AGENTS.md为九遗物图，Content/UI/StartScreen/AGENTS.md为无字合作封面。UpgradeCardPresentation统一升级/遗物路径，字符串加载的Cook归属见各资产地图与Config/DefaultGame.ini。

成员清单
- UpgradeCardPresentation.h / .cpp: 29升级+九遗物共38条稳定ID/中文标题/分类/短句/纹理路径单一展示真源。EUpgradeCardRarity=Common/Rare/Legendary/UniqueRelic；普通蓝(.24,.62,1)、稀有紫(.72,.40,1)、传说金(1,.73,.20)、独特遗物金(1,.68,.18)。不加载资源、不决定抽卡/前置/参数；遗物完整说明由组件真实参数经宿主注入。
- HudSkillIcon.h: 纯Slate升级/操作图标；52px升级圈与56px操作圈，完整卡图缩入圈内不裁图，独特遗物另绘红内环(.88,.12,.09)。接收稳定shared brush、稀有度/锁定/冷却视觉，不拥有UObject；纹理GC所有权归HUD缓存。
- UpgradeCardView.h / .cpp: 消费FUpgradeCardOption的稳定ID/槽位/已领取/稀有度/融合资格/完整说明/可选短句/弱选择委托。普通与遗物横排三卡304×488、图272，锁箱2×3六卡304×410、图220；ScaleToFit/DownOnly适配。四套成员ButtonStyle地址稳定：普通蓝、稀有紫、传说金、独特遗物深红；独特遗物金边与金字独立于三种升级稀有度，融合传说64px金红大拇指仅Legendary且bFusionLegendary显示。已领取灰显禁点击。按ID惰性加载纹理，UPROPERTY缓存防GC、shared brush保稳定地址；完整真实说明保留tooltip，短句优先ShortDescriptionOverride再静态元数据。宿主拥有暂停/输入/合法槽位/领取数量/奖品队列与效果，释放View前必须移除视口树。
- CompanionHud.h / .cpp: 常驻存活/统计、血条迟滞层与XP、六列52px已获升级/遗物图标，Count角标与真实稀有度外圈；右下SHIFT/C/AUTO/KEY四槽保留键提示、短状态、真实冷却条与秒数。旧已获技能长说明和AI目标日志退出。0.15s轮询FHudUpgradeCard/UpgradeCards，按快照diff更新；UPROPERTY纹理+shared brush缓存不随grid重建失效。DebugSkillIconState只读Slate文本/缓存/brush绑定/重建次数，ReleaseSlateResources清显示树引用并保留纹理/heap brush缓存，旧SObjectWidget树尚可能存活，禁止过早清资源/成员style。基础闪烁/召回读数来自Character，不在HUD推进技能。不灭执念图标底部显示上取整的周期秒数，实际狂暴期间显示“狂暴Ns”并将外圈改紫，死亡显示“已停止”；秒数/持续态由组件Clock及队友真实Timer经FHudUpgradeCard注入，不参与结构diff，不按秒重建网格。DebugSkillIconState新增spirit_frenzy实际Slate文本/外环/最后快照读数。
- MinimapCanvas.h / .cpp: 纯Slate连续小图；150px常驻，真实玩家Tab按住400px。复制64²揭示场并以B-C三角顶点插值绘制软边沙色/暗雾；高度明暗与8m等高线仅来自HUD的33²Landscape采样，缺样不补造。归一范围±25200cm，82%半宽细虚线只表示高度源山环起坡。Boss亮红实心骷髅、祭坛暗红空心骷髅全图可见；近玩家16×IconScale像素内骷髅展示外移并用leader连回真实映射位置，不改世界坐标。玩家白箭头/队友青菱形/敌粉点/钥匙金钥/普通箱金框/锁箱紫框形状分家；箱子过滤在HUD使用导演同一探索场0.45阈值，钥匙/普通敌仍全显。
- CompanionHudMinimap.cpp: 同一HUD类的世界适配局部实现，扫描实际活Boss/仍在场祭坛，不按时间表推造图标；分帧最多48条WorldStatic探地，仅接受LandscapeHeightfieldCollisionComponent。33²全图首轮1089点、缺样最多再试两轮，完成后一次上传，永久缺样保留未知；不修改地形/导航/探索账本。DebugMinimapState读取最后显示快照与实测高度统计。
- DebugPanel.h / .cpp: 测试入口的手动Boss/敌人/升级和调参面板；调用GameMode/Director现役调试口，不承诺这些原生C++方法全部Python反射。
- SpeechBubble.h / .cpp: 头顶 3D 聊天气泡（09-26 夜轮，检索定稿）——Screen 空间覆盖层（WoW/ESO 名牌语义：恒定字号/不被遮挡/自动面向相机）；圆角深底+台词白字 22pt（09-27 用户实测"看不清"从引擎默认 12pt 上调——气泡是注意力焦点字号要慷慨）；**单行排版 AutoWrapText=false（09-28 碎行案：420px 容器按字硬断成「放开/我的主/人！」碎行——台词全 ≤13 字，超长词再议按标点主动断句）**；**早词缓存 PendingLine（09-28 空气泡案：CreateWidget 后 Slate 树惰性建树，开局首句 Bark 到达时 LineText 未出生被静默丢弃而挂载侧已亮底板——词落袋待 RebuildWidget 补挂完整生命周期）**；ShowLine 新词覆盖旧词+重置真实时钟（WoW 惯例不做队列）；三段状态机 FPlatformTime 驱动（浮现 0.2s 上浮 8px 淡入→停留 4.5s→淡出 0.25s），完毕 opacity 归零静默待命；装载方 CompanionCharacter（WidgetComponent 挂胶囊，挂载参数归其 BeginPlay）；纯 C++ 类 CreateWidget 零蓝图资产
- DamageFlash.h / .cpp: 受击红闪全屏层（G4，09-22）——Pulse() 即 alpha 冲顶 0.22、真实时钟 exp(-6t) 衰减 ~0.4s（世界冻结期 UMG dt=0，世界时钟不可用）；Z 序 5（HUD 上菜单下）；GameMode::NotifyPlayerHurt 脉冲入口，镜头震动在角色侧自持（弹簧臂 TargetOffset 随机偏移）
- CompanionHudBosses.cpp: 三Boss统一屏幕中央上方血条，活Actor/Health每0.15s查询，按BossIndex/路径稳定堆叠；顶边约屏幕高17%且不小于112 Slate单位，宽52%限制340–680单位并钳制窗口宽。显示真实名字、当前/总血量及Health的暂时免伤，死亡/销毁移除、无活Boss折叠；只持弱Actor，条百分比从Health读取，不拥有战斗状态。DebugBossHudState只读实际Slate文字、lane缓存几何和最后赋值百分比，不把原生SProgressBar未公开的百分比getter杜撰为已读。高层奖励/死亡/Ending菜单仍覆盖常驻HUD，血条HitTestInvisible不接输入。BossBase旧HealthBarComp保留子类/猎印定位锚点，不创建或显示头顶血条widget。
- BossHealthBar.h / .cpp: 历史09-30头顶血条类保留源码；现役BossBase不再CreateWidget/BindSource装载，真实Boss血条统一由CompanionHudBosses负责。
- DeathScreen.h / .cpp: 死亡结算全屏（G2）——真实时钟三段编排：0-1.2s 黑幕 alpha 0→0.88 渐入（SBorder SetBorderBackgroundColor）→ 1.2s "你倒下了"+存活/击杀/等级显形 → 2.0s [重新开始] 显形（防误点缓冲）；点击 = OpenLevel(GetCurrentLevelName) 全量重置（dilation 随重载归位）；Setup 注入统计（GameMode 从快照装配）
- StartMenu.h / .cpp: 封面开始页（G3）——`PrepareArtworkAndStyles` 加载 `/Game/UI/StartScreen/T_StartCover_Coop_v1.T_StartCover_Coop_v1`；`UPROPERTY(Transient) CoverTexture` 防 GC，`CoverBrush` 与三套 `FButtonStyle` 为稳定成员，随标准 `SObjectWidget` 所有权存活；按钮四态使用纯色 `FSlateColorBrush`，边框与幕底显式使用 `WhiteBrush`。`RebuildWidget` 将 1920×1080 设计画布交给 `SScaleBox(ScaleToFit/Both)` 等比适配；`SStartMenuBackdrop` 按源图 1672×941 比例取两轴缩放最小值，居中绘制完整图片，横向左侧暗幕与纵向边缘暗幕两渐变承托左侧标题、中文引导及按钮；背景叶控件 `HitTestInvisible`，不拦截点击。`2027届秋招作品`、`制作周期：整个国庆节假期`、操作说明（WASD/E/Shift 解锁）均为独立 Slate 文字。`BindHost` 注入 GameMode；开始、测试、退出仍由 `CreateUObject` 绑定 `OnStartClicked→StartRun`、`OnTestClicked→StartTestRun`、`OnQuitClicked→QuitGame` 原回调，冻结/输入/测试账本归宿主。旧自动截图已移除；`DebugActivateMenuAction` 仅在 `WITH_EDITOR`、菜单仍在视口且 Host 有效时接受 `start`/`test`，共用真实按钮回调，非编辑器构建不执行。封面缺失仅记录 `[StartMenu]` 告警，保留深色底与菜单入口。

## 世界流转协议（与 GameMode 的契约）
标题幕弹入即 SetGlobalTimeDilation(0) 冻结（不刷怪不计时）→ StartRun 解冻=1 开演 → 玩家 OnDeath → Character 布娃娃+禁操作 → ShowDeathScreen 弹死亡屏，1.5s倒地窗后由GameMode::FreezeWorld定格 → 重开=关卡重载全量归位。


2026-10-08当前责任边界：本目录只展示选择/账本/地图。导演串行奖励token、ChoiceGeneration和遗物OwnedMask防重入由玩法层持有；没有UI内的卡池或效果副本。旧29卡/HUD专项结果见A:/craziestIdea/docs/ui-feedback-integration.md，本次九遗物/连续迷雾/红骷髅/猎印的编译、真实原生视口截图、GC、暂停与死亡结果须独立记录；既有29图验收不能替代新增九图或400px输入验收。猎印World WidgetComponent位于Vfx/HuntMarkVisual，不放进HUD小图，也不使用旧Sniper/Magic姿态特效。

- EndingScreen.h/.cpp：蠕虫胜利感谢页与近门“E 进入传送门”纯提示；正式页按1920×1080画布ScaleToFit并完整显示1672×941无字背景，黑底上FPlatformTime真实时钟1秒smoothstep渐入全部图文/按钮，世界暂停仍推进；淡入期间按钮禁用且真实回调复验。独立中文感谢/真实存活击杀等级/秋招首次UE实践反馈/用户确认邮箱/返回主菜单/退出。构造SetIsFocusable(true)与宿主UIOnly焦点匹配；UPROPERTY纹理、成员Brush/Style保活。EndingPortalPrompt HitTestInvisible，不持有按钮/鼠标切换，距离/奖励/正式E松键门控归玩法层。Content/UI/EndingScreen/AGENTS.md与ConfigAlwaysCook负责背景来源/打包。

终局流转：蠕虫死亡后通知GameMode生成门，Boss遗物待领取队列仍暂停/优先；近门保留GameOnly操作，以PlayerController之后门Tick读取E资格边沿，观察松键后才接新的按下。成功后导演Finished/停Timer，GameMode撤旧HUD/调试/受击层并清屏幕战斗消息、恢复虫战镜头，EndingScreen视口显示/渐入，世界SetGamePaused(true)/输入UIOnly；重开与死亡/EndPlay清理门和提示。不以dilation0被引擎钳0.0001代替终局停止。旧批焦点/GC/停止/晚到奖励拒绝验证见combat-ending-followup-20261009.md，本批E/渐入/模式HUD证据与未验范围见指挥部STATUS及shock-wurm-mode-refinement-20261009.md。

Q模式展示：CompanionHud右下模式区0.15s读取实际Controller的CurrentMode、GuardHealingBonus、AnnihilationDamageBonus及乘数，显示护卫/歼灭和两10%作用，高亮当前模式；不持有效果或推进模式时钟。DebugCompanionModeState回读实际Slate文本与最后来源快照，ReleaseSlateResources清新显示树引用。

[PROTOCOL]: 变更时更新此头部，然后检查 ../AGENTS.md
