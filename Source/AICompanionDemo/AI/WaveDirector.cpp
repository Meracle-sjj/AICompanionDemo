/**
 * [INPUT]: 波次/Endless节奏、出生参数与生命事件。
 * [OUTPUT]: 保留原关卡时序/三闸环形刷怪/死亡账本/旧回归HUD。
 * [POS]: AI/WaveDirector的按职责局部实现；机械拆分不改行为。
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md。
 */
#include "WaveDirector.h"
#include "Relics/BossRelicComponent.h"
#include "AI/BossAltar.h"
#include "AI/BossBase.h"
#include "AI/BossColossus.h"
#include "AI/BossSummoner.h"
#include "AI/BossWurm.h"
#include "AI/CompanionAIController.h"
#include "AI/CompanionCharacter.h"
#include "AI/Combatant.h"
#include "AICompanionDemo.h"
#include "AICompanionDemoGameMode.h"
#include "AICompanionDemoCharacter.h"
#include "Audio/SFXLibrary.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Events/RunEventLog.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Health/HealthComponent.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"
#include "TimerManager.h"
#include "UI/UpgradeCardView.h"
#include "Engine/GameViewportClient.h"
#include "WaveDirectorPrivate.h"

using namespace WaveDirectorPrivate;

namespace
{
	constexpr float kPhaseTick = 0.5f;
	constexpr int32 kHudKey = 4;
}

// ---------- 保留原关卡时序/三闸环形刷怪/死亡账本/旧回归HUD。 ----------

AWaveDirector::AWaveDirector()
{
	static_assert(static_cast<int32>(EUpgradeId::GoldPouch) == 28, "Legacy upgrade IDs must stay stable");
	// 三 Boss 终局默认值（09-30 用户定案：召唤师→裂地巨像→沙狱蠕虫）；Boss 三子类接线（白模占位案根治）
	BossAltarTimes = { 180.f, 360.f, 540.f };
	BossNames = { TEXT("召唤师"), TEXT("裂地巨像"), TEXT("沙狱蠕虫") };
	BossClasses = { ABossSummoner::StaticClass(), ABossColossus::StaticClass(), ABossWurm::StaticClass() };
	// Tick 仅服务升级暂停的数字键轮询（常态零开销早退）；节奏推进主体仍走 TimerManager 节拍
	PrimaryActorTick.bCanEverTick = true;

	// 默认二十波（用户 2026-09-13 定调"敌人随时间不断增强，数量优先、质量随后"）：
	// 总量逐波爬坡（数量优先），蛮兵配比缓入（质量随后）；近无缝喘息让"波"退化为难度级
	FWaveDef W1; W1.Grunts = 3;
	FWaveDef W2; W2.Rushers = 2; W2.Grunts = 3;
	FWaveDef W3; W3.Rushers = 3; W3.Grunts = 3; W3.Brutes = 1;
	FWaveDef W4; W4.Rushers = 4; W4.Grunts = 4; W4.Brutes = 1;
	FWaveDef W5; W5.Rushers = 5; W5.Grunts = 5; W5.Brutes = 1;
	FWaveDef W6; W6.Rushers = 5; W6.Grunts = 5; W6.Brutes = 1;
	FWaveDef W7; W7.Rushers = 5; W7.Grunts = 6; W7.Brutes = 1;
	FWaveDef W8; W8.Rushers = 6; W8.Grunts = 6; W8.Brutes = 2;
	FWaveDef W9; W9.Rushers = 6; W9.Grunts = 7; W9.Brutes = 2;
	FWaveDef W10; W10.Rushers = 8; W10.Grunts = 8; W10.Brutes = 3;
	FWaveDef W11; W11.Rushers = 8; W11.Grunts = 9; W11.Brutes = 3;
	FWaveDef W12; W12.Rushers = 9; W12.Grunts = 9; W12.Brutes = 3;
	FWaveDef W13; W13.Rushers = 9; W13.Grunts = 10; W13.Brutes = 4;
	FWaveDef W14; W14.Rushers = 10; W14.Grunts = 10; W14.Brutes = 4;
	FWaveDef W15; W15.Rushers = 10; W15.Grunts = 11; W15.Brutes = 4;
	FWaveDef W16; W16.Rushers = 11; W16.Grunts = 11; W16.Brutes = 5;
	FWaveDef W17; W17.Rushers = 11; W17.Grunts = 12; W17.Brutes = 5;
	FWaveDef W18; W18.Rushers = 12; W18.Grunts = 12; W18.Brutes = 5;
	FWaveDef W19; W19.Rushers = 12; W19.Grunts = 13; W19.Brutes = 6;
	FWaveDef W20; W20.Rushers = 13; W20.Grunts = 13; W20.Brutes = 6;
	Waves = { W1, W2, W3, W4, W5, W6, W7, W8, W9, W10,
		      W11, W12, W13, W14, W15, W16, W17, W18, W19, W20 };

	// Endless 配比茬表（R10 调研：VS 敌型 ~90s 换茬 → 按 90s 一档离散；3/6 分钟两处蛮兵台阶即刻意尖峰）
	// 09-25：妖术师（Boss 级蓄力射线）6 分钟低权重首现——09-28 用户实测"从没遇到过"定罪：
	// 0.06 权重形同虚设 → 提前 3 分钟入场（0.10）并逐档增稠（0.2/0.3），Boss 威胁要见得到
	RatioTiers = {
		{ 0.0f, 0.0f, 1.0f, 0.0f, 0.0f },    // 0 min：标准开路
		{ 1.5f, 0.3f, 0.7f, 0.0f, 0.0f },    // 1.5 min：猎犬入场
		{ 3.0f, 0.25f, 0.5f, 0.2f, 0.1f },   // 3 min：蛮兵首现 + 妖术师入场（尖峰①）
		{ 6.0f, 0.3f, 0.45f, 0.25f, 0.2f },  // 6 min：妖术师增稠（尖峰②）
		{ 9.0f, 0.25f, 0.35f, 0.3f, 0.3f }   // 9 min：终盘高压（妖术师成对出现）
	};
}

void AWaveDirector::BeginPlay()
{
	Super::BeginPlay();

	TotalWaves = Waves.Num();

	// 玩家阵亡监听：终局 Failed（敌人对死者自动停手，尸体不再参与任何结算）
	if (const APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		if (UHealthComponent* PlayerHealth = PlayerPawn->FindComponentByClass<UHealthComponent>())
		{
			PlayerHealth->OnDeath.AddDynamic(this, &AWaveDirector::HandlePlayerDied);
		}
	}

	if (bEndlessMode)
	{
		// Endless：无波次表无喘息，spawn 预算从第一拍开始滚（间隔随时间衰减，R10 公式）
		UE_LOG(LogAICompanionDemo, Warning, TEXT("[Wave] Endless 连续刷怪就绪：目标存活 %.0f 分钟 | 同屏上限 %d | 间隔 %.1fs→%.1fs | 血量 ×%.2f/分钟"),
			SurvivalGoalSeconds / 60.f, MaxAliveEndless, SpawnIntervalBase, SpawnIntervalMin, HpGrowthPerMinute);
	}
	else
	{
		EnterBreak();
		UE_LOG(LogAICompanionDemo, Warning, TEXT("[Wave] WaveDirector 就绪：%d 波 | 环半径 %.0fcm | 同屏上限 %d | 喘息 %.0fs"),
			TotalWaves, SpawnRingRadius, MaxAliveEnemies, InterWaveBreakSeconds);
	}
	GetWorldTimerManager().SetTimer(PhaseTimerHandle, this, &AWaveDirector::UpdatePhase, kPhaseTick, true);
}

void AWaveDirector::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (AAICompanionDemoCharacter* Hero = Cast<AAICompanionDemoCharacter>(UGameplayStatics::GetPlayerPawn(this, 0)))
		Hero->ResetWurmEncounterCamera();
	RegisteredAliveEnemies.Reset();
	// 视口树可比Actor活得更久：先卸载Slate，再释放持有Brush/纹理的UObject。
	HideUpgradeOverlay();
	UpgradeCardView = nullptr;
	Super::EndPlay(EndPlayReason);
}

void AWaveDirector::UpdateEndless()
{
	TickBondHeal();
	TickRadiance();

	if (Phase == EWavePhase::Finished || Phase == EWavePhase::Failed)
	{
		DrawHud();
		return;
	}

	ElapsedSeconds += kPhaseTick;

	// 测试模式（09-30 调试面板）：自动刷怪与祭坛调度全停——全由 DebugPanel 手动控制
	if (bTestMode)
	{
		return;
	}

	ProcessScheduledEvents();

	// （v2 终局：无"存活 600s 胜利"——生存是无尽的，Boss 征服与狂暴生存时长=新成绩维度；死亡=结算）

	// spawn 预算：间隔随时间衰减（max(0.45, 1.6×0.92^分钟)），攒够一只放一只
	const float Interval = FMath::Max(SpawnIntervalMin, SpawnIntervalBase * FMath::Pow(SpawnIntervalDecay, GetElapsedMinutes()));
	SpawnBudget += kPhaseTick / Interval;

	// 动态同屏上限：开局只给一小撮，随时间爬坡（09-20 用户反馈"前期怪太多清不掉"）
	const int32 DynamicMaxAlive = FMath::Min(MaxAliveEndless,
		MaxAliveStart + FMath::RoundToInt(MaxAliveRampPerMinute * GetElapsedMinutes()));

	while (SpawnBudget >= 1.f)
	{
		if (AliveCount >= DynamicMaxAlive)
		{
			// 上限满：预算封顶不无限攒（Megabonk 饱和语义——压力由强度承接而非数量）
			SpawnBudget = FMath::Min(SpawnBudget, 1.f);
			break;
		}
		SpawnBudget -= 1.f;
		const EGruntVariant Rolled = PickEndlessVariant();

		// 妖术师饱和换算（10-07 用户裁决"最多 5 只，第六只起换算为普通小怪"）：
		// 后期妖术师堆积是玩家无法处理的死局——活体 ≥ 上限时，本次妖术师配额整批换算为
		// 随机杂兵批次（猎犬×8 / 标准兵×5 / 蛮兵×3 三选一，威胁量级与单只妖术师相当）
		if (Rolled == EGruntVariant::Warlock)
		{
			const int32 AliveWarlocks = CountAliveWarlocks();
			if (AliveWarlocks >= WarlockAliveLimit)
			{
				static constexpr int32 BatchCounts[] = { 8, 5, 3 };
				static constexpr EGruntVariant BatchKinds[] =
					{ EGruntVariant::Rusher, EGruntVariant::Standard, EGruntVariant::Brute };
				const int32 Pick = FMath::RandRange(0, 2);
				int32 BatchSpawned = 0;
				for (int32 i = 0; i < BatchCounts[Pick]; ++i)
				{
					if (SpawnEnemyAtRing(BatchKinds[Pick]))
					{
						++AliveCount;
						++BatchSpawned;
					}
				}
				UE_LOG(LogAICompanionDemo, Warning, TEXT("[Wave] 妖术师饱和（%d/%d）——配额换算杂兵×%d"),
					AliveWarlocks, WarlockAliveLimit, BatchSpawned);
				continue;   // 预算已扣 1（触发源）；批次逐只另记 AliveCount
			}
		}

		if (SpawnEnemyAtRing(Rolled))
		{
			++AliveCount;
		}
	}

	// 掉队召回（2s 一轮节流）：504m 沙丘地形会把敌人卡在谷里——压力永不断线
	StragglerScanCountdown -= kPhaseTick;
	if (StragglerScanCountdown <= 0.f)
	{
		StragglerScanCountdown = 2.f;
		RecallStragglers();
	}

	DrawHud();
}

void AWaveDirector::RecallStragglers()
{
	const APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!PlayerPawn)
	{
		return;
	}
	const FVector Center = PlayerPawn->GetActorLocation();

	TArray<AActor*> Enemies;
	UGameplayStatics::GetAllActorsOfClass(this, AEnemyGrunt::StaticClass(), Enemies);
	for (AActor* Enemy : Enemies)
	{
		if (!Enemy || FVector(Enemy->GetActorLocation() - Center).Size2D() <= StragglerRecallDistance)
		{
			continue;
		}

		// Megabonk 式重定位：玩家附近环点（半环半径）+ 探地落位——"血厚的怪不会落在后面"
		const float Angle = FMath::FRandRange(0.f, 2.f * PI);
		const FVector Target = Center + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * (SpawnRingRadius * 0.5f);
		FHitResult Hit;
		FCollisionObjectQueryParams Params;
		Params.AddObjectTypesToQuery(ECC_WorldStatic);
		if (GetWorld()->LineTraceSingleByObjectType(Hit, Target + FVector(0.f, 0.f, 8000.f),
			Target - FVector(0.f, 0.f, 6000.f), Params))
		{
			Enemy->SetActorLocation(FVector(Target.X, Target.Y, Hit.ImpactPoint.Z + 100.f));
			UE_LOG(LogAICompanionDemo, Warning, TEXT("[Wave] 掉队召回：%s 传送至玩家侧"), *GetNameSafe(Enemy));
		}
	}
}

EGruntVariant AWaveDirector::PickEndlessVariant() const
{
	// 按存活分钟取最近的配比茬（表须时间升序）
	const FRatioTier* Tier = RatioTiers.Num() > 0 ? &RatioTiers[0] : nullptr;
	for (const FRatioTier& T : RatioTiers)
	{
		if (GetElapsedMinutes() + KINDA_SMALL_NUMBER >= T.AtMinute)
		{
			Tier = &T;
		}
	}
	if (!Tier)
	{
		return EGruntVariant::Standard;
	}

	const float Total = Tier->RusherWeight + Tier->GruntWeight + Tier->BruteWeight + Tier->WarlockWeight;
	if (Total <= 0.f)
	{
		return EGruntVariant::Standard;
	}

	const float Roll = FMath::FRandRange(0.f, Total);
	if (Roll < Tier->RusherWeight)
	{
		return EGruntVariant::Rusher;
	}
	if (Roll < Tier->RusherWeight + Tier->GruntWeight)
	{
		return EGruntVariant::Standard;
	}
	if (Roll < Tier->RusherWeight + Tier->GruntWeight + Tier->BruteWeight)
	{
		return EGruntVariant::Brute;
	}
	return EGruntVariant::Warlock;
}

int32 AWaveDirector::CountAliveWarlocks() const
{
	int32 Count = 0;
	for (TActorIterator<AEnemyGrunt> It(GetWorld()); It; ++It)
	{
		if (!It->IsDead() && It->GruntVariant == EGruntVariant::Warlock)
		{
			++Count;
		}
	}
	return Count;
}

void AWaveDirector::EnterBreak()
{
	Phase = EWavePhase::Break;
	PhaseCountdown = InterWaveBreakSeconds;
}

void AWaveDirector::UpdatePhase()
{
	// 战争迷雾标记（09-28 小地图）：0.5s 节拍随玩家足迹点亮迷雾格——游戏状态归导演
	if (const APawn* FogP = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		MarkExplored(FogP->GetActorLocation());
	}

	if (bEndlessMode)
	{
		UpdateEndless();
		return;
	}

	TickBondHeal();
	TickRadiance();

	switch (Phase)
	{
	case EWavePhase::Break:
		PhaseCountdown -= kPhaseTick;
		if (PhaseCountdown <= 0.f)
		{
			// 开波：装载刷怪队列（Rushers→Grunts→Brutes 出队，蛮兵压阵收尾）
			++CurrentWaveIndex;
			if (!Waves.IsValidIndex(CurrentWaveIndex))
			{
				// 防御：无表可打（空波次表）直接终局，不悬挂在 Break
				Phase = EWavePhase::Finished;
				break;
			}
			const FWaveDef& Def = Waves[CurrentWaveIndex];
			QueueRushers = Def.Rushers;
			QueueGrunts = Def.Grunts;
			QueueBrutes = Def.Brutes;
			Phase = EWavePhase::Spawning;

			UE_LOG(LogAICompanionDemo, Warning, TEXT("[Wave] 第 %d/%d 波来袭（猎犬 %d 标准 %d 蛮兵 %d）"),
				CurrentWaveIndex + 1, TotalWaves, Def.Rushers, Def.Grunts, Def.Brutes);
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(kBannerKey, 3.f, FColor::Red,
					FString::Printf(TEXT("—— 第 %d 波来袭 ——"), CurrentWaveIndex + 1));
			}
		}
		break;

	case EWavePhase::Spawning:
		// 波内节流补刷：活敌低于动态上限才放行下一只（上限随波数爬坡——数量优先的用户节奏；
		// 评分板每帧只画前 8 行候选，超出的敌人靠弹道/横幅感知，可读性护栏由显示层承担）
		while (AliveCount < FMath::Min(MaxAliveEnemies + FMath::Max(CurrentWaveIndex, 0) * MaxAliveRampPerWave, MaxAliveHardCap))
		{
			EGruntVariant Next;
			if (QueueRushers > 0)     { Next = EGruntVariant::Rusher;   --QueueRushers; }
			else if (QueueGrunts > 0) { Next = EGruntVariant::Standard; --QueueGrunts; }
			else if (QueueBrutes > 0) { Next = EGruntVariant::Brute;    --QueueBrutes; }
			else                      { Phase = EWavePhase::WaveActive; break; }

			if (SpawnEnemyAtRing(Next))
			{
				++AliveCount;
			}
			// 生成失败：配额不放回（本波略薄于配置；日志留痕可审计）
		}
		break;

	case EWavePhase::WaveActive:
		if (AliveCount <= 0)
		{
			if (CurrentWaveIndex >= TotalWaves - 1)
			{
				// 全波清空：终局胜利（HUD 常驻，横幅长留供录像取景）
				Phase = EWavePhase::Finished;
				if (GEngine)
				{
					GEngine->AddOnScreenDebugMessage(kBannerKey, 10.f, FColor::Green,
						FString::Printf(TEXT("生存成功！%d 波全部清空 | 击杀 %d"), TotalWaves, KillCount));
				}
				UE_LOG(LogAICompanionDemo, Warning, TEXT("[Wave] 胜利：%d 波全部清空，击杀 %d"), TotalWaves, KillCount);
			}
			else
			{
				UE_LOG(LogAICompanionDemo, Warning, TEXT("[Wave] 第 %d 波清空，%.0fs 后下一波"),
					CurrentWaveIndex + 1, InterWaveBreakSeconds);
				EnterBreak();
			}
		}
		break;

	case EWavePhase::Finished:
	case EWavePhase::Failed:
		break;  // 终局：相位冻结，HUD 仍随节拍刷新供观赛
	}

	DrawHud();
}

AActor* AWaveDirector::SpawnEnemyAtRing(const EGruntVariant Variant)
{
	if (!CanSpawnMob(Variant)) return nullptr;
	UWorld* World = GetWorld();
	const APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!World || !PlayerPawn)
	{
		return nullptr;
	}

	// 地面实证刷怪（隐形敌人案根治，2026-09-10）：环点必须向下探到实地面才准入场——
	// 探不到地的点（平台外虚空/夹层/结构顶）换角重试，整环无地则逐级收缩兜底；
	// 敌人 spawn 在错误层 = 玩家看不见 + 队友看不见 + Size2D 穿层围殴的三重惨案根源
	const FVector Center = PlayerPawn->GetActorLocation();

	// 同层闸的参照系：玩家脚下地面 Z（向下探一次， ObjectType 同款哲学——胶囊是 Pawn 通道不在查询集）。
	// 用"脚下"而非胶囊位置做参照（胶囊中心 ≈ 地面 +96cm，会放大层差误拒）；探不到（悬空中）退回胶囊位置
	FCollisionObjectQueryParams PlayerGroundParams;
	PlayerGroundParams.AddObjectTypesToQuery(ECC_WorldStatic);
	FHitResult PlayerGroundHit;
	const FVector PlayerProbeStart = PlayerPawn->GetActorLocation() + FVector(0.f, 0.f, 50.f);
	const bool bPlayerGroundProbed = World->LineTraceSingleByObjectType(PlayerGroundHit, PlayerProbeStart,
		PlayerProbeStart - FVector(0.f, 0.f, 300.f), PlayerGroundParams);
	const float PlayerGroundZ = bPlayerGroundProbed
		? PlayerGroundHit.ImpactPoint.Z
		: PlayerPawn->GetActorLocation().Z;

	// 可达性终审（墙顶围殴案二阶段，2026-09-11 D14）：同层闸只比高度，辨不了"高而窄的死地"
	//（墙顶/圆柱顶与玩家同层的情形）；NavMesh TestPathSync 问的是完整的问题——"从这里能不能走到玩家身边"。
	// 导航不可用时放行（回退到纯同层闸，绝不因导航缺失而停止刷怪）
	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	const ANavigationData* NavData = NavSys ? NavSys->GetDefaultNavDataInstance(FNavigationSystem::DontCreate) : nullptr;

	FVector GroundPoint = FVector::ZeroVector;
	bool bGrounded = false;
	// 收缩兜底下探到 300（09-19：v15 出生区四周陡降，2500→900 档全在深谷/坡面，高台驻停时需贴身档位救场）
	for (float Radius = SpawnRingRadius; Radius >= 300.f && !bGrounded; Radius *= 0.6f)
	{
		for (int32 Try = 0; Try < 8 && !bGrounded; ++Try)
		{
			const float AngleRad = FMath::FRandRange(0.f, 2.f * PI);
			const FVector RingPoint = Center + FVector(FMath::Cos(AngleRad), FMath::Sin(AngleRad), 0.f) * Radius;

			// ObjectType 只认 WorldStatic（地形/静态几何）——与 SnapToGround"球消失"案同款哲学，
			// 敌人/角色的胶囊不在查询集里，不会被当"地面"
			FHitResult GroundHit;
			// v15：探针起点抬高（钥匙埋地案全项目高空化，+80m 防扎山体）——但终点必须锚"环点绝对深度"
			// 而非相对起点定长（09-28 怪不刷新案定罪：终点 TraceStart-6000 随起点抬升，射线底端从
			// -45m 悬到 +20m，沙丘 ±21m 起伏下半数地面探空=探地闸全灭="刷怪放弃"）——
			// 覆盖 [+80m, -60m]，±21m 起伏全域穿地
			const FVector TraceStart = RingPoint + FVector(0.f, 0.f, 8000.f);
			const FVector TraceEnd = RingPoint - FVector(0.f, 0.f, 6000.f);
			FCollisionObjectQueryParams ObjectParams;
			ObjectParams.AddObjectTypesToQuery(ECC_WorldStatic);
			// 三闸串联（D14 完整语义）：D11 探地闸管"有地" → 同层闸管"同高度带"（v15 容差 800，见头文件）→ 可达闸管"走得到玩家身边"
			const bool bGroundProbed = World->LineTraceSingleByObjectType(GroundHit, TraceStart, TraceEnd, ObjectParams);
			const bool bLayerOk = bGroundProbed && FMath::Abs(GroundHit.ImpactPoint.Z - PlayerGroundZ) <= SpawnMaxLayerDeltaZ;
			if (bLayerOk)
			{
				const FVector CandidatePoint = GroundHit.ImpactPoint + FVector(0.f, 0.f, 100.f);
				bool bReachable = true;
				if (NavData)
				{
					const FPathFindingQuery Query(this, *NavData, CandidatePoint, PlayerPawn->GetActorLocation());
					bReachable = NavSys->TestPathSync(Query, EPathFindingMode::Regular);
					if (!bReachable)
					{
						// 连续全灭防护（09-19 -game 案）：NavMesh 运行时重建期 poly 未就绪，TestPathSync 全假——
						// 连续 16 次不可达视为"导航未就绪"降级放行（哲学一致：绝不因导航缺失停止刷怪）；一次成功即清零恢复严审
						++ConsecutiveUnreachable;
						if (ConsecutiveUnreachable >= 16)
						{
							bReachable = true;   // 降级放行：此后每 16 败放 1 只（败率 6%），导航就绪自动恢复严审
						}
						if (ConsecutiveUnreachable % 8 == 1)
						{
							UE_LOG(LogAICompanionDemo, Warning,
								TEXT("[Wave][Diag] 可达闸连灭 %d：候选=(%.0f,%.0f,%.0f) 玩家=(%.0f,%.0f,%.0f)"),
								ConsecutiveUnreachable, CandidatePoint.X, CandidatePoint.Y, CandidatePoint.Z,
								PlayerPawn->GetActorLocation().X, PlayerPawn->GetActorLocation().Y, PlayerPawn->GetActorLocation().Z);
						}
					}
					else
					{
						ConsecutiveUnreachable = 0;
					}
				}
				if (bReachable)
				{
					GroundPoint = CandidatePoint;
					bGrounded = true;
				}
			}
			else if (++DiagFailCount % 8 == 1)
			{
				// 采样诊断（09-19 -game 三闸全灭案）：8 败打 1 条详单——探地/同层谁死、关键 Z 值
				UE_LOG(LogAICompanionDemo, Warning,
					TEXT("[Wave][Diag] 环点详单：探地%s 同层%s hitZ=%.0f playerGroundZ=%.0f(探%s) 环点=(%.0f,%.0f) 玩家=(%.0f,%.0f,%.0f)"),
					bGroundProbed ? TEXT("中") : TEXT("脱"),
					bGroundProbed ? (FMath::Abs(GroundHit.ImpactPoint.Z - PlayerGroundZ) <= SpawnMaxLayerDeltaZ ? TEXT("中") : TEXT("脱")) : TEXT("-"),
					bGroundProbed ? GroundHit.ImpactPoint.Z : -9999.f,
					PlayerGroundZ, bPlayerGroundProbed ? TEXT("中") : TEXT("脱"),
					RingPoint.X, RingPoint.Y,
					PlayerPawn->GetActorLocation().X, PlayerPawn->GetActorLocation().Y, PlayerPawn->GetActorLocation().Z);
			}
		}
	}
	if (!bGrounded)
	{
		UE_LOG(LogAICompanionDemo, Warning, TEXT("[Wave] 刷怪放弃：玩家周围无实证地面（本波配额减一）"));
		return nullptr;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	const TSubclassOf<AActor> ResolvedClass = EnemyClass ? EnemyClass : TSubclassOf<AActor>(AEnemyGrunt::StaticClass());
	AActor* Enemy = World->SpawnActor<AActor>(ResolvedClass, GroundPoint, FRotator::ZeroRotator, Params);
	if (!Enemy)
	{
		UE_LOG(LogAICompanionDemo, Warning, TEXT("[Wave] 刷怪失败 @ %s"), *GroundPoint.ToCompactString());
		return nullptr;
	}

	// 变体注入：出生即定型（速度/血量/伤害/体型/色调/经验值）；非假人形体（球体安全网）无此能力，按基线参战
	if (AEnemyGrunt* Grunt = Cast<AEnemyGrunt>(Enemy))
	{
		Grunt->ApplyVariant(Variant);
		Grunt->SetEnraged(bEnraged);
		// 难度成长二选一：Endless 走时间曲线（R10：VS 数值 15%/2.5% 每分钟），波次机走波数线性（+5%/波）
		const float Minutes = GetElapsedMinutes();
		if (UHealthComponent* GruntHealth = Grunt->GetHealthComponent())
		{
			float HpFactor = bEndlessMode
				? FMath::Pow(HpGrowthPerMinute, Minutes)
				: 1.f + FMath::Max(CurrentWaveIndex, 0) * 0.05f;
			// 狂暴换挡（09-30 v2：12min 起血量指数换 1.35/分，不封顶——MegaBonk 式 insane）
			if (bEnraged)
			{
				HpFactor *= FMath::Pow(1.35f, Minutes - 12.f);
			}
			GruntHealth->SetMaxHealth(GruntHealth->GetMaxHealth() * HpFactor, /*bFillCurrent*/ true);
		}
		if (bEndlessMode && Grunt->GetCharacterMovement())
		{
			float SpeedFactor = FMath::Min(FMath::Pow(SpeedGrowthPerMinute, Minutes), SpeedGrowthCap);
			// 狂暴速度换挡：脱帽加速（压迫力拉满，逃不掉）
			if (bEnraged)
			{
				SpeedFactor *= FMath::Pow(1.15f, Minutes - 12.f);
			}
			Grunt->GetCharacterMovement()->MaxWalkSpeed *= FMath::Min(SpeedFactor, SpeedGrowthCap * 2.5f);
		}
		// 难度祭坛注入（09-25）：献祭档位的血量/经验乘子最后落位（叠加在时间成长之上）
		if (DifficultyLevel > 0)
		{
			Grunt->ApplyDifficulty(GetDifficultyHpMult(), GetDifficultyXpMult());
		}
	}

	// 死亡回调：全灭检测与击杀计数的信号源（ICombatant 契约跨形体拿血量组件）
	if (const ICombatant* Combatant = Cast<ICombatant>(Enemy))
	{
		if (UHealthComponent* Health = Combatant->GetHealthComponent())
		{
			Health->OnDeath.AddDynamic(this, &AWaveDirector::HandleEnemyDied);
			RegisteredAliveEnemies.Add(Enemy);
			Enemy->OnDestroyed.AddDynamic(this, &AWaveDirector::HandleEnemyRemoved);
		}
	}

	UE_LOG(LogAICompanionDemo, Warning, TEXT("[Wave] spawned %s（变体 %d）@ %s"),
		*GetNameSafe(Enemy), static_cast<int32>(Variant), *Enemy->GetActorLocation().ToCompactString());
	return Enemy;
}

void AWaveDirector::HandleEnemyDied(UHealthComponent* HealthComp)
{
	const TWeakObjectPtr<UHealthComponent> DeathKey(HealthComp);
	if (!IsValid(HealthComp) || !HealthComp->IsDead() || CountedEnemyDeaths.Contains(DeathKey)) return;
	CountedEnemyDeaths.Add(DeathKey);
	RegisteredAliveEnemies.Remove(HealthComp->GetOwner());
	for (auto It = CountedEnemyDeaths.CreateIterator(); It; ++It) { if (!It->IsValid()) It.RemoveCurrent(); }
	AliveCount = FMath::Max(AliveCount - 1, 0);
	++KillCount;
	NotifyMobDeath(HealthComp->GetOwner());
	// 尸体 3s 存留期与活敌计数解耦：评分器早已剔除死者（IsDead），导演只关心"还有几只能打"
}

void AWaveDirector::HandlePlayerDied(UHealthComponent* HealthComp)
{
	// 选卡冻结期间的同帧致死仍需给死亡布娃娃真实1.5s，最终冻结由DeathScreen接管。
	if (bUpgradePending) UGameplayStatics::SetGlobalTimeDilation(this, 1.f);
	Phase = EWavePhase::Failed;
	RefreshWurmEncounter();
	PendingChoices.Reset();
	bUpgradePending = false;
	bRelicPending = false;
	if (UpgradeOverlay.IsValid() && GEngine && GEngine->GameViewport)
	{ GEngine->GameViewport->RemoveViewportWidgetContent(UpgradeOverlay.ToSharedRef()); }
	UpgradeOverlay.Reset(); // DeathScreen owns input; do not call HideUpgradeOverlay here.
	const int32 SurvivedSec = static_cast<int32>(ElapsedSeconds);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(kBannerKey, 10.f, FColor::Red,
			bEndlessMode
				? FString::Printf(TEXT("你已阵亡——存活 %d:%02d，击杀 %d（R 重开随 C2 交付）"), SurvivedSec / 60, SurvivedSec % 60, KillCount)
				: FString::Printf(TEXT("你已阵亡——存活至第 %d 波，击杀 %d（R 重开随 C2 交付）"), CurrentWaveIndex + 1, KillCount));
	}
	UE_LOG(LogAICompanionDemo, Warning, TEXT("[Wave] 玩家阵亡：%s，击杀 %d"),
		bEndlessMode ? *FString::Printf(TEXT("存活 %d:%02d"), SurvivedSec / 60, SurvivedSec % 60)
		             : *FString::Printf(TEXT("存活至第 %d 波"), CurrentWaveIndex + 1),
		KillCount);
}

void AWaveDirector::DrawHud() const
{
	if (!GEngine)
	{
		return;
	}

	// 快照出口（09-21 G1）：Endless 展示数据已迁 UMG HUD（CompanionHud 轮询），此处仅非 Endless 回归安全网

	// Endless HUD：波次行换成存活时长 + 当前强度倍率
	// 09-21 G1：Endless 行已迁 UMG（CompanionHud）——裸字版退役，避免与真 HUD 双显
	if (bEndlessMode)
	{
		return;
	}

	FString PhaseTag;
	switch (Phase)
	{
	case EWavePhase::Break:      PhaseTag = FString::Printf(TEXT("下一波 %.0fs"), FMath::Max(PhaseCountdown, 0.f)); break;
	case EWavePhase::Spawning:   PhaseTag = TEXT("增援中"); break;
	case EWavePhase::WaveActive: PhaseTag = TEXT("交战中"); break;
	case EWavePhase::Finished:   PhaseTag = TEXT("胜利"); break;
	case EWavePhase::Failed:     PhaseTag = TEXT("阵亡"); break;
	}

	const FString Line = FString::Printf(TEXT("第 %d/%d 波 | %s | 活敌 %d | 击杀 %d | Lv.%d XP %d/%d | 金 %d"),
		FMath::Max(CurrentWaveIndex + 1, 1), TotalWaves, *PhaseTag, AliveCount, KillCount, Level, CurrentXp, XpToNextLevel, GoldCount);
	GEngine->AddOnScreenDebugMessage(kHudKey, kPhaseTick * 2.f, FColor::Yellow, Line);
}
