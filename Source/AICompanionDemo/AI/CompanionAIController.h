/**
 * [INPUT]: 依赖 AIModule 的 AAIController/MoveToActor/PathFollowingComponent/AIPerception(Sight)、NavigationSystem 的 FNavigationPath（路点读取）、Engine 的 GEngine 屏幕消息与 LineTrace、Kismet 的 GetPlayerPawn、TimerManager 的节流定时器、HAL 的 FPlatformTime（台词双闸门时钟）、本模块 HealthComponent（M3 评分的威胁/残血数据面）
 * [OUTPUT]: 对外提供 ACompanionAIController——队友大脑 M3 版：Follow/Engage 双状态手写 FSM（D3）+ **双作战模式**（Guard 护卫=移动层永远跟玩家走射护主 / Berserk 战斗狂人=追击评分最优目标；Q 键 Tick 边沿切换，共享评分器与火力层——元策略层）+ 感知对齐（26m 视距全向视锥，UPROPERTY 可调，信息对等原则）+ utility 评分器 EvaluateTargets（距离/威胁/残血三维，威胁含"咬人仇恨覆盖拉满"，权重护主主导 0.6/0.3/0.1 + SwitchCost 切换惩罚）+ 伤害通道 HandlePlayerDamaged（玩家受击广播 → 凶手注入候选集）+ 屏幕评分板 DrawScoreboard（[咬] 仇恨标记，可解释北极星的核心交付）+ M3.75 生命感层：Bark 台词播报（七类事件触发一句话人格，全局+分类双冷却，头顶跟随字幕）+ 步态方向平滑（路点瞬变→VInterpTo 连续转向）+ E5 暂态狂怒攻速 ActivateFrenzy（开火间隔×0.5 弹道紫，定时自恢复——TemporaryPickup 触发）；暴露 Follow/Perception/Combat/Mode/Utility/Bark 六组 EditAnywhere 参数
 * [POS]: Source/AICompanionDemo/AI 的决策核心；驱动CompanionCharacter，面向ICombatant感知/决策/攻击；常驻AI目标调试文字已移除，决策日志与显式开启的评分板保留。
 *  狂暴读真实FrenzyTimer；Q被动由Health乘一次，弹路显示实际扣血。感电每端基础20、过载40与比例成长取高；Nova命中后继承玩家状态及独立地刺口。
 * [PROTOCOL]: 变更时更新此头部，然后检查 ../AGENTS.md
 */

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "Audio/SFXLibrary.h"   // EGameSFX 完整定义——Bark 默认参数访问 None 成员，前向声明不够（C2027 实证）
#include "CompanionAIController.generated.h"

class UAISenseConfig_Sight;
class ICombatant;
class UHealthComponent;
class AAICompanionDemoCharacter;

/** 队友 FSM（D3 决策：C++ 手写状态机，拒绝 BT/StateTree 黑盒）——M2 两态起步，M3 生长 */
UENUM(BlueprintType)
enum class ECompanionState : uint8
{
	Follow,   /**< 无感知敌人：跟随玩家 */
	Engage    /**< 有感知敌人：接近至射程 + 视线射击 */
};

/** 队友作战模式（2026-09-05 用户定调：战斗狂人不是错误而是取舍——Q 键切换，乐趣来自元策略层） */
UENUM(BlueprintType)
enum class ECompanionMode : uint8
{
	Guard,    /**< 护卫：移动层永远跟随玩家，走射护主 */
	Berserk   /**< 歼灭：移动层追击评分最优目标，全队友伤害增加10% */
};

/** M3 utility 评分的候选快照：三维分数与加权总分（纯值语义，随每次决策刷新） */
struct FTargetScore
{
	AActor* Enemy = nullptr;
	float DistanceScore = 0.f;      /**< 距离分：队友越近越高——输出路径短、转火快 */
	float ThreatScore = 0.f;        /**< 威胁分：距离近似潜在威胁；正在咬玩家的敌人仇恨覆盖拉满（护主第一性） */
	float VulnerabilityScore = 0.f; /**< 状态分：目标越残血越高——收割终结（低权重甜点，防正反馈锁死） */
	bool bRetaliating = false;      /**< 仇恨标记：该敌正在贴脸攻击玩家（评分板 [咬] 标记的来源） */
	float Total = 0.f;              /**< 加权总分：W_D×D + W_T×T + W_V×V − SwitchCost（候选≠现目标时） */
};

UCLASS()
class AICOMPANIONDEMO_API ACompanionAIController : public AAIController
{
	GENERATED_BODY()

public:
	ACompanionAIController();
	virtual void BeginPlay() override;
	virtual void OnPossess(APawn* InPawn) override;
	virtual void Tick(float DeltaSeconds) override;

	/** 当前作战模式：Q 键切换（Tick 边沿检测），护卫/战斗狂人两种移动策略——共享评分器与火力层 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Companion|Mode")
	ECompanionMode CurrentMode = ECompanionMode::Guard;

	/** 切换作战模式（Guard <-> Berserk）：日志与队友播报反馈。 */
	UFUNCTION(BlueprintCallable, Category = "Companion|Mode")
	void SwitchMode();
	UPROPERTY(EditAnywhere, Category="Companion|Mode", meta=(ClampMin="0.0", ClampMax="2.0"))
	float GuardHealingBonus = .1f;
	UPROPERTY(EditAnywhere, Category="Companion|Mode", meta=(ClampMin="0.0", ClampMax="2.0"))
	float AnnihilationDamageBonus = .1f;
	/** 被动数值只供Health唯一入口消费，弹道/领域各入口不得再次乘算。 */
	UFUNCTION(BlueprintPure, Category="Companion|Mode")
	float GetModeDamageMultiplier() const { return CurrentMode == ECompanionMode::Berserk ? 1.f + FMath::Clamp(AnnihilationDamageBonus, 0.f, 2.f) : 1.f; }
	UFUNCTION(BlueprintPure, Category="Companion|Mode")
	float GetPlayerHealingMultiplier() const { return CurrentMode == ECompanionMode::Guard ? 1.f + FMath::Clamp(GuardHealingBonus, 0.f, 2.f) : 1.f; }

	/** 距玩家多近算"到位"（厘米）：到位即停喂输入，加速度归零回待机动画 */
	UPROPERTY(EditAnywhere, Category = "Companion|Follow", meta = (ClampMin = "50.0"))
	float FollowAcceptanceRadius = 150.f;

	/** 决策节流间隔（秒）：感知集合→状态→移动/开火的刷新频率上限 */
	UPROPERTY(EditAnywhere, Category = "Companion|Follow", meta = (ClampMin = "0.05"))
	float FollowUpdateInterval = 0.25f;

	/** 视距（厘米）：与第三人称玩家屏幕可辨敌人的范围对齐——信息对等原则（AI 看不见的，玩家看得见，决策必违背预期） */
	UPROPERTY(EditAnywhere, Category = "Companion|Perception", meta = (ClampMin = "500.0"))
	float SightRange = 2600.f;

	/** 丢失视距（厘米）：> 视距形成滞回带，防边界闪烁 */
	UPROPERTY(EditAnywhere, Category = "Companion|Perception", meta = (ClampMin = "500.0"))
	float LoseSightRange = 3000.f;

	/** 开火间隔（秒）：伤害节拍 */
	UPROPERTY(EditAnywhere, Category = "Companion|Combat", meta = (ClampMin = "0.1"))
	float AttackInterval = 0.8f;

	/** 评分板总开关：默认关（09-18 用户裁决"割草游戏显示敌人得分没有意义"——观感干净优先；面试叙事/调试时一键开回，M4 调试面的一员） */
	UPROPERTY(EditAnywhere, Category = "Companion|Debug")
	bool bEnableScoreboard = false;

	/** 火力射程（厘米）：射程内+有视线的目标被 FireTimer 自动输出（两模式共用的火力层判定） */
	UPROPERTY(EditAnywhere, Category = "Companion|Combat", meta = (ClampMin = "200.0"))
	float AttackRange = 800.f;

	/** 狂人逼近线（厘米）：Berserk 模式下高于此距离边逼近边开火，低于站定输出——Guard 模式不使用 */
	UPROPERTY(EditAnywhere, Category = "Companion|Combat", meta = (ClampMin = "100.0"))
	float EngageApproachDist = 350.f;

	/** 单发伤害：M2 唯一伤害出口（全项目走 HealthComponent） */
	UPROPERTY(EditAnywhere, Category = "Companion|Combat", meta = (ClampMin = "1.0"))
	float AttackDamage = 10.f;

	/** 两端传导采用max(基础伤害,本弹原始伤害×比例)，模式只在Health乘一次；过载基础20→40、比例.3→.55。 */
	UPROPERTY(EditAnywhere, Category = "Companion|Element", meta = (ClampMin = "0.0"))
	float ConductionBaseDamage = 20.f;
	/** 高火力下保留比例成长，低伤或衰减弹也不会把默认20点传导缩成零头。 */
	UPROPERTY(EditAnywhere, Category = "Companion|Element", meta = (ClampMin = "0.05", ClampMax = "2.0"))
	float ConductionScale = 0.3f;
	/** 两个已带电活敌的配对距离；未找到配对时保留标记至自然到期。 */
	UPROPERTY(EditAnywhere, Category="Companion|Element", meta=(ClampMin="50.0"))
	float ConductionRadius = 400.f;

	/** 处决每层伤害（猎杀印记消耗结算）——MarkMaster 深化 15→22 */
	UPROPERTY(EditAnywhere, Category = "Companion|Element", meta = (ClampMin = "1.0"))
	float ExecuteDamagePerMark = 15.f;

	// ---------- M3.75 C1 升级卡效果（UpgradeDirector 抽卡写入）——火力特化，感知优先 ----------

	/** 穿透弹体（技能卡）：红色贯穿光束从眼位打穿目标走廊，走廊内所有敌人受衰减伤害——一条整线的贯穿感 */
	UPROPERTY(EditAnywhere, Category = "Companion|Upgrade")
	bool bPierceUnlocked = false;

	/** 穿透伤害衰减系数 */
	UPROPERTY(EditAnywhere, Category = "Companion|Upgrade", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float PierceDamageScale = 0.7f;

	/** 走廊命中带宽（09-26 三审，用户实测"穿透打不到多个人"定罪 35cm 球扫管道太细）：数学走廊半径（厘米）——
	 *  普通弹=直击微放宽；穿透弹=真·走廊（扇形散布的围杀阵也能一线贯穿）。胸前判点，PointDistToSegment 判距 */
	UPROPERTY(EditAnywhere, Category = "Companion|Upgrade", meta = (ClampMin = "20.0"))
	float ShotCorridorRadius = 60.f;

	/** 穿透走廊带宽（厘米）：130 ≈ 一条小街的宽度，两个敌人横排站位也能一线双穿 */
	UPROPERTY(EditAnywhere, Category = "Companion|Upgrade", meta = (ClampMin = "60.0"))
	float PierceCorridorRadius = 130.f;

	/** 折射弹体（技能卡，光棱坦克——与穿透互斥）：命中后向最近敌人弹射（用户 2026-09-13 提案） */
	UPROPERTY(EditAnywhere, Category = "Companion|Upgrade")
	bool bPrismUnlocked = false;

	/** 折射弹射半径（厘米）：命中点向该范围内最近的下一个敌人弹射 */
	UPROPERTY(EditAnywhere, Category = "Companion|Upgrade", meta = (ClampMin = "100.0"))
	float PrismBounceRadius = 800.f;

	/** 折射弹射跳数（命中首目标后额外弹射次数） */
	UPROPERTY(EditAnywhere, Category = "Companion|Upgrade", meta = (ClampMin = "0"))
	int32 PrismBounces = 2;

	/** 折射每跳伤害衰减系数 */
	UPROPERTY(EditAnywhere, Category = "Companion|Upgrade", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float PrismBounceDecay = 0.7f;

	/** 命中链统一入口（09-24 弹道融合，用户定调"弹道和弹道之间应该有融合"）：溅射+寒霜——
	 *  直击/穿透段/折射跳/闪现轰击全部经此触发；链内溅射伤害不再递归触发链（无雪崩） */
	void ApplyHitChain(AActor* DirectVictim, UHealthComponent* DirectHealth, const FVector& ChainPoint, float ChainScale, AActor* InstigatorPawn);
	/** 仅AI子弹实际直击调用；传导伤害不递归本入口，辉耀/遗物不会触发。 */
	void TriggerShockConduction(AActor* DirectVictim, UHealthComponent* DirectHealth, float ChainScale, AActor* InstigatorPawn);

	/** 高爆弹头（火力卡，视觉系）：命中点范围溅射 + 橙色大爆闪——群杂的葬礼（用户 2026-09-13 提案） */
	UPROPERTY(EditAnywhere, Category = "Companion|Upgrade")
	bool bExplosiveUnlocked = false;

	/** 爆炸溅射半径（厘米）：每抽一张 +100（09-25 收敛：+100→+80——多抽叠加的半径滚雪球是"太厉害"主因之一） */
	UPROPERTY(EditAnywhere, Category = "Companion|Upgrade", meta = (ClampMin = "50.0"))
	float ExplosionRadius = 250.f;

	/** 爆炸溅射伤害系数：09-25 削弱（用户实测"爆炸太厉害"）——0.5→0.35、每抽 +0.2→+0.12；传说稀有度对冲 */
	UPROPERTY(EditAnywhere, Category = "Companion|Upgrade", meta = (ClampMin = "0.1", ClampMax = "2.0"))
	float ExplosionDamageScale = 0.35f;

	/** 寒霜弹（火力卡，控制系视觉）：命中使敌人减速染蓝（用户 2026-09-13 提案） */
	UPROPERTY(EditAnywhere, Category = "Companion|Upgrade")
	bool bFrostUnlocked = false;

	/** 散射（技能卡）：每发分裂三向弹道（主方向 ±15°），侧向受衰减——密集杂兵的清场爽感 */
	UPROPERTY(EditAnywhere, Category = "Companion|Upgrade")
	bool bScatterUnlocked = false;

	/** 散射侧向伤害系数 */
	UPROPERTY(EditAnywhere, Category = "Companion|Upgrade", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float ScatterDamageScale = 0.7f;

	/** 融合·弹幕风暴（散射+穿透）：散射进化五向弹道（±12.5°/±25°）全贯穿 */
	UPROPERTY(EditAnywhere, Category = "Companion|Upgrade")
	bool bBarrageUnlocked = false;

	/** 重击弹体（火力卡）：伤害已由 AttackDamage 承载；弹道加粗 + 命中爆闪——数值提升的可视化锚点 */
	UPROPERTY(EditAnywhere, Category = "Companion|Upgrade")
	bool bHeavyRoundsUnlocked = false;

	/** 速射形态（火力卡）：AttackInterval 已压缩；弹道变青色——射速提升的可视化锚点 */
	UPROPERTY(EditAnywhere, Category = "Companion|Upgrade")
	bool bRapidFireUnlocked = false;

	// ---------- M3 utility 权重（评分器的可调面） ----------

	/** 距离分权重：队友自身到目标的接近度在决策中的话语权（辅助项——护主不被队友便利绑架） */
	UPROPERTY(EditAnywhere, Category = "Companion|Utility", meta = (ClampMin = "0.0"))
	float WeightDistance = 0.3f;

	/** 威胁分权重：目标威胁度在决策中的话语权——护主主导项（咬人仇恨覆盖 + 距离近似），刻意压过距离分 */
	UPROPERTY(EditAnywhere, Category = "Companion|Utility", meta = (ClampMin = "0.0"))
	float WeightThreat = 0.6f;

	/** 残血分权重：目标已损失血量占比在决策中的话语权——收割终结（刻意低配：残血分有正反馈，高配会锁死第一个目标） */
	UPROPERTY(EditAnywhere, Category = "Companion|Utility", meta = (ClampMin = "0.0"))
	float WeightVulnerability = 0.1f;

	/** 切换惩罚：非现目标候选的总分抵扣——横向跳动的滞回带，0 即无记忆的纯评分 */
	UPROPERTY(EditAnywhere, Category = "Companion|Utility", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SwitchCost = 0.1f;

	/** 威胁判定半径（厘米）：敌人与玩家距离归一化的基准圈 */
	UPROPERTY(EditAnywhere, Category = "Companion|Utility", meta = (ClampMin = "100.0"))
	float ThreatRange = 1200.f;

	// ---------- M3.75 玩法强化（A4 步态平滑 / A1 队友 Bark） ----------

	/** 步态转向速度（秒^-1）：喂入方向向目标方向指数插值的速率——路点切换的方向瞬变在此磨平（STATUS 待办①清账）；调大回退为瞬变。
	 * v15 坡地调优：6 在坡地锯齿路径上把高频折向磨成糊状慢弯（体感"变慢"），10 保持敏捷 */
	UPROPERTY(EditAnywhere, Category = "Companion|Follow", meta = (ClampMin = "1.0"))
	float FeedDirTurnSpeed = 10.f;

	/** 台词总开关：Bark 生命感表现层的运行时开关（M4 调试开关面的一员） */
	UPROPERTY(EditAnywhere, Category = "Companion|Bark")
	bool bEnableBarks = true;

	/** 台词停留时长（秒）：屏幕消息存续 */
	UPROPERTY(EditAnywhere, Category = "Companion|Bark", meta = (ClampMin = "0.5"))
	float BarkDuration = 2.5f;

	/** 台词全局冷却（秒）：任意两条台词的最小间隔——混战不刷屏 */
	UPROPERTY(EditAnywhere, Category = "Companion|Bark", meta = (ClampMin = "0.0"))
	float BarkGlobalCooldown = 1.2f;

private:
	/** 决策体（0.25s 节拍）：感知集合 → 状态迁移 → 移动/开火节拍管理 → 可解释状态行 */
	void UpdateBrain();

	/** 感知回调（引擎线程）：进视野加入集合、出视野移除——目标集合的唯一事实源 */
	UFUNCTION()
	void HandlePerceptionUpdated(AActor* Actor, struct FAIStimulus Stimulus);

	/** 伤害通道回调：玩家血量组件的 OnDamaged——伙伴挨打即把凶手注入威胁候选集（补视锥盲区，感知-决策断层修复） */
	UFUNCTION()
	void HandlePlayerDamaged(UHealthComponent* HealthComp, AActor* DamageSource);

	/** 开火节拍：射程+LOS 双条件校验通过才 ApplyDamage + 弹道光束 */
	void FireAtTarget();

	/** 单条弹道（C1 火力层统一出口）：方向 trace → 命中/贯穿伤害 → 光束特效（颜色/粗细/贯穿感随升级卡变形）；HitFlashColor=命中火花色（伤害来源的可视辨识——常态黄/重击红/爆射品红，用户 2026-09-13 定调） */
	void FireShot(AActor* InstigatorPawn, const FVector& Origin, const FVector& Direction, float DamageScale, const FColor& HitFlashColor = FColor::Yellow,
		TMap<TWeakObjectPtr<AActor>, FVector>* ElementHits = nullptr);

public:
	/** 融合·闪现轰击（C1 融合卡）：闪现落点向八方爆射弹道——玩家闪现时回调，"你落地即爆炸" */
	void NovaBurst(AActor* InstigatorPawn, const FVector& Center);

	/** E5 狂怒攻速（TemporaryPickup 收集触发）：开火间隔×0.5 持续 Duration 秒，弹道变紫，定时自恢复 */
	UFUNCTION(BlueprintCallable, Category = "Companion|Combat")
	void ActivateFrenzy(float Duration, bool bFromPotion = true);
	/** 真实狂暴定时器剩余时间；不创建计时副本，非编辑器构建同样可用。 */
	UFUNCTION(BlueprintPure, Category = "Companion|Combat")
	float GetFrenzyRemaining() const;
	UFUNCTION(BlueprintCallable, Category = "Debug|Relics")
	void DebugFireAt(AActor* Target);
	UFUNCTION(BlueprintPure, Category = "Debug|Relics")
	FString DebugFrenzyState() const;
	/** 复用真实闪现爆射；只在编辑器接受，射线仍走正式伤害/状态入口。 */
	UFUNCTION(BlueprintCallable, Category = "Debug|Combat")
	void DebugNovaBurst(const FVector& Center);
	UFUNCTION(BlueprintPure, Category = "Debug|Combat")
	FString DebugLastShotState() const;

private:
	float LastShotRawDamage = 0.f;
	float LastShotActualDamage = 0.f;
	float LastShotDisplayedDamage = 0.f;
	float LastShotModeMultiplier = 1.f;
	TWeakObjectPtr<AActor> LastShotVictim;
	/** 狂怒恢复回调：系数回 1；节拍活着则按基线重挂（FireTimer 与 Engage 状态严格同生命周期，重挂前须判活） */
	void RestoreFireRate();

	/** E5 开火间隔临时系数：1=常态、0.5=狂怒（纯运行时暂态，不进反射） */
	float FrenzyCooldownFactor = 1.f;

	/** 狂怒到期定时器（重复拾取刷新，ApplySlow 同范式） */
	FTimerHandle FrenzyTimerHandle;

private:

	/** 视线判定：Visibility 通道直线，命中物恰为目标才算畅通（防隔墙射击） */
	bool HasLineOfSight(const AActor* Target) const;

	/** M3 utility 评分器（M2 PickTarget 的整体替换）：感知集合逐个三维评分 + 切换惩罚，返回最优目标；OutScores 供审计日志与评分板复用 */
	AActor* EvaluateTargets(TArray<FTargetScore>& OutScores);

	/** 屏幕评分板（key 池 20+）：表头带玩家血量，每候选一行三维明细与总分，►标记选定——"为什么选它"的常驻出口 */
	void DrawScoreboard(const TArray<FTargetScore>& Scores, const AActor* Best) const;

	/** 步态输入馈送（D7）：兜底目标 Follow=玩家 / Engage=敌人，方向优先寻路路点；到位停喂；方向经 SmoothedFeedDir 逐帧平滑（A4） */
	void FeedMovementInput(float DeltaSeconds);

public:
	/** 台词播报（A1 Bark，09-27 起 public——WaveDirector 组合根抽卡台词需要调用）：全局冷却 + 分类冷却双闸门，
	 *  头顶 3D 气泡 + 空间化配音（Voice 槽，None=只出字不出声）+ [Bark] 日志留痕——队友人格的嘴 */
	void Bark(const FString& Line, FName Category, float CategoryCooldown = 0.f, EGameSFX Voice = EGameSFX::None);

private:
	/** 击杀连杀闸（09-27 用户裁决"5s 内杀 10+ 才喊"）：滚动窗口计数 */
	int32 KillStreakCount = 0;
	double KillWindowStart = -100.0;

	/** 视觉配置（构造期写入父类 PerceptionComponent；SightRadius/LoseSightRadius 可后续参数化） */
	UPROPERTY(Transient)
	TObjectPtr<UAISenseConfig_Sight> SightConfig;

	/** 当前感知到的敌人集合：弱指针——敌人自毁后自动失效，不悬空；元素为战斗单位（经 ICombatant 识别，跨形体抽象） */
	TSet<TWeakObjectPtr<AActor>> PerceivedEnemies;

	/** 当前 Engage 目标：弱指针，死亡自毁不悬空 */
	TWeakObjectPtr<AActor> EngageTarget;

	ECompanionState CurrentState = ECompanionState::Follow;

	/** 开火节拍手柄：Enter Engage 启动 / Exit Engage 清除 */
	FTimerHandle FireTimerHandle;

	/** 决策节流手柄（BeginPlay 启动，贯穿全程） */
	FTimerHandle BrainTimerHandle;

	/** 决策计数：前 3 次输出 COMP/PLAYER 对照读数（M1 排障遗产，回归锚点） */
	int32 DiagnosticUpdateCount = 0;

	/** Q 键上帧状态：边沿检测（按下瞬间才切换，防按住连发） */
	bool bPrevQDown = false;

	/** 喂入方向平滑缓存（A4）：跨帧指数转向，路点切换不再瞬变；零向量时直接对齐目标方向 */
	FVector SmoothedFeedDir = FVector::ZeroVector;

	/** 台词分类节流表（A1）：类别 → 上次播报时刻（FPlatformTime 秒） */
	TMap<FName, double> LastBarkTime;

	/** 全局上次播报时刻（A1）：与 LastBarkTime 双闸门中的外层 */
	double LastBarkGlobalTime = 0.0;

	/** 玩家低血量边沿缓存：跌破 30% 只播一次"撑住"，回升后重置 */
	bool bPrevPlayerLow = false;

	/** 巡检探针倒计时（秒）：每 5s 一行队友/玩家坐标日志——掉队/卡死/跨层在日志里现形 */
	float PatrolLogCountdown = 0.f;
};
