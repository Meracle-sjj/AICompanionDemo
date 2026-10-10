/**
 * [INPUT]: 依赖 AIModule 的 MoveToActor/PathFollowingComponent/AIPerceptionComponent/AISenseConfig_Sight/AISense_Sight、NavigationSystem 的 FNavigationPath（路点读取）、Engine 的 GEngine 屏幕消息/DrawDebugLine/LineTrace、Kismet 的 GetPlayerPawn、TimerManager 的节流定时器、本模块 HealthComponent（玩家血量行与残血分）、本模块 Audio/SFXLibrary（Shot_Companion/Hit_Enemy/Explosion 事件音效）
 * [OUTPUT]: 对外提供 M3 行为实现——UpdateBrain 决策体（状态迁移 + ENTER/SWITCH 审计日志 + 走射行动：FireTimer 与寻路并行，站桩已废）、EvaluateTargets 三维评分器（威胁 = 距离近似 + 咬人仇恨覆盖，修复贴脸混战时距离分满格失明的退化）、DrawScoreboard 屏幕评分板、HandlePerceptionUpdated 感知回调、FireAtTarget 射击节拍、FeedMovementInput当前路径段馈送与同目标MoveTo去重（1.1）；M3.75 增补 Bark 台词播报（六类事件钩点——09-27 换目标播报退役，击杀播报挂连杀闸 5s/10 杀；全局+分类双冷却；09-26 换装头顶 3D 气泡、09-27 配音经 PlayVoice 空间化在队友身上）；E5 增补 ActivateFrenzy/RestoreFireRate（暂态狂怒攻速：开火间隔系数槽 + 弹道紫，定时自恢复——TemporaryPickup 触发）；**火力结算层候选集一律 CollectAliveCombatants 契约收集**（走廊命中/溅射/折射弹跳——09-30 召唤师案定罪 TActorIterator<AEnemyGrunt> 类型扫描把 ABossBase 排除在命中集外=弹道穿体不掉血；冰/电残留仍限定EnemyGrunt；猎印按ICombatant叠层/消费，覆盖Boss家族）
 *  火力/感电配对已抽到CompanionCombatShots；本文件保留Q护卫/歼灭切换与决策。伤害/治疗加成由Health唯一入口消费helper，弹路不重复乘算。
 * [POS]: Source/AICompanionDemo/AI 的决策核心实现；驱动CompanionCharacter、经ICombatant感知与攻击；2026-10-08移除常驻AI目标/状态调试行，真实决策日志与默认关闭的评分板保留为调试证据。
 * [PROTOCOL]: 变更时更新此头部，然后检查../AGENTS.md
 */

#include "CompanionAIController.h"
#include "Events/RunEventLog.h"
#include "Relics/BossRelicComponent.h"

#include "AICompanionDemo.h"
#include "Components/SkeletalMeshComponent.h"
#include "AI/Combatant.h"
#include "AI/CompanionCharacter.h"
#include "AI/EnemyGrunt.h"
#include "Animation/AnimInstance.h"
#include "Audio/SFXLibrary.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Health/HealthComponent.h"
#include "InputCore.h"
#include "Landscape.h"
#include "Kismet/GameplayStatics.h"
#include "Vfx/VfxLibrary.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationData.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Perception/AISense_Sight.h"
#include "TimerManager.h"

namespace
{
	// 视线起点在胶囊上的抬升：接近"眼高"，避免地面杂物干扰 Visibility 通道
	constexpr float kEyeHeightOffset = 80.f;

	// 无视线候选的评分硬惩罚（09-25 无视线锁死案配套）：0.35 恰好压过满格距离分——
	// "打不着的 optimal"让位给"打得着的次优"；火力层走廊射击兜底，双层杜绝哑火
	constexpr float kNoLosPenalty = 0.35f;

	// 在评分表中定位某目标的快照（ENTER/SWITCH 审计日志用）；阵亡者不入表，返回 nullptr
	const FTargetScore* FindScore(const TArray<FTargetScore>& Scores, const AActor* Enemy)
	{
		for (const FTargetScore& Score : Scores)
		{
			if (Score.Enemy == Enemy)
			{
				return &Score;
			}
		}
		return nullptr;
	}
}

void ACompanionAIController::HandlePlayerDamaged(UHealthComponent* HealthComp, AActor* DamageSource)
{
	// 台词职责已迁出（09-30 用户复测"还是不对"定罪：本处逐击 Bark 与 Character 累计制警报
	// 双通道异步播同一句语音，各自 6s 冷却叠加=体感频繁）——挨打喊话统一由
	// Character::HandleHealthChanged 累计制触发（3s 窗 20 血 + 6s 冷却，一条通路一个冷却）。
	// 本回调只保留战术职责：

	// 伙伴挨打，队友记下凶手：注入威胁候选集（弱指针自管理，敌人死亡自动失效）——
	// 记仇语义：视锥外的它一直算候选，直到被击杀或进入视野走正常感知流程
	ICombatant* Enemy = Cast<ICombatant>(DamageSource);
	if (!Enemy || Enemy->IsDead())
	{
		return;
	}

	AActor* EnemyActor = DamageSource;
	const bool bNewEntry = !PerceivedEnemies.Contains(EnemyActor);
	PerceivedEnemies.Add(EnemyActor);

	if (bNewEntry)
	{
		UE_LOG(LogAICompanionDemo, Warning, TEXT("[Perception] %s 攻击玩家，经伤害通道纳入威胁候选"),
			*GetNameSafe(EnemyActor));
	}
}

ACompanionAIController::ACompanionAIController()
{
	// 决策由定时器节流（UpdateBrain）；Tick 只负责每帧喂步态输入向量
	PrimaryActorTick.bCanEverTick = true;

	// 视觉感知（R6：感知走引擎不自研）：参数 UPROPERTY 化（Companion|Perception 分类，PIE 实时可调）。
	// 感知对齐（2026-09-05 用户产品反馈"信息对等原则"）：26m 视距 ≈ 第三人称玩家屏幕可辨敌人范围，
	// 全向视锥（180°）消除背后盲区——AI 与玩家看到同一个世界，决策才符合人类预期。
	// 关键事实：AAIController 不自动创建感知组件（5.6 源 AIController.h:142 只是空指针），
	// 必须自建并经 SetPerceptionComponent 注册——2026-09-03 编辑器启动即崩（CDO 构造空引用）的教训。
	// 隶属度全默认是"看不见任何人"——Enemies/Neutrals 双开，敌人无 TeamAgent 时按 Neutral 判定
	SightConfig = CreateDefaultSubobject<UAISenseConfig_Sight>(TEXT("SightConfig"));
	SetPerceptionComponent(*CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("PerceptionComponent")));

	SightConfig->SightRadius = SightRange;
	SightConfig->LoseSightRadius = LoseSightRange;
	SightConfig->PeripheralVisionAngleDegrees = 180.f;
	SightConfig->DetectionByAffiliation.bDetectEnemies = true;
	SightConfig->DetectionByAffiliation.bDetectNeutrals = true;

	GetPerceptionComponent()->ConfigureSense(*SightConfig);
	GetPerceptionComponent()->SetDominantSense(UAISense_Sight::StaticClass());
	GetPerceptionComponent()->OnTargetPerceptionUpdated.AddDynamic(this, &ACompanionAIController::HandlePerceptionUpdated);
}

void ACompanionAIController::BeginPlay()
{
	Super::BeginPlay();

	// 诊断日志：确认控制器生命周期已启动（若日志缺席 = 从未生成/接管）
	UE_LOG(LogAICompanionDemo, Warning, TEXT("[Companion] Controller BeginPlay: %s"), *GetNameSafe(this));

	// 决策循环：以 FollowUpdateInterval 为节拍重复 UpdateBrain（感知→状态→行动）
	GetWorldTimerManager().SetTimer(
		BrainTimerHandle, this, &ACompanionAIController::UpdateBrain, FollowUpdateInterval, true);

	// 伤害通道（感知-决策断层修复）：绑定玩家血量组件的受击广播——
	// 视锥 140° 有盲区，背后咬主者必须经此通道入候选集（5.6 引擎 Damage Sense 分发给被伤者自身，
	// 对"护主转发"是死路，见 2026-09-05 grep 实录——事件多播是语义与工程的双正解）
	if (const APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		if (UHealthComponent* PlayerHealth = PlayerPawn->FindComponentByClass<UHealthComponent>())
		{
			PlayerHealth->OnDamaged.AddDynamic(this, &ACompanionAIController::HandlePlayerDamaged);
		}
	}
}

void ACompanionAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	// 诊断日志：确认 AutoPossessAI 接管链路（若日志缺席 = 放置实例未被 AI 接管）
	UE_LOG(LogAICompanionDemo, Warning, TEXT("[Companion] OnPossess: controller=%s -> pawn=%s"), *GetNameSafe(this), *GetNameSafe(InPawn));
}

void ACompanionAIController::HandlePerceptionUpdated(AActor* Actor, FAIStimulus Stimulus)
{
	// 只关心敌对方战斗单位（ICombatant 契约）；感知系统的其他刺激（噪声等）静默忽略
	if (!Cast<ICombatant>(Actor))
	{
		return;
	}

	if (Stimulus.WasSuccessfullySensed())
	{
		PerceivedEnemies.Add(Actor);
	}
	else
	{
		PerceivedEnemies.Remove(Actor);
	}

	// 感知事件低频且是 M2 验收核心链路：每条都留痕（可解释叙事的日志面）
	UE_LOG(LogAICompanionDemo, Warning, TEXT("[Perception] %s %s"),
		*GetNameSafe(Actor),
		Stimulus.WasSuccessfullySensed() ? TEXT("进入视野") : TEXT("离开视野"));
}

AActor* ACompanionAIController::EvaluateTargets(TArray<FTargetScore>& OutScores)
{
	OutScores.Reset();

	const APawn* MyPawn = GetPawn();
	const APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!MyPawn || !PlayerPawn)
	{
		return nullptr;
	}

	// 距离分归一化基准与感知半径同源（SightConfig 读数即 UPROPERTY SightRange）——改感知配置，评分自动跟随
	const float SightRangeNorm = SightConfig ? SightConfig->SightRadius : 1500.f;
	const AActor* CurrentTarget = EngageTarget.Get();

	AActor* BestEnemy = nullptr;
	float BestTotal = -MAX_flt;

	for (const TWeakObjectPtr<AActor>& WeakEnemy : PerceivedEnemies)
	{
		AActor* Enemy = WeakEnemy.Get();
		const ICombatant* Combatant = Cast<ICombatant>(Enemy);
		if (!Enemy || !Combatant || Combatant->IsDead())
		{
			continue;
		}

		FTargetScore Score;
		Score.Enemy = Enemy;

		// D 距离分：队友越近越高——输出路径短，转火成本低
		const float DistToMe = FVector::Dist(MyPawn->GetActorLocation(), Enemy->GetActorLocation());
		Score.DistanceScore = 1.f - FMath::Clamp(DistToMe / SightRangeNorm, 0.f, 1.f);

		// T 威胁分：距离只是潜在威胁（离玩家近）；正在咬玩家的敌人是现实仇恨——覆盖拉满，
		// 修复"三敌贴脸时距离分全部满格、评分器失明"的退化（2026-09-05 用户实测锁死第一个目标的根因）
		const float DistToPlayer = FVector::Dist(PlayerPawn->GetActorLocation(), Enemy->GetActorLocation());
		Score.bRetaliating = Combatant->IsEngagingPlayer();
		Score.ThreatScore = Score.bRetaliating
			? 1.f
			: 1.f - FMath::Clamp(DistToPlayer / ThreatRange, 0.f, 1.f);

		// V 残血分：目标已损血量占比——收割终结，缩短战斗
		const UHealthComponent* EnemyHealth = Combatant->GetHealthComponent();
		Score.VulnerabilityScore = EnemyHealth
			? 1.f - FMath::Clamp(EnemyHealth->GetCurrentHealth() / FMath::Max(EnemyHealth->GetMaxHealth(), 1.f), 0.f, 1.f)
			: 0.f;

		// 加权合成；候选≠现目标时抵扣切换惩罚（滞回带：现任有卫冕优势，抑制横跳）
		Score.Total = WeightDistance * Score.DistanceScore
			+ WeightThreat * Score.ThreatScore
			+ WeightVulnerability * Score.VulnerabilityScore;
		if (Enemy != CurrentTarget)
		{
			Score.Total -= SwitchCost;
		}

		// LOS 惩罚（09-25 无视线锁死案配套）：打不着的目标出局优先级——混战里被同伴胶囊挡线
		// 是常态，评分器必须偏向"打得着的"；火力层无视线走廊射击兜底，双层合力杜绝哑火
		if (!HasLineOfSight(Enemy))
		{
			Score.Total -= kNoLosPenalty;
		}

		OutScores.Add(Score);
		if (Score.Total > BestTotal)
		{
			BestTotal = Score.Total;
			BestEnemy = Enemy;
		}
	}

	return BestEnemy;
}

void ACompanionAIController::DrawScoreboard(const TArray<FTargetScore>& Scores, const AActor* Best) const
{
	// 总开关（09-18 用户裁决默认关）：评分体系仍在驱动 AI，只是不画——可解释性留给面试叙事与调试
	if (!bEnableScoreboard || !GEngine)
	{
		return;
	}

	// 表头带玩家血量：威胁维度"为何存在"的实时注脚——玩家在掉血，评分板在看
	FString Header = TEXT("── M3 Utility 评分板 ──");
	if (const APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		if (const UHealthComponent* PlayerHealth = PlayerPawn->FindComponentByClass<UHealthComponent>())
		{
			Header = FString::Printf(TEXT("── M3 评分板 | 玩家 HP %.0f/%.0f ──"),
				PlayerHealth->GetCurrentHealth(), PlayerHealth->GetMaxHealth());
		}
	}

	// key 池20起：20=表头，21+=候选行；仅显式打开评分板时显示，时长为节拍×2。
	GEngine->AddOnScreenDebugMessage(20, FollowUpdateInterval * 2.f, FColor::Silver, Header);

	int32 Key = 21;
	for (const FTargetScore& Score : Scores)
	{
		const bool bChosen = Score.Enemy == Best;
		const bool bPenalized = CurrentState == ECompanionState::Engage && Score.Enemy != EngageTarget.Get();
		const FString PenaltyTag = bPenalized ? FString::Printf(TEXT(" (切换-%.2f)"), SwitchCost) : TEXT("");

		const FString Row = FString::Printf(TEXT("%s %s | 总分 %.2f%s%s | 距 %.2f 威 %.2f%s 残 %.2f"),
			bChosen ? TEXT("►") : TEXT(" "),
			*GetNameSafe(Score.Enemy),
			Score.Total,
			bChosen ? TEXT(" ◄选定") : TEXT(""),
			*PenaltyTag,
			Score.DistanceScore,
			Score.ThreatScore,
			Score.bRetaliating ? TEXT("[咬]") : TEXT(""),
			Score.VulnerabilityScore);

		GEngine->AddOnScreenDebugMessage(Key++, FollowUpdateInterval * 2.f,
			bChosen ? FColor::Orange : FColor::Silver, Row);
	}
}

bool ACompanionAIController::HasLineOfSight(const AActor* Target) const
{
	const APawn* MyPawn = GetPawn();
	if (!MyPawn || !Target)
	{
		return false;
	}

	FHitResult Hit;
	const FVector Start = MyPawn->GetActorLocation() + FVector(0.f, 0.f, kEyeHeightOffset);
	const FVector End = Target->GetActorLocation();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CompanionLOS), /*bTraceComplex*/ false, /*IgnoreActor*/ GetPawn());

	const bool bBlocked = GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params);
	// 命中物恰为目标才畅通：被第三方（墙/另一木桩）挡住不算
	return bBlocked && Hit.GetActor() == Target;
}

void ACompanionAIController::UpdateBrain()
{
	// ---------- 1. 感知集合 → utility 评分 → 最优目标 ----------
	TArray<FTargetScore> Scores;
	AActor* Target = EvaluateTargets(Scores);

	// ---------- 2. 状态迁移（ENTER/SWITCH 双审计口） ----------
	if (Target)
	{
		if (CurrentState != ECompanionState::Engage)
		{
			CurrentState = ECompanionState::Engage;
			EngageTarget = Target;
			// 开火节拍只在进入 Engage 时启动，Exit 时清除——节拍与状态严格同生命周期；
			// 间隔乘 E5 狂怒系数（常态 1 = 原节拍，TemporaryPickup 激活期间 0.5）
			GetWorldTimerManager().SetTimer(FireTimerHandle, this, &ACompanionAIController::FireAtTarget, AttackInterval * FrenzyCooldownFactor, true);

			// ENTER 审计：选中目标的三维明细随行——"为什么是它"的第一现场
			if (const FTargetScore* Chosen = FindScore(Scores, Target))
			{
				UE_LOG(LogAICompanionDemo, Warning,
					TEXT("[Combat] 状态 FOLLOW -> ENGAGE，目标 %s（总分 %.2f = 距 %.2f 威 %.2f 残 %.2f）"),
					*GetNameSafe(Target), Chosen->Total,
					Chosen->DistanceScore, Chosen->ThreatScore, Chosen->VulnerabilityScore);
			}

			// A1 台词：接敌宣言（FEAR 结论：台词是"看起来聪明"的最大杠杆）
			Bark(TEXT("发现敌人——交给我！"), TEXT("Discover"), 0.f, EGameSFX::Voice_Discover);
		}
		else if (Target != EngageTarget.Get())
		{
			AActor* OldTarget = EngageTarget.Get();
			EngageTarget = Target;

			// SWITCH 审计：新目标总分须压过现目标（已抵切换惩罚）——横跳在此被滞回带吸收
			const FTargetScore* NewScore = FindScore(Scores, Target);
			const FTargetScore* OldScore = FindScore(Scores, OldTarget);
			if (OldScore)
			{
				UE_LOG(LogAICompanionDemo, Warning,
					TEXT("[Combat] 切换目标 %s -> %s（新 %.2f 压过旧 %.2f，已抵切换惩罚 %.2f）"),
					*GetNameSafe(OldTarget), *GetNameSafe(Target),
					NewScore ? NewScore->Total : 0.f, OldScore->Total, SwitchCost);
			}
			else
			{
				UE_LOG(LogAICompanionDemo, Warning,
					TEXT("[Combat] 切换目标 %s -> %s（原目标已阵亡；新总分 %.2f）"),
					*GetNameSafe(OldTarget), *GetNameSafe(Target),
					NewScore ? NewScore->Total : 0.f);
			}

			// 换目标不播报（09-27 用户裁决：混战中频繁换目标是系统内务，不是人话）
		}
	}
	else
	{
		if (CurrentState == ECompanionState::Engage)
		{
			CurrentState = ECompanionState::Follow;
			EngageTarget = nullptr;
			GetWorldTimerManager().ClearTimer(FireTimerHandle);
			UE_LOG(LogAICompanionDemo, Warning, TEXT("[Combat] 状态 ENGAGE -> FOLLOW（候选清空）"));
			// "威胁解除"播报退役（09-30 用户实测莫名出现+音色漂移）：Endless 下感知候选清空≠敌人全灭，
			// 甩开视野/掉队瞬间就误触——触发语义在 Endless 根本不成立；静态波次安全网也不需要它
		}

		EngageTarget = nullptr;
	}

	// ---------- 3. 行动执行（移动层按模式分派；火力层与评分器两模式共享） ----------
	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
	if (CurrentMode == ECompanionMode::Berserk && Target && PlayerPawn)
	{
		// 歼灭：评分最优目标就是目的地——走射逼近（2026-09-05 用户定调：狂人是取舍不是错误）
		EnsureMoveRequest(Target, EngageApproachDist);
	}
	else if (PlayerPawn)
	{
		// 护卫（默认）：移动层永远归玩家——与玩家同步移动，火力交 FireTimer 在射程内自动输出；
		// 敌人的猎手模型（追玩家）把它们自己送进火力圈，护卫无需追击（2026-09-05 "守护优先"定调）
		EnsureMoveRequest(PlayerPawn, FollowAcceptanceRadius);
	}

	// A1 台词：玩家低血量边沿（跌破 30% 播一次，回升重置）——护伙伴人格的"撑住"时刻
	if (PlayerPawn)
	{
		if (const UHealthComponent* PlayerHealth = PlayerPawn->FindComponentByClass<UHealthComponent>())
		{
			const float PlayerPct = PlayerHealth->GetCurrentHealth() / FMath::Max(PlayerHealth->GetMaxHealth(), 1.f);
			const bool bLowNow = PlayerPct > 0.f && PlayerPct <= 0.3f;
			if (bLowNow && !bPrevPlayerLow)
			{
				Bark(TEXT("你撑住，我顶上！"), TEXT("PlayerLow"), 8.f, EGameSFX::Voice_PlayerLow);
			}
			bPrevPlayerLow = bLowNow;
		}
	}

	// 巡检探针（隐形敌人案取证件，2026-09-10）：每 5s 一行队友/玩家坐标与间距——
	// 掉队/卡死/跨层在日志里现形（本案 ENTER ENGAGE 距离分 0.00 反推队友离玩家 ≥24m，
	// 却无任何位置日志可查，排障多绕一圈——位置事实必须常驻日志）
	PatrolLogCountdown -= FollowUpdateInterval;
	if (PatrolLogCountdown <= 0.f && GetPawn() && PlayerPawn)
	{
		PatrolLogCountdown = 5.f;
		const float FollowDist = FVector::Dist(GetPawn()->GetActorLocation(), PlayerPawn->GetActorLocation());
		UE_LOG(LogAICompanionDemo, Warning, TEXT("[Companion] 巡检：队友@%s 玩家@%s 间距 %.1f m"),
			*GetPawn()->GetActorLocation().ToCompactString(),
			*PlayerPawn->GetActorLocation().ToCompactString(), FollowDist / 100.f);
	}

	// ---------- 4. 可解释输出 ----------
	DrawScoreboard(Scores, Target);

	// 诊断日志：前 3 次决策的 COMP/PLAYER 同刻对照读数（M1 动画排障遗产，回归锚点——
	// 队友步态依赖输入管线的加速度，此行 accel 归零即步态病复发的前兆）
	if (DiagnosticUpdateCount < 3)
	{
		++DiagnosticUpdateCount;
		const ACharacter* CompanionPawn = Cast<ACharacter>(GetPawn());
		const ACharacter* PlayerChar = Cast<ACharacter>(PlayerPawn);
		if (CompanionPawn && PlayerChar)
		{
			const UCharacterMovementComponent* CompCMC = CompanionPawn->GetCharacterMovement();
			const UCharacterMovementComponent* PlayerCMC = PlayerChar->GetCharacterMovement();
			const UAnimInstance* CompAnim = CompanionPawn->GetMesh()->GetAnimInstance();
			const UAnimInstance* PlayerAnim = PlayerChar->GetMesh()->GetAnimInstance();

			UE_LOG(LogAICompanionDemo, Warning,
				TEXT("[Companion] Probe#%d: COMP vel=%.0f accel=%.1f anim=%s | PLAYER vel=%.0f accel=%.1f anim=%s"),
				DiagnosticUpdateCount,
				CompanionPawn->GetVelocity().Size(),
				CompCMC ? CompCMC->GetCurrentAcceleration().Size() : -1.f,
				*GetNameSafe(CompAnim),
				PlayerChar->GetVelocity().Size(),
				PlayerCMC ? PlayerCMC->GetCurrentAcceleration().Size() : -1.f,
				*GetNameSafe(PlayerAnim));
		}
	}
}

// ---------- E5 暂态狂怒攻速（TemporaryPickup 收集触发） ----------

void ACompanionAIController::ActivateFrenzy(float Duration, bool bFromPotion)
{
	if (bFromPotion)
	{
		const APawn* Hero = UGameplayStatics::GetPlayerPawn(this, 0);
		if (const UBossRelicComponent* Relics = Hero ? Hero->FindComponentByClass<UBossRelicComponent>() : nullptr)
		{ Duration *= Relics->GetPotionFrenzyDurationMultiplier(); }
	}
	Duration = FMath::Max(Duration, GetWorldTimerManager().GetTimerRemaining(FrenzyTimerHandle));
	FrenzyCooldownFactor = 0.5f;
	// 节拍活着才重挂（未接敌时 FireTimer 不存在，等下次 EnterEngage 自然用新系数——与状态同生命周期）
	if (FireTimerHandle.IsValid())
	{
		GetWorldTimerManager().SetTimer(FireTimerHandle, this, &ACompanionAIController::FireAtTarget,
			AttackInterval * FrenzyCooldownFactor, true);
	}
	// 攻速期特效：队友染紫（Paint Tint 真名管线，EnemyGrunt 同款）——状态肉眼可见（用户 09-18"AI 身上应该有点特效"）
	if (const APawn* MyPawn = GetPawn())
	{
		if (USkeletalMeshComponent* Skel = MyPawn->FindComponentByClass<USkeletalMeshComponent>())
		{
			Skel->SetVectorParameterValueOnMaterials(TEXT("Paint Tint"), FVector(0.35f, 0.08f, 0.55f));
		}
	}
	GetWorldTimerManager().SetTimer(FrenzyTimerHandle, this, &ACompanionAIController::RestoreFireRate, Duration, /*bLoop*/ false);
	UE_LOG(LogAICompanionDemo, Warning, TEXT("[Combat] 狂怒攻速激活 %.0fs（开火间隔 %.2f -> %.2f）"),
		Duration, AttackInterval, AttackInterval * FrenzyCooldownFactor);
}

void ACompanionAIController::RestoreFireRate()
{
	FrenzyCooldownFactor = 1.f;
	if (FireTimerHandle.IsValid())
	{
		GetWorldTimerManager().SetTimer(FireTimerHandle, this, &ACompanionAIController::FireAtTarget, AttackInterval, true);
	}
	// 还原队友染色（白 = 无色调）
	if (const APawn* MyPawn = GetPawn())
	{
		if (USkeletalMeshComponent* Skel = MyPawn->FindComponentByClass<USkeletalMeshComponent>())
		{
			Skel->SetVectorParameterValueOnMaterials(TEXT("Paint Tint"), FVector::OneVector);
		}
	}
	UE_LOG(LogAICompanionDemo, Warning, TEXT("[Combat] 狂怒攻速结束，开火间隔恢复 %.2f"), AttackInterval);
}

void ACompanionAIController::FireAtTarget()
{
	if (UGameplayStatics::GetGlobalTimeDilation(this) < .01f || GetWorld()->IsPaused()) return;
	const APawn* CombatGateHero = UGameplayStatics::GetPlayerPawn(this, 0);
	const UHealthComponent* HeroHealth = CombatGateHero ? CombatGateHero->FindComponentByClass<UHealthComponent>() : nullptr;
	if (!HeroHealth || HeroHealth->GetCurrentHealth() <= 0.f || HeroHealth->IsDead()) return;
	AActor* Target = EngageTarget.Get();
	ICombatant* Combatant = Cast<ICombatant>(Target);
	if (!Target || !Combatant || Combatant->IsDead())
	{
		return;  // 死亡目标不补枪；目标切换由 UpdateBrain 下一拍接管
	}

	APawn* MyPawn = GetPawn();  // 非 const：施伤者身份随伤害入口传递（ApplyDamage 的 Instigator）
	if (!MyPawn)
	{
		return;
	}

	const float DistCm = FVector::Dist(MyPawn->GetActorLocation(), Target->GetActorLocation());
	const bool bInRange = DistCm <= AttackRange;
	const bool bLOS = HasLineOfSight(Target);

	// 09-25 无视线锁死案修正（用户实测"捡盾后队友一直不攻击"）：原"无视线即不射"在围殴圈里
	// 被同伴/敌人胶囊挡线时永久哑火——改为无视线也照常走廊射击：弹道 trace 自己找首个命中者，
	// 挡路者吃子弹（穿透卡全穿），评分器 LOS 惩罚另负责把"打得着的"排前面。超射程仍禁火
	if (!bInRange)
	{
		UE_LOG(LogAICompanionDemo, Warning, TEXT("[Combat] 未开火（超射程 %.0fcm）"), DistCm);
		return;
	}

	UHealthComponent* TargetHealth = Combatant->GetHealthComponent();
	if (!TargetHealth)
	{
		return;
	}

	// 主弹道：评分与 LOS 已背书，伤害必达主目标（C1 火力层统一出口，特效随升级卡变形）
	const FVector Origin = MyPawn->GetActorLocation() + FVector(0.f, 0.f, kEyeHeightOffset);
	const FVector BaseDir = (Target->GetActorLocation() - Origin).GetSafeNormal();
	FireShot(MyPawn, Origin, BaseDir, 1.0f);
	USFXLibrary::PlaySFX(GetWorld(), EGameSFX::Shot_Companion);   // [SFX] 主弹道一声——散射/爆射侧向弹道不再重复发声

	// C1 技能卡·散射：±15° 侧向弹道各 70%——密集杂兵的清场爽感
	// 融合·弹幕风暴（散射+穿透）：进化五向（±12.5°/±25°），配合穿透走廊全部贯穿
	if (bScatterUnlocked)
	{
		if (bBarrageUnlocked)
		{
			for (const float OffsetYaw : { -25.f, -12.5f, 12.5f, 25.f })
			{
				FireShot(MyPawn, Origin, BaseDir.RotateAngleAxis(OffsetYaw, FVector::UpVector), ScatterDamageScale);
			}
		}
		else
		{
			for (const float OffsetYaw : { -15.f, 15.f })
			{
				FireShot(MyPawn, Origin, BaseDir.RotateAngleAxis(OffsetYaw, FVector::UpVector), ScatterDamageScale);
			}
		}
	}

	// A1 台词：击杀播报加连杀闸（09-27 用户裁决"5 秒内杀 10 只以上才喊"——单杀不喊，割草高潮才出声）：
	// 滚动窗口计数，达量播报一次并清零
	if (TargetHealth->GetCurrentHealth() <= 0.f)
	{
		const double Now = FPlatformTime::Seconds();
		if (Now - KillWindowStart > 5.0)
		{
			KillWindowStart = Now;
			KillStreakCount = 0;
		}
		++KillStreakCount;
		if (KillStreakCount >= 10)
		{
			KillStreakCount = 0;
			Bark(TEXT("威胁已清除"), TEXT("Kill"), 0.f, EGameSFX::Voice_Kill);
		}
	}
}

void ACompanionAIController::EnsureMoveRequest(AActor* Goal, float Acceptance)
{
	UPathFollowingComponent* Path = GetPathFollowingComponent();
	if (!Goal || !Path) { return; }
	const EPathFollowingStatus::Type Status = Path->GetStatus();
	if (RequestedMoveGoal == Goal && FMath::IsNearlyEqual(RequestedAcceptance, Acceptance)
		&& (Status == EPathFollowingStatus::Moving || Status == EPathFollowingStatus::Waiting)) { return; }
	if (Path->HasReached(*Goal, EPathFollowingReachMode::OverlapAgentAndGoal, Acceptance))
	{
		if (Status != EPathFollowingStatus::Idle) { StopMovement(); }
		return;
	}
	RequestedMoveGoal = Goal;
	RequestedAcceptance = Acceptance;
	++MovementRequestCount;
	MoveToActor(Goal, Acceptance, true, true, true); // 第五参为CanStrafe；移动Actor路径由PFC观察并更新。
}

void ACompanionAIController::FeedMovementInput(float DeltaSeconds)
{
	APawn* MyPawn = GetPawn();
	const UPathFollowingComponent* Path = GetPathFollowingComponent();
	if (!MyPawn || !Path || Path->GetStatus() != EPathFollowingStatus::Moving) { return; }
	// 当前目标由PFC推进，绝不从路径起点重扫；无有效路径时不以直线输入绕过导航失败。
	const FVector DesiredDir = (Path->GetCurrentTargetLocation() - MyPawn->GetActorLocation()).GetSafeNormal2D();
	if (DesiredDir.IsNearlyZero()) { return; }
	SmoothedFeedDir = FMath::VInterpTo(SmoothedFeedDir, DesiredDir, DeltaSeconds, FeedDirTurnSpeed);
	FVector FeedDir = SmoothedFeedDir.GetSafeNormal2D();
	if (FeedDir.IsNearlyZero()) { FeedDir = DesiredDir; }
	MyPawn->AddMovementInput(FeedDir, 1.f);
}

FString ACompanionAIController::DebugMovementState() const
{
	const UPathFollowingComponent* Path = GetPathFollowingComponent();
	const ACharacter* ControlledCharacter = Cast<ACharacter>(GetPawn());
	const UCharacterMovementComponent* CMC = ControlledCharacter ? ControlledCharacter->GetCharacterMovement() : nullptr;
	const FVector Position = ControlledCharacter ? ControlledCharacter->GetActorLocation() : FVector::ZeroVector;
	const FVector Velocity = ControlledCharacter ? ControlledCharacter->GetVelocity() : FVector::ZeroVector;
	const FVector Target = Path ? Path->GetCurrentTargetLocation() : FVector::ZeroVector;
	return FString::Printf(TEXT("{\"requests\":%d,\"pathStatus\":%d,\"segment\":%d,\"nextSegment\":%d,\"position\":[%.3f,%.3f,%.3f],\"velocity\":[%.3f,%.3f,%.3f],\"speed\":%.3f,\"accel\":%.3f,\"maxSpeed\":%.3f,\"target\":[%.3f,%.3f,%.3f]}"),
		MovementRequestCount, Path?int32(Path->GetStatus()):0, Path?Path->GetCurrentPathIndex():-1, Path?Path->GetNextPathIndex():-1,
		Position.X, Position.Y, Position.Z, Velocity.X, Velocity.Y, Velocity.Z, Velocity.Size2D(), CMC?CMC->GetCurrentAcceleration().Size():0.f, CMC?CMC->MaxWalkSpeed:0.f,
		Target.X, Target.Y, Target.Z);
}

void ACompanionAIController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!GetWorld() || GetWorld()->IsPaused() || UGameplayStatics::GetGlobalTimeDilation(this) < .01f) { return; }
	FeedMovementInput(DeltaSeconds);

	// Q 键边沿检测切换模式（零资产依赖：IsInputKeyDown 直读玩家输入，demo 单键的最小实现；
	// 按住不放只触发一次——bPrevQDown 边沿门）
	const APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	const bool bQDown = PC && PC->IsInputKeyDown(EKeys::Q);
	if (bQDown && !bPrevQDown)
	{
		SwitchMode();
	}
	bPrevQDown = bQDown;
}

void ACompanionAIController::SwitchMode()
{
	CurrentMode = (CurrentMode == ECompanionMode::Guard) ? ECompanionMode::Berserk : ECompanionMode::Guard;

	UE_LOG(LogAICompanionDemo, Warning, TEXT("[Companion] 模式切换 -> %s"),
		CurrentMode == ECompanionMode::Guard ? TEXT("护卫（跟玩家走射护主）") : TEXT("歼灭（追击评分最优目标）"));

	// A1 台词：模式切换宣言（Q 键的即时人格反馈）——文案与语音按新模式同源选槽
	if (CurrentMode == ECompanionMode::Guard)
	{
		Bark(TEXT("护卫协议：掩护优先。"), TEXT("Mode"), 0.f, EGameSFX::Voice_ModeGuard);
	}
	else
	{
		Bark(TEXT("全部交给我！"), TEXT("Mode"), 0.f, EGameSFX::Voice_ModeBerserk);
	}
}

void ACompanionAIController::Bark(const FString& Line, const FName Category, const float CategoryCooldown, const EGameSFX Voice)
{
	if (!bEnableBarks || !GEngine)
	{
		return;
	}

	// 双闸门节流：全局冷却压总频次（混战不刷屏），分类冷却放行低频要事
	//（击杀 0 冷却=只受全局闸门，连杀节奏保留；玩家挨打 6s、低血 8s、换目标 3s 各自独立）
	const double Now = FPlatformTime::Seconds();
	if (Now - LastBarkGlobalTime < BarkGlobalCooldown)
	{
		return;
	}
	if (const double* Last = LastBarkTime.Find(Category); Last && Now - *Last < CategoryCooldown)
	{
		return;
	}
	LastBarkGlobalTime = Now;
	LastBarkTime.Add(Category, Now);

	// 头顶 3D 聊天气泡（09-26 夜轮，用户定调"说话没有气泡吸引注意"——DrawDebugString 日志观感退役）：
	// Screen 空间覆盖层（检索定稿 WoW/ESO 语义），展示/生命周期归 UI/SpeechBubble，这里只递词；[Bark] 日志留痕供审计回放
	if (ACompanionCharacter* Companion = Cast<ACompanionCharacter>(GetPawn()))
	{
		Companion->ShowSpeechBubble(Line);
	}
	// 配音（09-27 豆包 seed-audio 冷峻女声）：语音与文字同源——空间化在队友身上（09-27 用户定调"音箱放到
	// AI 身上"：队友离远了喊话自然变小变远，PlayVoice 3D 衰减），槽位 ini 配置，空槽静默只出字
	if (Voice != EGameSFX::None)
	{
		if (const APawn* VoicedPawn = GetPawn())
		{
			USFXLibrary::PlayVoice(GetWorld(), Voice, VoicedPawn->GetActorLocation() + FVector(0.f, 0.f, 60.f));
		}
	}
	URunEventLog::Publish(GetWorld(), TEXT("队友"), Line, Category);
	UE_LOG(LogAICompanionDemo, Warning, TEXT("[Companion][Bark] %s"), *Line);
}
