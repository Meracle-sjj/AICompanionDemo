/**
 * [INPUT]: 依赖 Engine 的 AActor/TimerManager/SpawnActor（0.5s 节拍与环形刷怪）、Kismet 的 GetPlayerPawn（刷怪圆心与玩家死亡监听）、本模块 EnemyGrunt（变体注入 ApplyVariant 与刷怪队列枚举）、本模块 Combatant 接口与 HealthComponent（敌人 OnDeath 绑定→全灭检测/击杀计数；玩家 OnDeath→终局）；UI/UpgradeCardView（GC保护的图文视图，视口树先于视图释放）
 * [OUTPUT]: 对外提供 AWaveDirector——L4D 导演式节奏中枢（M3.75 B2）：波次表驱动的 Break→Spawning→WaveActive→…→Finished/Failed 相位机（环形刷怪、同屏上限节流补刷、全灭晋级）+ VS 成长账本（AddXp 收集口/等级曲线，普通横排三选一/锁箱2×3六选二的图文面板接入）+ 屏幕 HUD 行（key=4：波数/活敌/击杀/等级）与事件横幅（key=5：开波/升级/胜负）+ **妖术师饱和换算（10-07 用户裁决：CountAliveWarlocks 存活计数 ≥WarlockAliveLimit(5) 时，妖术师配额整批换算为随机杂兵批次 猎犬×8/标准兵×5/蛮兵×3——后期堆积死局治理）**；升级图文卡面的候选/选择槽位/暂停状态与编辑器验收口
 *  成长契约：奖励保留来源/发生等级，Lv.9玩家/Lv.10 AI交替；玩家抽满三张强化，Flip独立按钮/F键只转换当前玩家等级奖励，不占数字槽。奖励输入统一由GameMode仲裁并恢复测试F1偏好。
 *  元素契约：冰/电/猎印三线只准一线，候选与领取复验共用当前玩家状态；六选二首领路线后将冲突未领槽改为金币袋，保留第二次领取。
 *  同批契约：64²连续探索场/33²真实地形显示；Boss死亡加入串行选择队列，九遗物由Relics组件结算；辉耀5/0.5s、每张+10，过载翻倍已有伤害；HUD稳定ID追加独特遗物与真实周期/狂暴剩余秒数。
 *  活蠕虫期间共享世界杂兵限额10/妖术师3，导演及召唤师逐只准入；超员直接退场无死亡/掉落/反馈伤害，导演自有活敌账本由OnDestroyed校正。
 *  v1.2公开只读IsRunTerminated供初始箱金焰清理；相位写入仍由导演持有。
 * [POS]: AI/ 的玩法节奏中枢；被 GameMode 代码生成，敌人类经 SetEnemyClass 注入；升级账本按稳定枚举ID计数，显式映射UI稀有度/融合标志与HUD图标快照，不让UI解析玩法描述。
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */

#pragma once

#include "CoreMinimal.h"
#include "Relics/BossRelicTypes.h"
#include "Containers/Set.h"
#include "GameFramework/Actor.h"
#include "AI/EnemyGrunt.h"
#include "Audio/SFXLibrary.h"
#include "UI/UpgradeCardPresentation.h"
#include "WaveDirector.generated.h"

class UHealthComponent;
class ACompanionAIController;
class ACompanionCharacter;
class SWidget;
class UUpgradeCardView;
class ABossWurm;

/** 单波构成：三类变体的数量配比——波次表的最小语义单元（变体语义见 EnemyGrunt::ApplyVariant） */
USTRUCT(BlueprintType)
struct FWaveDef
{
	GENERATED_BODY()

	/** 猎犬（快速低血）数量 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wave")
	int32 Rushers = 0;

	/** 标准假人数量 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wave")
	int32 Grunts = 0;

	/** 蛮兵（慢速高血压阵）数量 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wave")
	int32 Brutes = 0;
};

/** 配比茬（Endless 连续刷怪）：某时间点起的三变体权重——按存活分钟取最近一档（R10 调研：VS 敌型 ~90 秒换茬） */
USTRUCT(BlueprintType)
struct FRatioTier
{
	GENERATED_BODY()

	/** 生效时间（分钟）：存活时长跨过它即切换到本档 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Endless")
	float AtMinute = 0.f;

	/** 猎犬权重 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Endless")
	float RusherWeight = 0.f;

	/** 标准权重 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Endless")
	float GruntWeight = 1.f;

	/** 蛮兵权重 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Endless")
	float BruteWeight = 0.f;

	/** 妖术师权重（09-25 Boss 变体：缓慢巨物+蓄力射线，低权重稀有出场） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Endless")
	float WarlockWeight = 0.f;
};

UCLASS()
class AICOMPANIONDEMO_API AWaveDirector : public AActor
{
	GENERATED_BODY()

public:
	AWaveDirector();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	/** 敌人类注入（GameMode 在 spawn 后调用；未注入时刷怪期回退裸假人类） */
	void SetEnemyClass(TSubclassOf<AActor> InClass) { EnemyClass = InClass; }

	/** XP 收集口（XpPickup 经 GameMode 中枢上报）：攒满升级、曲线 ×1.4 递进——升级即进三选一暂停 */
	UFUNCTION(BlueprintCallable, Category = "Endless")
	void AddXp(int32 Amount);

	/** 金币收集口（GoldPickup 经 GameMode 中枢上报，E3）：宝箱经济的收入端 */
	UFUNCTION(BlueprintCallable, Category = "Endless")
	void AddGold(int32 Amount);

	/** 金币钱包查询（宝箱交互提示用） */
	int32 GetGold() const { return GoldCount; }

	/** 当前等级查询（09-25 玩家 Lv.6 解锁射击的解锁判据） */
	UFUNCTION(BlueprintPure, Category = "Endless")
	int32 GetUpgradeLevel() const { return Level; }

	/** 难度档（09-25 难度祭坛）：献祭 +1——敌强但经验金币同步上浮，强者主动加压换成长 */
	UFUNCTION(BlueprintPure, Category = "Endless")
	int32 GetDifficultyLevel() const { return DifficultyLevel; }

	/** 敌人血量难度乘子：1 + 0.3×档（spawn 定型时注入敌人） */
	float GetDifficultyHpMult() const { return 1.f + 0.3f * DifficultyLevel; }

	/** 掉落经验难度乘子：1 + 0.25×档 */
	float GetDifficultyXpMult() const { return 1.f + 0.25f * DifficultyLevel; }

	/** 难度档 +1（DifficultyAltar 献祭成功后调用） */
	void IncreaseDifficulty() { ++DifficultyLevel; }

	/** 免费锁箱六选二：账本零进出；当前选择未结束时由统一奖励队列续接。 */
	UFUNCTION(BlueprintCallable, Category = "Endless")
	void GrantFreeUpgrade();

	/** 初始箱仅领取一次：免费普通三选一，不改变金币/收费箱次数/后期等级轮换。 */
	UFUNCTION(BlueprintCallable, Category = "Endless")
	bool TryGrantStarterUpgrade();

	/** 宝箱开箱口（TreasureChest::TryOpen 经组合根调用，E3）：钱够即扣费并进三选一暂停，返回是否成功 */
	bool TrySpendGoldForUpgrade(int32 Cost);

	/** 下一口宝箱的价格（09-25，用户定调"开得越多越贵"对冲金币常规化）：3 + 2×已开数 */
	UFUNCTION(BlueprintPure, Category = "Endless")
	int32 GetNextChestCost() const { return BaseChestCost + ChestCostStep * ChestOpenCount; }

	/** 纯扣费口（09-25 信标等非升级消费）：钱够即扣，不进三选一——效果由消费方自证 */
	bool TrySpendGold(int32 Cost);

	/** 退款口（消费方效果未达成的兜底，如信标无队友可召） */
	void RefundGold(int32 Amount);

	/** 宝箱开启计数（09-25 价格递增的账本）：GetNextChestCost 的乘数 */
	UPROPERTY(VisibleInstanceOnly, Category = "Endless")
	int32 ChestOpenCount = 0;

	/** 难度档（09-25 难度祭坛献祭累积）：敌 Hp ×1.3^档/经验 ×1.25^档 */
	UPROPERTY(VisibleInstanceOnly, Category = "Endless")
	int32 DifficultyLevel = 0;

	/** 宝箱基础价（首箱）：MegaBonk 参考曲线——金币常规化后价格递增对冲经济通胀 */
	UPROPERTY(EditAnywhere, Category = "Endless")
	int32 BaseChestCost = 3;

	/** 宝箱每开一口的涨幅 */
	UPROPERTY(EditAnywhere, Category = "Endless")
	int32 ChestCostStep = 2;

	// ---------- HUD 快照出口（UI/CompanionHud 轮询拉取，09-21 游戏感三件套 G1） ----------
	/** 已获强化的图标快照：稳定ID/名称/次数/稀有度，展示层不解析玩法文案。 */
	struct FHudUpgradeCard
	{
		FName UpgradeId;
		FString Name;
		FString Description;   /**< 已获效果真实说明；玩法层生成，HUD仅展示。 */
		int32 Count = 1;
		EUpgradeCardRarity Rarity = EUpgradeCardRarity::Common;
		float CooldownRemaining = 0.f;
		float CooldownTotal = 0.f;   /**< 仅周期遗物使用；0表示未运行（例如玩家死亡）。 */
		float ActiveRemaining = 0.f;   /**< 实际效果Timer剩余时间，包含药剂延长。 */
	};

	/** HUD 展示数据一包带走：HUD 不认识导演内部、导演不认识 HUD——快照即两相的全部交集 */
	struct FEndlessHudSnapshot
	{
		float SurvivalSeconds = 0.f;
		float HpMultiplier = 1.f;
		int32 AliveCount = 0;
		int32 KillCount = 0;
		int32 Level = 1;
		int32 CurrentXp = 0;
		int32 XpToNext = 1;
		int32 GoldCount = 0;
		int32 BossKilledCount = 0;   /**< Boss 击杀数（09-30 三 Boss 终局战绩行） */
		bool bHasTreasureKey = false;   /**< 持钥状态（09-28：HUD 技能栏钥槽——锁箱资格可视） */
		TArray<FHudUpgradeCard> UpgradeCards;   /**< 已获强化图标，稳定ID排序与叠加次数。 */
	};

	/** 快照出口：Endless 展示数据打包（含强化账本） */
	FEndlessHudSnapshot GetHudSnapshot() const;

	/** 终局只读出口：表现组件据此清理，不能更改导演相位。 */
	bool IsRunTerminated() const { return Phase == EWavePhase::Finished || Phase == EWavePhase::Failed; }

private:
	/** 节奏相位：喘息倒计时 → 波内节流补刷 → 等全灭 → 下一波；终局 Finished（全波清空）/ Failed（玩家阵亡） */
	enum class EWavePhase : uint8
	{
		Break,
		Spawning,
		WaveActive,
		Finished,
		Failed
	};

	// ---------- C1 三选一升级 ----------

	/** 卡牌稀有度（09-25 用户定调"强化先分稀有度"）：卡面颜色 + 抽取权重乘子双联动 */
	enum class ECardRarity : uint8
	{
		Common,   /**< 普通（蓝）：数值成长与安全垫 */
		Rare,     /**< 稀有（紫）：机制类技能与火力强化 */
		Epic      /**< 传说（金）：质变技能与全部融合卡 */
	};

	/** 升级卡 id：每张卡的完整行为在 ApplyUpgrade（09-12 v3：火力特化 + 辉耀领域 + 融合进阶——
	 * 融合卡前置齐备才入池，"两种升级合体"的 mindblowing 时刻） */
	enum class EUpgradeId : uint8
	{
		Blink,             /**< 技能：解锁 Shift 闪现（割草走位灵魂） */
		Scatter,           /**< 技能：散射——三向弹道扇形，侧向 70% 伤害 */
		PierceRounds,      /**< 技能：穿透——红色贯穿光束打穿走廊，一穿多 */
		RapidFire,         /**< 火力：攻速 ×0.6（可叠加），弹道变青色 */
		HeavyRounds,       /**< 火力：伤害 ×1.5（可叠加），弹道加粗+命中爆闪 */
		Radiance,          /**< 领域：辉耀——队友 4m 光环常驻，圈内敌人持续灼烧（AI 的领域感） */
		BuddyHealing,      /**< 羁绊：贴贴疗愈（2.5m 内双方缓回） */
		GlassCannon,       /**< 契约：移速 +40% / 最大生命 -40% */
		BloodSacrifice,    /**< 契约：队友伤害 +50% / 最大生命 -20 */
		Vigor,             /**< 成长安全垫：生命上限 +30 并回满 */
		Barrage,           /**< 融合=散射+穿透：三向→五向弹道全贯穿 */
		BlinkBomb,         /**< 融合=闪烁+散射：闪现落点向八方爆射 */
		RadianceOverload,  /**< 融合=辉耀+速射：光环 6m、灼烧翻倍 */
		PrismRounds,       /**< 技能：折射（光棱坦克）——命中后弹射最近敌人，与穿透互斥（用户 2026-09-13 提案） */
		SwiftMove,         /**< 成长：疾行——玩家移速 +15%（可叠加，用户 2026-09-13 提案"速度可往上升"） */
		ExplosiveRounds,   /**< 火力：高爆弹头——命中点范围溅射+橙色大爆闪，可叠加（视觉系，用户 2026-09-13"视觉系升级太少"） */
		FrostRounds,       /**< 火力：寒霜弹——队友弹减速40%持续2s/雪花；仅本体火可消费队友冰另结等额融化 */
		RadianceBond,      /**< 融合=辉耀+贴贴（09-25 用户点名）：辉耀圈内即可贴贴互回——光之领域也是庇护所 */
		HealNova,          /**< 羁绊·回响脉冲（09-26 夜轮用户提案）：累计治疗满阈值向四周爆发治疗冲击波——回复也是武器 */
		FrostShotPlayer,   /**< 元素：寒霜弹·本体版（09-29 十二节）——本体弹路挂冰/0.5s近邻2m传播，感染者不接力；AI火消费本体冰另结等额融化 */
		ShockShot,         /**< 元素：感电弹（09-29 十二节）——玩家弹刷新感电5秒，队友每半秒配对，基础10或攻击15% */
		HuntMark,          /**< 元素：猎杀印记（09-29 十二节）——玩家弹叠金色 ◆（上限 3），AI 弹命中消耗全部层=处决 15/层 */

		// ---------- 元素深化三卡（09-30 弹药互斥配套"选线走线"：主卡解锁才入池，一次性） ----------
		ShockOverload,     /**< 感电深化：传导伤害 0.3→0.55——电弧更痛 */
		MarkMaster,        /**< 印记深化：上限 3→5、处决 15→22/层——攒爆更狠 */
		FrostDeep,         /**< 冰霜深化：减速 1.5s/0.7 → 4s/0.4——冻得更久更狠 */

		// ---------- 玩家弹道三线（09-30 用户裁决：散射/穿透/折射玩家版，与 AI 完全独立，互斥三选一） ----------
		PlayerScatter,     /**< 玩家散射：三向弹道 ±15° */
		PlayerPierce,      /**< 玩家穿透：射线贯穿走廊全伤 */
		PlayerPrism,       /**< 玩家折射：命中弹射 8m 内最近敌 70% */
		GoldPouch,         /**< 金币袋（09-30 锁箱干涸保底）：直接 +15 金 */
		PlayerRapidFire,   /**< 1.1追加：玩家射击间隔成长 */
		CompanionSwiftMove,/**< 1.1追加：队友最高步速成长，最多三次且不超过玩家 */
		Flip               /**< 1.1追加：当前玩家等级奖励转换为AI奖励，不授予技能 */
		/* DoubleJump 已摘除（09-18 用户裁决：v15 大陆无高台，卡位空出待配合类卡） */
	};

	/** 升级卡定义（卡池数据行）：Id + 分组 + 抽取权重 + 一次性键（协议组永远在池——新协议覆盖旧协议） */
	struct FUpgradeCardDef
	{
		EUpgradeId Id;
		FName Group;
		float Weight;
		FName OneShotKey;
		ECardRarity Rarity = ECardRarity::Common;   /**< 稀有度（09-25 用户定调"强化先分稀有度"）：决定卡面色与抽取权重乘子 */
	};

	enum class ERewardSource : uint8 { LevelUp, NormalChest, StarterChest, LockedChest, BossRelic, DebugPreview };
	enum class ERewardPool : uint8 { Mixed, Player, AI };
	struct FRewardChoice
	{
		ERewardSource Source = ERewardSource::NormalChest;
		int32 EarnedLevel = 0;
		int32 BossIndex = 0;
	};

	/** 入队即保留来源和发生等级，只有经验等级奖励参与后期轮换。 */
	void EnterUpgradePause(bool bChestPool = false, ERewardSource Source = ERewardSource::NormalChest, int32 RewardLevel = 0);
	void StartUpgradeReward(const FRewardChoice& Reward);
	bool CanFlipCurrentReward() const;
	void FlipCurrentReward();
	bool HasPlayerAttackSpeedBenefit() const;
	bool HasCompanionMoveSpeedBenefit() const;
	FString AcquiredCardDescription(EUpgradeId Id) const;
	static bool IsPlayerPoolCard(EUpgradeId Id);
	/** 玩家冰/电/猎印三线兼容与深化前置；抽池、提交和效果入口共用，不在拒绝时消费奖励。 */
	bool IsPlayerElementCompatible(EUpgradeId Id) const;

	/** 按权重抽三张不重复（协议组同屏至多一张；通用池摘除爆炸卡——09-27 迁移锁箱） */
	void RollOffers(bool bChestPool = false);

	/** 卡面文案（名称+分组+一句话+数值透明） */
	FString DescribeCard(EUpgradeId Id) const;

	/** 卡牌稀有度查询（09-25 W2）：Offers 只有 Id，卡面着色/文案标注经此反查 */
	ECardRarity CardRarityOf(EUpgradeId Id) const;
	static bool IsFusionLegendary(EUpgradeId Id);
	static EUpgradeCardRarity PresentationRarity(ECardRarity Rarity);

	/** 稀有度短标（◆传说/◇稀有/普通无标）：卡面前缀 */
	static const TCHAR* RarityTag(ECardRarity Rarity);

	/** 图文卡面：UpgradeCardView 渲染普通横排三选一/锁箱2×3六选二；业务与输入状态仍归导演 */
	void ShowUpgradeOverlay(bool bChest = false);
	void HideUpgradeOverlay();
	void SelectUpgradeOffer(int32 Slot);
	void ProcessNextChoice();
	void CompleteChoice();
	void ShowRelicChoice(int32 BossIndex);
	void SelectRelicOffer(int32 Slot);
	void AppendRelicHudCards(FEndlessHudSnapshot& Snapshot) const;
	TArray<FRewardChoice> PendingChoices;
	FRewardChoice ActiveReward;
	ERewardPool ActiveRewardPool = ERewardPool::Mixed;
	bool bRewardFlipped = false;
	bool bStarterUpgradeClaimed = false;
	FString OfferFallbackNote;
	bool bRelicPending = false;
	EBossRelicId RelicOffers[3];
	int32 RelicChoiceBoss = 0;
	uint32 ChoiceGeneration = 0;
	static FName UpgradeCardKey(EUpgradeId Id);
	FString UpgradeCardDescription(EUpgradeId Id) const;

	/** 应用卡片并解除暂停（协议卡写评分器权重——"runtime personality injection"的落点） */
	void ApplyUpgrade(EUpgradeId Id);

	/** 卡面短名（DescribeCard 的"｜"前段）：已获强化账本的键（09-21 G1） */
	FString CardShortName(EUpgradeId Id) const;

	/** 卡面台词（09-27 配音轮，用户定调"抽到新技能冒一句"）：队友评论式播报——台词与语音槽同源映射；空串=该卡无台词 */
	FString CardBarkOf(EUpgradeId Id, EGameSFX& OutVoice) const;

	/** 队友控制器缓存解析（ApplyUpgrade/开箱台词共用）：空则 GetActorOfClass 补缓存 */
	ACompanionAIController* ResolveCompanionCtl();

private:
	// ---------- 战争迷雾（09-28 用户立项小地图）：游戏状态归导演，UI 只读渲染 ----------

	/** 迷雾网格边长（12×12 覆盖 ±FogWorldHalfExtent 场地，v15 504m 大陆下格 ~42m） */

	/** 标记世界坐标所在格为已探索（玩家移动节拍调用） */
	void MarkExplored(const FVector& WorldPos);

	/** 迷雾网格（行主序 FogGridN²）：玩家走过点亮，重开局随导演重建 */
	UPROPERTY(Transient)
	TArray<float> ExploredGrid;
	bool bFogInitialized = false;

	// ---------- 锁箱六选二（09-30 用户裁决"大宝箱 6 选 2"） ----------

	/** 锁箱六选二卡面（bChestPool 抽 6 张入此）——纯运行时状态非 UPROPERTY（EUpgradeId 枚举定义在本类后段，UHT 前向引用受限） */
	EUpgradeId ChestOffers[6];

	/** 双选剩余次数：每 Apply 一张扣一次，扣尽才关面板解冻 */
	int32 ChestPicksRemaining = 0;
	bool bChestDoublePick = false;

	/** 按槽位记录已领取卡：鼠标/数字键统一校验，重复金币袋仍可从两个不同槽位领取 */
	TSet<int32> ChestTakenSlots;

	// ---------- 三 Boss 终局（09-30 用户定案：召唤师→巨像→蠕虫，12min 狂暴，无胜利条件） ----------

	/** Boss 祭坛出现时间点（秒）：3/6/9 分钟三档 */
	UPROPERTY(EditAnywhere, Category = "Boss", meta = (ClampMin = "30.0"))
	TArray<float> BossAltarTimes;

	/** Boss 类三档（09-30 波 2-4 子类接线：召唤师/巨像/蠕虫——此前 BossClass 单槽指基类占位=白模根因） */
	UPROPERTY(EditAnywhere, Category = "Boss")
	TArray<TSubclassOf<class ABossBase>> BossClasses;

	/** Boss 名（三档对应） */
	UPROPERTY(EditAnywhere, Category = "Boss")
	TArray<FString> BossNames;

	/** 已出祭坛档位标记 */
	UPROPERTY(Transient)
	TArray<bool> BossAltarSpawned;

	/** Boss 击杀计数（死亡屏战绩） */
	UPROPERTY(Transient)
	int32 BossKilledCount = 0;

	/** 狂暴态（12min 起：速度/血量指数换挡，不封顶） */
	bool bEnraged = false;

	/** 测试模式（09-30 调试面板）：自动刷怪/祭坛调度全停，Debug 系手动控制 */
	bool bTestMode = false;

public:
	/** 狂暴由导演统一切换；杂兵读取同一状态，包含召唤师新生杂兵。 */
	void SetEnraged(bool bInEnraged);
	UFUNCTION(BlueprintPure, Category = "Endless") bool IsEnraged() const { return bEnraged; }
	/** 出生与存量怪读取同一绝对时间倍率；14分钟阶段只免移动减速。 */
	float GetMobSpeedMultiplier() const;
	bool IsMobUnstoppable() const;
	void UpdateMobPressure();
	UFUNCTION(BlueprintCallable, Category = "Debug|Endless") void DebugSetEnraged(bool bInEnraged);
	/** 真实杂兵死亡旁路；普通刷怪与召唤杂兵都调用，蠕虫端按Health身份去重。 */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	void NotifyMobDeath(AActor* Mob);
	/** 蠕虫入场/死亡/EndPlay共用刷新；Leaving用于EndPlay尚未从世界移除的宿主。 */
	void RefreshWurmEncounter(const ABossWurm* Leaving = nullptr);
	/** 两条生产刷怪入口共用限额，按真实世界活体计数，召唤物也占位。 */
	bool CanSpawnMob(EGruntVariant Variant);
	UFUNCTION(BlueprintPure, Category = "Debug|Wurm")
	FString DebugWurmPressureState() const;
	/** 受控验收仍走正常环点/变体/登记，不提供绕限额通道。 */
	UFUNCTION(BlueprintCallable, Category = "Debug|Wurm")
	AActor* DebugSpawnMobVariant(EGruntVariant Variant);
	/** Boss 击杀上报（BossBase::HandleDeath 调用，09-30 用户实测 private 后 C2248——转公开） */
	void NotifyBossKilled(int32 BossIndex, const FString& BossName, const FVector& BossLocation);

	/** 归途门必须等待所有升级/锁箱/Boss遗物领取完。 */
	bool HasPendingRewardChoices() const { return bUpgradePending || bRelicPending || !PendingChoices.IsEmpty(); }
	/** Ending正式提交后终止节奏，移除选择树/失效旧委托；暂停和输入归GameMode。 */
	void StopForEnding();

	// ---------- 测试模式（09-30 用户立项：开始页入口，跳过正常节奏全手动——验收提速专用） ----------

	/** 测试模式开关：true=自动刷怪/祭坛调度全停（调试面板手动控制） */
	UFUNCTION(BlueprintCallable, Category = "Debug")
	void SetTestMode(bool bEnabled) { bTestMode = bEnabled; }

	/** 调试：直接 spawn 指定序号 Boss（0=召唤师 1=巨像 2=蠕虫）于玩家面前 8m——跳过祭坛流程 */
	UFUNCTION(BlueprintCallable, Category = "Debug")
	void DebugSpawnBoss(int32 BossIndex);

	/** 调试：立即刷 N 只混编怪于玩家刷怪环 */
	UFUNCTION(BlueprintCallable, Category = "Debug")
	void DebugSpawnEnemies(int32 Count);

	/** 调试：清空全场活敌（销毁即无掉落） */
	void DebugClearEnemies();

	/** 调试：直接设置等级（测试模式开局满级攻击——Lv.6 射击/全部强化可用） */
	void SetLevel(int32 TargetLevel);

	/** 调试：直接应用指定升级（跳过三选一抽卡——全卡直加） */
	void DebugApplyUpgrade(EUpgradeId Id);

	/** 调试：随机发放 N 个升级（测试模式开局免手加——RollOffers 权重抽卡+离池逻辑全复用） */
	void DebugGrantRandomUpgrades(int32 Count);

	/** 调试：按枚举序号应用升级（外部宿主转发用——EUpgradeId private，外部位转 int） */
	UFUNCTION(BlueprintCallable, Category = "Debug|UpgradeCards")
	void DebugApplyUpgradeByIdx(int32 UpgradeIndex);

	/** 编辑器验收口：按枚举连续展示正式图文卡；bGoldOnly用于验证重复保底槽位，Shipping不执行 */
	UFUNCTION(BlueprintCallable, Category = "Debug|UpgradeCards")
	void DebugPreviewUpgradeCards(int32 FirstCard = 0, bool bChest = false, bool bGoldOnly = false);

	/** 编辑器验收与正式输入共用同一槽位选择口；Shipping不执行 */
	UFUNCTION(BlueprintCallable, Category = "Debug|UpgradeCards")
	void DebugChooseUpgradeCard(int32 Slot);

	/** 编辑器验收只读状态JSON，记录实际暂停/剩余领取/已选槽位/金币；Shipping返回空对象 */
	UFUNCTION(BlueprintPure, Category = "Debug|UpgradeCards")
	FString DebugUpgradeCardState() const;

	/** 编辑器验收：通过正式等级奖励入口创建/排队，不改角色当前等级。 */
	UFUNCTION(BlueprintCallable, Category = "Debug|UpgradeCards")
	void DebugQueueLevelReward(int32 RewardLevel);
	/** 编辑器桥仅调用正式额外Flip操作，不伪造卡槽或消耗下一奖励。 */
	UFUNCTION(BlueprintCallable, Category = "Debug|UpgradeCards")
	void DebugFlipCurrentReward();
	UFUNCTION(BlueprintPure, Category = "Debug|UpgradeCards")
	FString DebugGrowthState() const;
	/** 编辑器调度fixture：只推进导演调度时间并复用正式祭坛/狂暴口，不推进刷怪预算。 */
	UFUNCTION(BlueprintCallable, Category = "Debug|Endless")
	bool DebugAdvanceScheduleTime(float DeltaSeconds);
	/** 包含Flip操作卡的展示ID数量；旧29个ID顺序保持不变。 */
	UFUNCTION(BlueprintPure, Category = "Debug|UpgradeCards")
	int32 DebugUpgradeCardCount() const { return static_cast<int32>(EUpgradeId::Flip) + 1; }
	UFUNCTION(BlueprintPure, Category = "Debug|UpgradeCards")
	FString DebugUpgradeCardLabel(int32 UpgradeIndex) const;
	UFUNCTION(BlueprintPure, Category = "Debug|Relics")
	FString DebugRelicState() const;
	UFUNCTION(BlueprintCallable, Category = "Debug|Relics")
	void DebugGrantRelic(int32 RelicIndex);


	/** 迷雾网格只读直出（SMinimapCanvas 渲染源）；返回 nullptr=未初始化 */
	const TArray<float>* GetFogReveal() const { return bFogInitialized ? &ExploredGrid : nullptr; }
	bool IsExplored(float WorldX, float WorldY) const;

	/** 迷雾/小地图共享的世界归一半径（cm）：v15 504m 沙丘大陆 ±252m——09-28 定罪：旧值 ±100m 是
	 *  100m 地板时代遗物，宝箱最远 185m 被映射出框（小地图比例失真案）。真源在此，Minimap 经 HUD 注入。 */
	static constexpr float FogWorldHalfExtent = 25200.f;
	static constexpr int32 FogGridN = 64;

	/** 世界→迷雾格索引（行主序，与 MarkExplored 同公式）——UI 侧迷雾过滤共用，防两处漂移 */
	int32 FogIndexAt(float WorldX, float WorldY) const
	{
		const int32 Ix = FMath::Clamp(FMath::RoundToInt((WorldX + FogWorldHalfExtent) / (FogWorldHalfExtent * 2.f) * (FogGridN-1)), 0, FogGridN - 1);
		const int32 Iy = FMath::Clamp(FMath::RoundToInt((WorldY + FogWorldHalfExtent) / (FogWorldHalfExtent * 2.f) * (FogGridN-1)), 0, FogGridN - 1);
		return Iy * FogGridN + Ix;
	}

	/** 贴贴疗愈（0.5s 节拍内调用）：玩家与队友 2.5m 内双方缓回——生存不是干加血，是走位去贴住队友 */
	void TickBondHeal();

	/** 辉耀领域（0.5s 节拍内调用）：队友光环可视化（青圈常驻）+ 圈内敌人持续灼烧 */
	void TickRadiance();

	/** 节拍体（0.5s 定时器）：相位机推进 + 波内补刷 + HUD 刷新——无自身 Tick（HealthComponent 同款"纯事件/定时器"风格） */
	void UpdatePhase();

	// ---------- Endless 连续刷怪 ----------

	/** Endless 节拍：存活时间累积 + 终局判定 + spawn 预算消化（间隔衰减公式 R10）+ HUD */
	void UpdateEndless();

	/** 掉队召回（Megabonk 式）：远距敌人重定位到玩家附近环点——压力永不断线 */
	void RecallStragglers();

	/** 按当前配比茬随机变体（权重累加法） */
	EGruntVariant PickEndlessVariant() const;

	/** 场上存活妖术师计数（10-07 饱和换算闸）：TActorIterator 扫活体 Warlock，0.5s 刷怪节拍下成本可忽略 */
	int32 CountAliveWarlocks() const;

	/** 已存活分钟数（成长因子的指数底） */
	float GetElapsedMinutes() const { return ElapsedSeconds / 60.f; }
	void ProcessScheduledEvents();

	/** 进入波间喘息：倒计时 InterWaveBreakSeconds 后开下一波 */
	void EnterBreak();

	/** 环上生成一只变体敌人并绑死亡回调；返回生成物（nullptr=失败，波内配额不放回） */
	AActor* SpawnEnemyAtRing(EGruntVariant Variant);

	/** 敌人阵亡回调：活敌递减 + 击杀计数（尸体 3s 存留期与活敌计数解耦——评分早已剔除死者） */
	UFUNCTION()
	void HandleEnemyDied(UHealthComponent* HealthComp);
	TSet<TWeakObjectPtr<UHealthComponent>> CountedEnemyDeaths;
	/** 直接退场只减导演登记的活体；死亡已移出登记，不会二次减数或加击杀。 */
	UFUNCTION()
	void HandleEnemyRemoved(AActor* Enemy);
	TSet<TWeakObjectPtr<AActor>> RegisteredAliveEnemies;
	void TickWurmPressure(float DeltaSeconds);
	bool bWurmPressureActive = false;
	float WurmPressureScanCountdown = 0.f;
	int32 WurmTrimmedMobs = 0;
	int32 WurmRejectedSpawns = 0;
	UPROPERTY(EditAnywhere, Category = "Wurm|Pressure", meta = (ClampMin = "1", ClampMax = "10"))
	int32 WurmMaxAliveMobs = 10;
	UPROPERTY(EditAnywhere, Category = "Wurm|Pressure", meta = (ClampMin = "0", ClampMax = "3"))
	int32 WurmMaxAliveWarlocks = 3;

	/** 玩家阵亡回调：终局 Failed（敌人 HuntPlayer 对死者自动停手，无需额外清理） */
	UFUNCTION()
	void HandlePlayerDied(UHealthComponent* HealthComp);

	/** HUD 行（key=4）：一行说清 波数/相位/活敌/击杀/等级/经验 */
	void DrawHud() const;

	// ---------- 波次配置（全 EditAnywhere，调参零编译） ----------

	/** 波次表：逐波变体配比（默认五波：快狼开路→标准主力→蛮兵压阵的威胁梯度，评分器"先打谁"全程有戏） */
	UPROPERTY(EditAnywhere, Category = "Wave")
	TArray<FWaveDef> Waves;

	/** 刷怪环半径（厘米）：以玩家当前位置为圆心环上落敌——VS 式"敌从四面八方来"，走位因此有意义 */
	UPROPERTY(EditAnywhere, Category = "Wave", meta = (ClampMin = "500.0"))
	float SpawnRingRadius = 2500.f;

	/** 波间喘息（秒）：时间化节奏（用户 2026-09-13"敌人随时间不断增强"）——近无缝推进，"波"退化为难度级 */
	UPROPERTY(EditAnywhere, Category = "Wave", meta = (ClampMin = "0.0"))
	float InterWaveBreakSeconds = 0.8f;

	/** 同屏活敌基础上限：数量压迫感的起点（随波数线性爬升——数量优先、质量随后，用户 2026-09-13 定调） */
	UPROPERTY(EditAnywhere, Category = "Wave", meta = (ClampMin = "1"))
	int32 MaxAliveEnemies = 8;

	/** 每波同屏上限增幅：第 N 波上限 = MaxAliveEnemies + N×本值，封顶 MaxAliveHardCap——数量爬坡的斜率（用户复测三 2026-09-13"AI 清太快"翻倍斜率） */
	UPROPERTY(EditAnywhere, Category = "Wave", meta = (ClampMin = "0"))
	int32 MaxAliveRampPerWave = 2;

	/** 同屏硬顶：性能与评分板的双保险 */
	UPROPERTY(EditAnywhere, Category = "Wave", meta = (ClampMin = "1"))
	int32 MaxAliveHardCap = 30;

	/** 刷怪同层容差（厘米）：候选落点地面与玩家脚下地面的 Z 差超过它即弃点换角——
	 * 09-19 实测再放宽（800→1500）：v15 出生区是 +6m 山包，Diag 采样实证 25m 环点地面 -225~-862（四周陡降深谷），
	 * 800 容差在玩家驻停高台时 100% 全灭（-game 案定罪）。地形连续无穿层结构，15m 带宽内重力+寻路皆可消化；
	 * "高而窄的死地"终审仍由 NavMesh TestPathSync 承担 */
	UPROPERTY(EditAnywhere, Category = "Wave", meta = (ClampMin = "50.0"))
	float SpawnMaxLayerDeltaZ = 1500.f;

	// ---------- 连续刷怪（2026-09-18 用户裁决"取消波次"；数值 R10 调研直套 VS/Megabonk，全可调） ----------

	/** 连续刷怪开关：true=时间驱动源源不断（现役）；false=旧波次相位机（回归安全网，D6 哲学） */
	UPROPERTY(EditAnywhere, Category = "Endless")
	bool bEndlessMode = true;

	/** 配比茬表（时间升序）：首版台阶——3 分钟蛮兵首现（尖峰①）、6 分钟蛮兵增稠（尖峰②） */
	UPROPERTY(EditAnywhere, Category = "Endless")
	TArray<FRatioTier> RatioTiers;

	/** 存活目标（秒）：撑过即胜利结算（E1 结算屏的接入点） */
	UPROPERTY(EditAnywhere, Category = "Endless", meta = (ClampMin = "30.0"))
	float SurvivalGoalSeconds = 600.f;

	/** spawn 间隔基准（秒）：每分钟 ×SpawnIntervalDecay 衰减 */
	UPROPERTY(EditAnywhere, Category = "Endless", meta = (ClampMin = "0.1"))
	float SpawnIntervalBase = 1.6f;

	/** 间隔每分钟衰减系数（R10：0.92 → 约 8 分钟触底） */
	UPROPERTY(EditAnywhere, Category = "Endless", meta = (ClampMin = "0.5", ClampMax = "1.0"))
	float SpawnIntervalDecay = 0.92f;

	/** 间隔下限（秒）：刷怪压力的物理终点 */
	UPROPERTY(EditAnywhere, Category = "Endless", meta = (ClampMin = "0.1"))
	float SpawnIntervalMin = 0.45f;

	/** 同屏上限（Endless）：骨骼体性能护栏（波次机 8 → 24，实测帧率再调；VS 是 300 顶点动画不可比） */
	UPROPERTY(EditAnywhere, Category = "Endless", meta = (ClampMin = "1"))
	int32 MaxAliveEndless = 24;

	/** 起步上限（09-20 用户反馈"前期怪太多清不掉"）：开局只给一小撮，随时间爬坡 */
	UPROPERTY(EditAnywhere, Category = "Endless", meta = (ClampMin = "1"))
	int32 MaxAliveStart = 5;

	/** 上限爬坡（每分钟）：动态上限 = min(MaxAliveEndless, MaxAliveStart + 分钟×本值)——约 8 分钟到满 */
	UPROPERTY(EditAnywhere, Category = "Endless", meta = (ClampMin = "0.0"))
	float MaxAliveRampPerMinute = 2.5f;

	/** 妖术师场上活体上限（10-07 用户裁决：后期妖术师堆积无法处理）——选中妖术师时活体已≥本值，
	 *  本次配额整批换算为随机杂兵批次（猎犬×8 / 标准兵×5 / 蛮兵×3 三选一） */
	UPROPERTY(EditAnywhere, Category = "Endless", meta = (ClampMin = "1"))
	int32 WarlockAliveLimit = 5;

	/** 掉队召回距离（厘米）：敌人远于此值（被地形卡住/追不上）即传送回玩家附近环点（Megabonk 式，09-20 用户点名）。
	 *  09-21 二审：90m 在真实移速差下要跑近 1 分钟才够线——"追兵"早成天边剪影，机制形同虚设（用户实测截图定罪）；
	 *  VS 实证语义是"出视野即回收"从不等待。
	 *  09-21 三审：50m 仍偏松（用户复测"撒丫子跑还是要等很久才触发"）——砍到 25m（刚出交战圈即回收），
	 *  距离本身已蕴含掉队时间，无需另设滞留计时 */
	UPROPERTY(EditAnywhere, Category = "Endless", meta = (ClampMin = "2000.0"))
	float StragglerRecallDistance = 2500.f;

	/** 血量成长（每分钟乘性系数）：VS 原值 1.15 直套 */
	UPROPERTY(EditAnywhere, Category = "Endless", meta = (ClampMin = "1.0"))
	float HpGrowthPerMinute = 1.15f;

	/** 速度成长（每分钟乘性系数）：VS 原值 1.025 直套 */
	UPROPERTY(EditAnywhere, Category = "Endless", meta = (ClampMin = "1.0"))
	float SpeedGrowthPerMinute = 1.025f;

	/** 速度成长封顶（倍）：防后期敌人追速崩盘 */
	UPROPERTY(EditAnywhere, Category = "Endless", meta = (ClampMin = "1.0"))
	float SpeedGrowthCap = 1.3f;

	// ---------- 成长账本（VS 循环） ----------

	UPROPERTY(VisibleInstanceOnly, Category = "Xp")
	int32 CurrentXp = 0;

	/** 升级所需经验：每升一级 ×1.4 递进；首级 5——第一波 6 XP 必升（VS 节奏：首级要快，用户 09-12 定调） */
	UPROPERTY(VisibleInstanceOnly, Category = "Xp")
	int32 XpToNextLevel = 5;

	UPROPERTY(VisibleInstanceOnly, Category = "Xp")
	int32 Level = 1;

	// ---------- 宝箱经济账本（E3） ----------

	/** 金币钱包：GoldPickup 收集入账，开宝箱消费 */
	UPROPERTY(VisibleInstanceOnly, Category = "Xp")
	int32 GoldCount = 0;

	/** 开箱消耗（金币）：TreasureChest 的默认开箱价 */
	UPROPERTY(EditAnywhere, Category = "Xp", meta = (ClampMin = "1"))
	int32 ChestUpgradeCost = 3;

	// ---------- 运行时相位状态 ----------

	TSubclassOf<AActor> EnemyClass;

	EWavePhase Phase = EWavePhase::Break;

	/** 喘息倒计时（秒）：仅 Break 相位有意义 */
	float PhaseCountdown = 0.f;

	/** 正在打的波（0 起；-1=尚未开战） */
	int32 CurrentWaveIndex = -1;

	int32 TotalWaves = 0;

	/** 本波刷怪队列（Rushers→Grunts→Brutes 顺序出队，蛮兵压阵） */
	int32 QueueRushers = 0;
	int32 QueueGrunts = 0;
	int32 QueueBrutes = 0;

	/** 同屏活敌数：spawn ++ / OnDeath --，全灭晋级与上限节流的真源 */
	int32 AliveCount = 0;

	int32 KillCount = 0;

	// ---------- Endless 连续刷怪运行时 ----------

	/** 已存活秒数（0.5s 节拍累积） */
	float ElapsedSeconds = 0.f;

	/** spawn 预算：每节拍 += kPhaseTick/间隔，攒够 1 消化一只（间隔衰减的连续化实现） */
	float SpawnBudget = 0.f;

	/** 三闸失败采样计数（诊断日志 8 取 1 的节流器） */
	int32 DiagFailCount = 0;

	/** 可达闸连续失败计数：≥16 视为"导航未就绪"降级放行（-game NavMesh 运行时重建期全灭案，一次成功即清零恢复严审） */
	int32 ConsecutiveUnreachable = 0;

	/** 掉队召回扫描倒计时（2s 一轮，节流） */
	float StragglerScanCountdown = 2.f;

	// ---------- C1 三选一运行时状态 ----------

	/** 升级暂停进行中：世界冻结（TimeDilation=0），Tick 轮询数字键 */
	bool bUpgradePending = false;

	/** 本轮三张供给 */
	EUpgradeId Offers[3] = { EUpgradeId::Vigor, EUpgradeId::Vigor, EUpgradeId::Vigor };

	/** 已抽取的一次性卡（技能/羁绊/契约；火力卡可叠加不受限） */
	TSet<FName> UnlockedIds;

	/** 已获强化账本（稳定枚举ID→次数）：HUD按ID取图与稀有度；玩法真源仍是各旗标。 */
	TMap<EUpgradeId, int32> CardTakenCounts;

	/** 贴贴疗愈已解锁（羁绊卡） */
	bool bBondHealUnlocked = false;

	/** 融合·辉耀结界已解锁（辉耀+贴贴，09-25 用户点名）：贴贴距离放宽到辉耀圈半径——圈内即互回 */
	bool bRadianceBondUnlocked = false;

	// ---------- 辉耀领域（C1 领域卡） ----------

	/** 辉耀已解锁：队友光环常驻，圈内敌人持续灼烧 */
	bool bRadianceUnlocked = false;

	/** 光环半径（厘米）：辉耀·过载融合卡 400→600 */
	UPROPERTY(EditAnywhere, Category = "Upgrade|Radiance", meta = (ClampMin = "100.0"))
	float RadianceRadius = 400.f;

	/** 每节拍灼烧伤害（0.5s 一跳） */
	UPROPERTY(EditAnywhere, Category = "Upgrade|Radiance", meta = (ClampMin = "1"))
	int32 RadianceDamagePerTick = 5;

	/** 贴贴疗愈半径（厘米） */
	UPROPERTY(EditAnywhere, Category = "Upgrade|Bond", meta = (ClampMin = "100.0"))
	float BondHealRadius = 250.f;

	/** 贴贴疗愈量（每 0.5s 节拍各回） */
	UPROPERTY(EditAnywhere, Category = "Upgrade|Bond", meta = (ClampMin = "1"))
	int32 BondHealPerTick = 1;

	/** 队友控制器缓存：协议卡写入评分器权重的落点 */
	TWeakObjectPtr<ACompanionAIController> CompanionController;

	/** 队友缓存：贴贴疗愈的距离对象 */
	TWeakObjectPtr<ACompanionCharacter> CompanionPawn;

	/** 升级卡 Slate 浮层（暂停期挂在 viewport 上，选卡即拆） */
	TSharedPtr<class SWidget> UpgradeOverlay;

	/** 视图是GC根链的一环；它持有已加载纹理与稳定Brush，EndPlay先拆Slate树再释放视图 */
	UPROPERTY(Transient)
	TObjectPtr<UUpgradeCardView> UpgradeCardView;

	FTimerHandle PhaseTimerHandle;
};
