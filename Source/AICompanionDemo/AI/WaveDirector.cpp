/**
 * [INPUT]: 依赖 WaveDirector.h 的相位机契约、AI/EnemyGrunt 的 ApplyVariant 变体注入、AI/Combatant 的 ICombatant（GetHealthComponent 绑死亡）、Engine 的 TimerManager/SpawnActor/DrawDebugString/GEngine 屏幕消息、HealthComponent 的 OnDeath 多播、Kismet 的 GetPlayerPawn、本模块 Audio/SFXLibrary（UpgradeChoice 事件音效）；UI/UpgradeCardView（无字纹理、独立短文案与纯Slate展示）
 * [OUTPUT]: 实现AWaveDirector的波次/Endless节奏、三闸环形刷怪、XP/金币账本、领域与治疗节拍；升级冻结世界后按权重供给候选，显式提供展示稀有度与四融合标志，UpgradeCardView渲染三选一/六选二；鼠标与数字键共用槽位校验，ApplyUpgrade统一应用效果与恢复流速；按稳定升级ID记录次数并输出排序后的HUD图标快照。EndPlay先拆视口树再释放视图与纹理。
 *  选择出口串行续接；玩家穿透与折射可同持、散射仍互斥；AI穿透折射互斥。感电基础20/过载40下限与比例取高，Nova状态文案同步；虫战/遗物HUD局部实现各持真源。
 * [POS]: AI/ 的玩法节奏中枢实现；GameMode 组合根负责生成它并注入敌人类——"何时何地谁入场"从 GameMode 迁入此处，GameMode 只保留配置与装配职责
 * [PROTOCOL]: 变更时更新此头部，然后检查 ../AGENTS.md
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

namespace
{
	// 相位节拍（秒）：也是波内补刷的粒度与 HUD 刷新频率
	constexpr float kPhaseTick = 0.5f;

	// 屏幕消息 key 池：4=旧波次HUD行、5=事件横幅；队友评分板20+默认关闭。
	constexpr int32 kHudKey = 4;
	constexpr int32 kBannerKey = 5;
}

AWaveDirector::AWaveDirector()
{
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

// ---------- Endless 连续刷怪（2026-09-18，R10 调研数值） ----------

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

	// ---------- 三 Boss 终局（09-30 用户定案）：时间过档即冒祭坛（玩家选择应战时机） ----------
	if (BossAltarTimes.Num() > 0 && BossAltarSpawned.Num() != BossAltarTimes.Num())
	{
		BossAltarSpawned.Init(false, BossAltarTimes.Num());
	}
	for (int32 i = 0; i < BossAltarTimes.Num(); ++i)
	{
		if (!BossAltarSpawned[i] && ElapsedSeconds >= BossAltarTimes[i])
		{
			BossAltarSpawned[i] = true;
			if (const APawn* P = UGameplayStatics::GetPlayerPawn(this, 0))
			{
				// 祭坛落点：玩家周围 60-80m 随机角（足够近有威胁、足够远有反应时间）
				const float Angle = FMath::FRandRange(0.f, 2.f * PI);
				const float Dist = FMath::FRandRange(6000.f, 8000.f);
				const FVector Loc(P->GetActorLocation().X + FMath::Cos(Angle) * Dist,
					P->GetActorLocation().Y + FMath::Sin(Angle) * Dist, P->GetActorLocation().Z);
				if (ABossAltar* Altar = GetWorld()->SpawnActor<ABossAltar>(Loc, FRotator::ZeroRotator))
				{
					const TSubclassOf<ABossBase> SlotClass = BossClasses.IsValidIndex(i) ? BossClasses[i] : BossClasses[0];
					Altar->SetBossInfo(SlotClass,
						BossNames.IsValidIndex(i) ? BossNames[i] : FString::Printf(TEXT("BOSS %d"), i + 1), i + 1);
					USFXLibrary::PlaySFX(GetWorld(), EGameSFX::BossWarn);   // [SFX] 祭坛警报（危机降临的"咚"——09-30 用户点名）
					if (GEngine)
					{
						GEngine->AddOnScreenDebugMessage(kBannerKey, 6.f, FColor::Red,
							FString::Printf(TEXT("☠ %s 的祭坛已出现——循红色光柱前往！"),
								BossNames.IsValidIndex(i) ? *BossNames[i] : TEXT("BOSS")));
					}
					UE_LOG(LogAICompanionDemo, Warning, TEXT("[Boss] %d 号祭坛冒出 @ %s"), i + 1,
						*Loc.ToCompactString());
				}
			}
		}
	}

	// 狂暴态（09-30 用户裁决 v2：12min 起 MegaBonk 式 insane——速度/血量换挡不封顶，死亡即结算）
	if (!bEnraged && ElapsedSeconds >= 720.f)
	{
		SetEnraged(true);
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(kBannerKey, 8.f, FColor::Red, TEXT("☠ 狂 暴 开 始 —— 逃 是 逃 不 掉 的"));
		}
		UE_LOG(LogAICompanionDemo, Warning, TEXT("[Wave] 狂暴态开启——怪物曲线换挡不封顶"));
	}

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

void AWaveDirector::AddXp(const int32 Amount)
{
	if (IsRunTerminated() || Amount <= 0)
	{
		return;
	}

	CurrentXp += Amount;
	while (CurrentXp >= XpToNextLevel)
	{
		CurrentXp -= XpToNextLevel;
		++Level;
		XpToNextLevel = FMath::RoundToInt(XpToNextLevel * 1.4f);

		// 升级 = 进三选一暂停（C1 落地：世界冻结抽卡，数字键选取）
		UE_LOG(LogAICompanionDemo, Warning, TEXT("[Wave] 升级 Lv.%d（下级需 %d XP）——进入三选一"), Level, XpToNextLevel);
		EnterUpgradePause();
	}
}

void AWaveDirector::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	TickWurmPressure(DeltaSeconds);

	// 常态零开销：Tick 只服务升级暂停的数字键轮询（世界 TimeDilation=0 时 TimerManager 停摆，
	// 轮询必须有常驻心跳——actor Tick 不受膨胀影响，仅 dt=0）
	if (!bUpgradePending)
	{
		ProcessNextChoice();
		if (!bUpgradePending) { return; }
	}

	APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	if (!PC)
	{
		return;
	}

	static const FKey ChoiceKeys[] = { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six };
	for (int32 Slot = 0; Slot < (bChestDoublePick ? 6 : 3); ++Slot)
	{
		if (PC->WasInputKeyJustPressed(ChoiceKeys[Slot]))
		{
			if (bRelicPending) { SelectRelicOffer(Slot); }
			else { SelectUpgradeOffer(Slot); }
			break;
		}
	}
}

void AWaveDirector::EnterUpgradePause(const bool bChestPool)
{
	if (IsRunTerminated()) { return; }
	if (bUpgradePending) { PendingChoices.Add(bChestPool ? 1 : 0); return; }
	UGameplayStatics::SetGlobalTimeDilation(this, 0.f);
	bUpgradePending = true;
	RollOffers(bChestPool);
	if (bChestPool)
	{
		bChestDoublePick = true;
		ChestPicksRemaining = 2;   // 09-30 用户裁决"大宝箱 6 选 2"——拿两个东西才配叫大奖
		ChestTakenSlots.Reset();   // 已选记录清零（新的一局）
	}
	ShowUpgradeOverlay(bChestPool);
	USFXLibrary::PlaySFX(GetWorld(), EGameSFX::UpgradeChoice);   // [SFX] 三选一弹出（2D 音不受时间膨胀冻结影响）
	if (bChestPool)
	{
		UE_LOG(LogAICompanionDemo, Warning, TEXT("[Wave] 锁箱六选二：[%s] [%s] [%s] [%s] [%s] [%s]"),
			*DescribeCard(ChestOffers[0]), *DescribeCard(ChestOffers[1]), *DescribeCard(ChestOffers[2]),
			*DescribeCard(ChestOffers[3]), *DescribeCard(ChestOffers[4]), *DescribeCard(ChestOffers[5]));
	}
	else
	{
		UE_LOG(LogAICompanionDemo, Warning, TEXT("[Wave] 三选一：[%s] [%s] [%s]"),
			*DescribeCard(Offers[0]), *DescribeCard(Offers[1]), *DescribeCard(Offers[2]));
	}
}

void AWaveDirector::ShowUpgradeOverlay(const bool bChest)
{
	if (IsRunTerminated() || UpgradeOverlay.IsValid() || !GEngine || !GEngine->GameViewport)
	{
		return;
	}
	if (!UpgradeCardView)
	{
		UpgradeCardView = NewObject<UUpgradeCardView>(this);
	}
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		PC->bShowMouseCursor = true;
		FInputModeGameAndUI InputMode;
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PC->SetInputMode(InputMode);
	}

	TArray<FUpgradeCardOption> Options;
	const uint32 Generation = ++ChoiceGeneration;
	for (int32 Slot = 0; Slot < (bChest ? 6 : 3); ++Slot)
	{
		const EUpgradeId Id = bChest ? ChestOffers[Slot] : Offers[Slot];
		const ECardRarity Rarity = CardRarityOf(Id);
		FUpgradeCardOption& Option = Options.AddDefaulted_GetRef();
		Option.UpgradeId = UpgradeCardKey(Id);
		Option.Slot = Slot;
		Option.bTaken = bChest && ChestTakenSlots.Contains(Slot);
		Option.Rarity = PresentationRarity(Rarity);
		Option.bFusionLegendary = IsFusionLegendary(Id);
		Option.FullDescription = DescribeCard(Id);
		Option.ShortDescriptionOverride = UpgradeCardDescription(Id);
		Option.OnSelected = FSimpleDelegate::CreateWeakLambda(this, [this, Slot, Generation]()
		{ if (Generation == ChoiceGeneration) { SelectUpgradeOffer(Slot); } });
	}
	const FString Heading = bChest ? TEXT("锁箱大奖 · 六选二") : FString::Printf(TEXT("升级 · Lv.%d"), Level);
	const FString Hint = bChest ? FString::Printf(TEXT("还可领取 %d 张 · 点击卡片或按 1–6"), ChestPicksRemaining)
		: TEXT("选择一项强化 · 点击卡片或按 1 / 2 / 3");
	UpgradeOverlay = UpgradeCardView->Build(Options, Heading, Hint, bChest);
	GEngine->GameViewport->AddViewportWidgetContent(UpgradeOverlay.ToSharedRef(), 100);
}

void AWaveDirector::SelectUpgradeOffer(const int32 Slot)
{
	if (IsRunTerminated()) { return; }
	if (bRelicPending) { SelectRelicOffer(Slot); return; }
	const int32 Count = bChestDoublePick ? 6 : 3;
	if (!bUpgradePending || Slot < 0 || Slot >= Count
		|| (bChestDoublePick && (ChestPicksRemaining <= 0 || ChestTakenSlots.Contains(Slot))))
	{
		return;
	}
	if (bChestDoublePick)
	{
		ChestTakenSlots.Add(Slot);
	}
	ApplyUpgrade(bChestDoublePick ? ChestOffers[Slot] : Offers[Slot]);
}

void AWaveDirector::HideUpgradeOverlay()
{
	if (UpgradeOverlay.IsValid() && GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(UpgradeOverlay.ToSharedRef());
	}
	UpgradeOverlay.Reset();

	// 恢复游戏输入模式（与 09-26 输入三态机的游戏态对齐：鼠标隐藏+GameOnly）
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		PC->SetInputMode(FInputModeGameOnly());
	}

	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		PC->bShowMouseCursor = false;
	}
}

void AWaveDirector::RollOffers(const bool bChestPool)
{
	// 卡池 v2（用户 09-12 二次定调：火力特化+感知优先；协议卡下架——评分板权重对玩家不可感知）
	// v3 追加（用户 2026-09-13）：折射（与穿透互斥入池）/ 疾行（移速可叠）/ 二段跳
	// 稀有度显示使用普通蓝/稀有紫/传说金；抽取权重乘子保持原值。
	//    （传说 0.45：质变技能与融合卡"该稀有但要能等到"）；爆炸归传说并对冲数值削弱（太厉害）
	TArray<FUpgradeCardDef> Pool = {
		{ EUpgradeId::Blink,        TEXT("Skill"),    1.0f, TEXT("Blink"),      ECardRarity::Epic },
		{ EUpgradeId::Scatter,      TEXT("Skill"),    1.0f, TEXT("Scatter"),    ECardRarity::Rare },
		{ EUpgradeId::PierceRounds, TEXT("Skill"),    1.0f, TEXT("PierceRounds"), ECardRarity::Epic },
		{ EUpgradeId::PrismRounds,  TEXT("Skill"),    1.0f, TEXT("PrismRounds"), ECardRarity::Epic },
		{ EUpgradeId::RapidFire,    TEXT("Firepower"),0.9f, NAME_None,          ECardRarity::Common },
		{ EUpgradeId::HeavyRounds,  TEXT("Firepower"),0.9f, NAME_None,          ECardRarity::Rare },
		{ EUpgradeId::FrostRounds,     TEXT("Firepower"), 0.8f, NAME_None,       ECardRarity::Rare },
		{ EUpgradeId::Radiance,     TEXT("Domain"),   0.9f, NAME_None,          ECardRarity::Epic },   // 10-08 辉耀成长：再抽范围+1m/每0.5s伤害+10
		{ EUpgradeId::BuddyHealing, TEXT("Bond"),     0.8f, NAME_None,          ECardRarity::Common },  // 09-29 叠加化：再抽 +1 HP/拍
		{ EUpgradeId::HealNova,     TEXT("Bond"),     0.7f, NAME_None,          ECardRarity::Rare },    // 09-29 叠加化：再抽 阈值-5/伤害+10
		{ EUpgradeId::GlassCannon,  TEXT("Contract"), 0.8f, TEXT("GlassCannon"), ECardRarity::Rare },
		{ EUpgradeId::BloodSacrifice,TEXT("Contract"),0.8f, TEXT("BloodSacrifice"), ECardRarity::Epic },
		{ EUpgradeId::Vigor,        TEXT("Growth"),   0.4f, NAME_None,          ECardRarity::Common },
		{ EUpgradeId::SwiftMove,    TEXT("Growth"),   0.7f, NAME_None,          ECardRarity::Common },
		// 元素三卡（09-29 十二节：玩家=状态制造器——冰霜/感电/印记三线，09-30 改互斥"选线走线"）
		{ EUpgradeId::FrostShotPlayer, TEXT("ElementFrost"), 0.85f, TEXT("FrostShotPlayer"), ECardRarity::Rare },
		{ EUpgradeId::ShockShot,       TEXT("ElementShock"), 0.85f, TEXT("ShockShot"),       ECardRarity::Rare },
		{ EUpgradeId::HuntMark,        TEXT("ElementMark"),  0.85f, TEXT("HuntMark"),        ECardRarity::Rare },
		// 玩家弹道三线（09-30 用户裁决"散射/穿透/折射也作为玩家武器升级，与 AI 完全独立"）：组内互斥三选一
		{ EUpgradeId::PlayerScatter, TEXT("PlayerFire"), 0.8f, TEXT("PlayerScatter"), ECardRarity::Rare },
		{ EUpgradeId::PlayerPierce,  TEXT("PlayerFire"), 0.8f, TEXT("PlayerPierce"),  ECardRarity::Rare },
		{ EUpgradeId::PlayerPrism,   TEXT("PlayerFire"), 0.8f, TEXT("PlayerPrism"),   ECardRarity::Rare },
	};
	// 元素三卡前置（09-30 用户裁决"Lv.6 解锁主动攻击之前别出本体攻击天赋"）：玩家没枪时
	// 元素弹头/玩家弹道全是废卡，白占三选一位还劝退前期——射击未解锁前整组离池
	if (const APawn* RollPawn = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		if (const AAICompanionDemoCharacter* RollChar = Cast<AAICompanionDemoCharacter>(RollPawn);
			RollChar && !RollChar->bPlayerShootingUnlocked)
		{
			Pool.RemoveAll([](const FUpgradeCardDef& C)
			{
				const FString G = C.Group.ToString();
				return G.StartsWith(TEXT("Element")) || G == TEXT("PlayerFire");
			});
		}
	}

	// 元素深化三卡（09-30 弹药互斥配套"选线走线"）：主卡解锁才入池，一次性——选线后的专属成长
	if (UnlockedIds.Contains(TEXT("ShockShot")))
	{
		Pool.Add({ EUpgradeId::ShockOverload, TEXT("ElementShock"), 1.0f, TEXT("ShockOverload"), ECardRarity::Epic });
	}
	if (UnlockedIds.Contains(TEXT("HuntMark")))
	{
		Pool.Add({ EUpgradeId::MarkMaster, TEXT("ElementMark"), 1.0f, TEXT("MarkMaster"), ECardRarity::Epic });
	}
	if (UnlockedIds.Contains(TEXT("FrostShotPlayer")))
	{
		Pool.Add({ EUpgradeId::FrostDeep, TEXT("ElementFrost"), 1.0f, TEXT("FrostDeep"), ECardRarity::Epic });
	}

	// 弹药互斥（09-30 用户裁决"选一种子弹后只出现与它相关的强化"）：三元素线互相排斥——
	// 任一线主卡已解锁，另两线全部离池（本线深化卡随主线保留）
	if (UnlockedIds.Contains(TEXT("ShockShot")) || UnlockedIds.Contains(TEXT("HuntMark")) || UnlockedIds.Contains(TEXT("FrostShotPlayer")))
	{
		Pool.RemoveAll([this](const FUpgradeCardDef& C)
		{
			const FString G = C.Group.ToString();
			if (!G.StartsWith(TEXT("Element")))
			{
				return false;
			}
			return !((G == TEXT("ElementShock") && UnlockedIds.Contains(TEXT("ShockShot"))) ||
				(G == TEXT("ElementMark") && UnlockedIds.Contains(TEXT("HuntMark"))) ||
				(G == TEXT("ElementFrost") && UnlockedIds.Contains(TEXT("FrostShotPlayer"))));
		});
	}
	// 玩家低伤弹允许穿透+折射同持；散射仍与这两种互斥。OneShotKey独立排除已领卡。
	if (UnlockedIds.Contains(TEXT("PlayerScatter")))
	{
		Pool.RemoveAll([](const FUpgradeCardDef& C)
		{
			return C.Group == TEXT("PlayerFire");
		});
	}
	else if (UnlockedIds.Contains(TEXT("PlayerPierce")) || UnlockedIds.Contains(TEXT("PlayerPrism")))
	{
		Pool.RemoveAll([](const FUpgradeCardDef& C) { return C.Id == EUpgradeId::PlayerScatter; });
	}
	// DoubleJump 已摘除（09-18 用户裁决：v15 大陆无高台）

	// 融合进阶卡（前置齐备才入池——"两种升级合体"的 mindblowing 时刻，权重高配让玩家见得到）——全传说
	if (UnlockedIds.Contains(TEXT("Scatter")) && UnlockedIds.Contains(TEXT("PierceRounds")))
	{
		Pool.Add({ EUpgradeId::Barrage, TEXT("Fusion"), 1.3f, TEXT("Barrage"), ECardRarity::Epic });
	}
	if (UnlockedIds.Contains(TEXT("Blink")) && UnlockedIds.Contains(TEXT("Scatter")))
	{
		Pool.Add({ EUpgradeId::BlinkBomb, TEXT("Fusion"), 1.3f, TEXT("BlinkBomb"), ECardRarity::Epic });
	}
	if (UnlockedIds.Contains(TEXT("Radiance")) && UnlockedIds.Contains(TEXT("RapidFire")))
	{
		Pool.Add({ EUpgradeId::RadianceOverload, TEXT("Fusion"), 1.3f, TEXT("RadianceOverload"), ECardRarity::Epic });
	}
	// 融合·辉耀结界（09-25 用户点名"辉耀领域和贴贴回复融合"）：圈内即贴贴互回
	if (UnlockedIds.Contains(TEXT("Radiance")) && UnlockedIds.Contains(TEXT("BuddyHealing")))
	{
		Pool.Add({ EUpgradeId::RadianceBond, TEXT("Fusion"), 1.3f, TEXT("RadianceBond"), ECardRarity::Epic });
	}

	// 旧高爆弹头保留枚举与调试兼容，但不进入普通/宝箱池；三Boss现役奖励为各自遗物三选一。
	// 不再由蠕虫击杀自动发放；RemoveAll 保留防线，防止旧卡误加回。
	Pool.RemoveAll([](const FUpgradeCardDef& C) { return C.Id == EUpgradeId::ExplosiveRounds; });

	// 稀有度权重乘子（09-25）：普通 1.0 / 稀有 0.75 / 传说 0.45——"质变要稀有，但三选一里要能等到"
	for (FUpgradeCardDef& C : Pool)
	{
		C.Weight *= C.Rarity == ECardRarity::Epic ? 0.45f
			: (C.Rarity == ECardRarity::Rare ? 0.75f : 1.0f);
	}

	// 已抽走的一次性卡不入池（火力卡可叠加：速射/重击每抽一次再乘）；
	// HuntMark 豁免（09-29）：首抽解锁后仍留池——再抽=印记强化（上限 +1，文档 B 成长线）
	Pool.RemoveAll([this](const FUpgradeCardDef& C)
	{
		return C.OneShotKey != NAME_None && C.Id != EUpgradeId::HuntMark && UnlockedIds.Contains(C.OneShotKey);
	});

	// 折射 ↔ 穿透互斥（用户 2026-09-13 定调"选了折射就和穿透互斥"）：任一已解锁，另一张离池
	if (UnlockedIds.Contains(TEXT("PrismRounds")) || UnlockedIds.Contains(TEXT("PierceRounds")))
	{
		Pool.RemoveAll([](const FUpgradeCardDef& C)
		{
			return C.Id == EUpgradeId::PrismRounds || C.Id == EUpgradeId::PierceRounds;
		});
	}

	// 池况留痕（09-24 崩溃案取证）：离池条件多（OneShot/互斥/融合前置），空池路径难脑补——日志给证据
	UE_LOG(LogAICompanionDemo, Log, TEXT("[Wave] 卡池 %d 张入抽（%s）"), Pool.Num(), bChestPool ? TEXT("锁箱六选二") : TEXT("普通三选一"));

	// 抽卡循环（09-30 锁箱六选二：bChestPool 抽 6 张入 ChestOffers，普通抽 3 张入 Offers）
	const int32 DrawCount = bChestPool ? 6 : 3;
	for (int32 Slot = 0; Slot < DrawCount; ++Slot)
	{
		// 池干涸保底（09-24 开箱崩溃案）：一次性卡拿太多时，池可被"互斥/OneShot 离池 + 同组抽走"三轮内抽干，
		// 旧代码 Pool[Num()-1] = Pool[-1] 直接断言炸编辑器——保底用 Vigor（成长安全垫：可无限叠加、
		// 无 OneShotKey 不入离池逻辑、组离池不影响它再被填）
		if (Pool.IsEmpty())
		{
			// 干涸保底（09-30 升级：锁箱模式不再 Vigor 无限刷——用户实测"只能重复选择两个选项"定罪
			// 中期池薄+保底循环；金币袋=直接收益的厚礼填位，无限供给不重复抽）
			const EUpgradeId Filler = bChestPool ? EUpgradeId::GoldPouch : EUpgradeId::Vigor;
			if (bChestPool)
			{
				ChestOffers[Slot] = Filler;
			}
			else
			{
				Offers[Slot] = Filler;
			}
			continue;
		}

		float Total = 0.f;
		for (const FUpgradeCardDef& C : Pool)
		{
			Total += C.Weight;
		}

		float Roll = FMath::FRandRange(0.f, Total);
		int32 Picked = Pool.Num() - 1;
		for (int32 i = 0; i < Pool.Num(); ++i)
		{
			Roll -= Pool[i].Weight;
			if (Roll <= 0.f)
			{
				Picked = i;
				break;
			}
		}

		if (bChestPool)
		{
			ChestOffers[Slot] = Pool[Picked].Id;
		}
		else
		{
			Offers[Slot] = Pool[Picked].Id;
		}

		// 同组至多一张：抽中后整组离池（含所抽卡本身）
		const FName Group = Pool[Picked].Group;
		Pool.RemoveAll([&Group](const FUpgradeCardDef& C) { return C.Group == Group; });
	}
}

FName AWaveDirector::UpgradeCardKey(const EUpgradeId Id)
{
	switch (Id)
	{
	case EUpgradeId::Blink: return TEXT("Blink");
	case EUpgradeId::Scatter: return TEXT("Scatter");
	case EUpgradeId::PierceRounds: return TEXT("PierceRounds");
	case EUpgradeId::RapidFire: return TEXT("RapidFire");
	case EUpgradeId::HeavyRounds: return TEXT("HeavyRounds");
	case EUpgradeId::Radiance: return TEXT("Radiance");
	case EUpgradeId::BuddyHealing: return TEXT("BuddyHealing");
	case EUpgradeId::GlassCannon: return TEXT("GlassCannon");
	case EUpgradeId::BloodSacrifice: return TEXT("BloodSacrifice");
	case EUpgradeId::Vigor: return TEXT("Vigor");
	case EUpgradeId::Barrage: return TEXT("Barrage");
	case EUpgradeId::BlinkBomb: return TEXT("BlinkBomb");
	case EUpgradeId::RadianceOverload: return TEXT("RadianceOverload");
	case EUpgradeId::PrismRounds: return TEXT("PrismRounds");
	case EUpgradeId::SwiftMove: return TEXT("SwiftMove");
	case EUpgradeId::ExplosiveRounds: return TEXT("ExplosiveRounds");
	case EUpgradeId::FrostRounds: return TEXT("FrostRounds");
	case EUpgradeId::RadianceBond: return TEXT("RadianceBond");
	case EUpgradeId::HealNova: return TEXT("HealNova");
	case EUpgradeId::FrostShotPlayer: return TEXT("FrostShotPlayer");
	case EUpgradeId::ShockShot: return TEXT("ShockShot");
	case EUpgradeId::HuntMark: return TEXT("HuntMark");
	case EUpgradeId::ShockOverload: return TEXT("ShockOverload");
	case EUpgradeId::MarkMaster: return TEXT("MarkMaster");
	case EUpgradeId::FrostDeep: return TEXT("FrostDeep");
	case EUpgradeId::PlayerScatter: return TEXT("PlayerScatter");
	case EUpgradeId::PlayerPierce: return TEXT("PlayerPierce");
	case EUpgradeId::PlayerPrism: return TEXT("PlayerPrism");
	case EUpgradeId::GoldPouch: return TEXT("GoldPouch");
	}
	return NAME_None;
}

FString AWaveDirector::UpgradeCardDescription(const EUpgradeId Id) const
{
	// 叠加卡显示本次收益，避免再次抽到时仍写首层基线；完整说明继续复用DescribeCard。
	if (Id == EUpgradeId::Radiance && bRadianceUnlocked)
	{
		return TEXT("辉耀半径增加 1m\n每0.5s灼烧增加10伤。");
	}
	if (Id == EUpgradeId::BuddyHealing && bBondHealUnlocked)
	{
		return TEXT("每拍治疗量增加 1\n范围内双方持续回血。");
	}
	return FString();
}

FString AWaveDirector::DescribeCard(EUpgradeId Id) const
{
	switch (Id)
	{
	case EUpgradeId::Blink:        return TEXT("闪烁（技能）｜解锁 Shift 闪现：朝移动方向瞬移8m（穿人穿怪），CD5s——走位即保命");
	case EUpgradeId::Scatter:      return TEXT("散射（技能）｜队友每发分裂三向弹道（±15°），侧向 70% 伤害——清杂爽感");
	case EUpgradeId::PierceRounds: return TEXT("穿透弹体（技能）｜红色贯穿光束打穿整条走廊，后排敌人 70% 伤害——一穿多");
	case EUpgradeId::RapidFire:    return TEXT("狂暴速射（火力）｜开火间隔 ×0.6（可叠加），弹道变青色——射得肉眼可见地密");
	case EUpgradeId::HeavyRounds:  return TEXT("重击弹头（火力）｜伤害 ×1.5（可叠加），弹道加粗+命中爆闪——威力看得见");
	case EUpgradeId::Radiance:     return TEXT("辉耀领域（领域·可叠加）｜队友周围4m内每0.5秒灼烧5伤；后续每张半径+1m、每拍伤害+10");
	case EUpgradeId::HealNova:     return TEXT("回响脉冲（羁绊·可叠加）｜每累计恢复 40 点生命，脚下展开蔓延 2 秒的绿色领域：7m 内敌人持续受创，蔓延终点爆发 25 伤冲击；再抽 阈值-5/终点伤+10——回复也是武器");
	case EUpgradeId::BuddyHealing: return TEXT("贴贴疗愈（羁绊·可叠加）｜与队友 2.5m 内双方缓慢回血；再抽 +1 HP/拍——贴住他，也贴住危险");
	case EUpgradeId::FrostShotPlayer: return TEXT("寒霜弹·本体版（元素）｜玩家弹挂冰霜轻减速；命中【灼烧中的敌人】=融化，本次伤害 ×2——辉耀的火，等你来点");
	case EUpgradeId::ShockShot:       return TEXT("感电弹（元素）｜每个主弹/穿透命中赠1次4m分叉，70%弹伤并挂电3s；队友子弹直击配对两带电目标，每端传导至少20伤或本弹30%伤害（取高），消费两标记");
	case EUpgradeId::HuntMark:        return TEXT("猎杀印记（元素）｜玩家弹叠金色 ◆（至多 3 层）；队友弹命中=处决，每层 15 额外伤害——你攒印记，他来引爆；再抽上限+1");
	case EUpgradeId::ShockOverload:   return TEXT("感电过载（深化）｜每端基础传导20→40，比例30%→55%并取高；保留逐个主弹/穿透命中的免费分叉，队友基础子弹可触发");
	case EUpgradeId::MarkMaster:      return TEXT("猎杀大师（深化）｜印记上限设为 5 层、处决设为 22/层——攒爆更狠");
	case EUpgradeId::FrostDeep:       return TEXT("深度冻结（深化）｜冰霜减速 1.5s/0.7→4s/0.4——冻得更久更狠");
	case EUpgradeId::PlayerScatter:   return TEXT("玩家散射（弹道）｜你的射击三向分裂 ±15°——与 AI 散射完全独立");
	case EUpgradeId::PlayerPierce:    return TEXT("玩家穿透（弹道）｜你的子弹贯穿敌群，全额伤害；可与玩家折射同持，每个贯穿目标各折射一次，分支不递归");
	case EUpgradeId::PlayerPrism:     return TEXT("玩家折射（弹道）｜每个主弹/贯穿命中向8m内另一敌人单跳70%伤害；与穿透协同，整发目标去重；感电赠叉共用单跳");
	case EUpgradeId::GoldPouch:       return TEXT("金币袋（厚礼）｜直接获得 15 金币——大奖池的保底厚礼，落袋为安");
	case EUpgradeId::GlassCannon:  return TEXT("玻璃大炮（契约）｜移速设为 7m/s / 最大生命 -40%：走位流的极限取舍");
	case EUpgradeId::BloodSacrifice: return TEXT("血祭强化（契约）｜队友伤害 +50% / 最大生命 -20：以血换火力");
	case EUpgradeId::Vigor:        return TEXT("强身（成长）｜生命上限 +30 并回满：稳稳的安全垫");
	case EUpgradeId::Barrage:      return TEXT("★ 融合·弹幕风暴（散射+穿透）｜三向弹道进化五向，全部贯穿——真正的弹幕");
	case EUpgradeId::BlinkBomb:    return TEXT("★ 融合·闪现轰击（闪烁+散射）｜落点八方爆射；活命中继承当前冰霜/感电/猎印，并独立判定概率地刺；同目标每次爆射只挂载一次，不额外触发主攻震爆或火爆");
	case EUpgradeId::RadianceOverload: return TEXT("★ 融合·辉耀过载（辉耀+速射）｜光环半径至少6m，已有每拍伤害翻倍，保留叠加成长");
	case EUpgradeId::RadianceBond:     return TEXT("★ 融合·辉耀结界（辉耀+贴贴）｜辉耀圈内即贴贴互回，不必再贴身 2.5m；且回响脉冲计数总是生效（满血治疗也累计）——光之领域也是庇护所");
	case EUpgradeId::PrismRounds:      return TEXT("折射弹体（技能）｜命中后向最近敌人弹射（2 跳，每跳 70%），品红折射线——光棱坦克（与穿透互斥）");
	case EUpgradeId::SwiftMove:        return TEXT("疾行（成长）｜移速 +15%（可叠加）——走位流的复利");
	case EUpgradeId::ExplosiveRounds:  return TEXT("高爆弹头（旧版兼容）｜每次范围 +0.8m、溅射 +12 个百分点（首次 3.3m/47%），橙色大爆闪；现役Boss奖励不自动授予");
	case EUpgradeId::FrostRounds:      return TEXT("寒霜弹（火力）｜命中使敌人减速 40% 持续 2s 并染蓝——被点名的猎犬跑不动了");
	}
	return TEXT("？？？");
}

ACompanionAIController* AWaveDirector::ResolveCompanionCtl()
{
	// 队友控制器缓存解析（ApplyUpgrade/开箱/锁箱台词三处共用——DRY）
	ACompanionAIController* Ctl = CompanionController.Get();
	if (!Ctl)
	{
		Ctl = Cast<ACompanionAIController>(UGameplayStatics::GetActorOfClass(this, ACompanionAIController::StaticClass()));
		CompanionController = Ctl;
	}
	return Ctl;
}

// ---------- 战争迷雾（09-28 小地图） ----------

void AWaveDirector::MarkExplored(const FVector& WorldPos)
{
	// 场地 ±FogWorldHalfExtent（v15 504m 大陆 ±252m）归一到 12×12 格；越界静默（围墙外不存在探索语义）
	if (!bFogInitialized)
	{
		ExploredGrid.Init(0.f, FogGridN * FogGridN);
		bFogInitialized = true;
	}
	const float Step = 2.f * FogWorldHalfExtent / (FogGridN-1);
	constexpr float Inner = 2600.f, Outer = 4200.f;
	for (int32 Y = 0; Y < FogGridN; ++Y)
	{
		for (int32 X = 0; X < FogGridN; ++X)
		{
			const float D = FVector2D::Distance(FVector2D(WorldPos.X, WorldPos.Y),
				FVector2D(-FogWorldHalfExtent+X*Step, -FogWorldHalfExtent+Y*Step));
			const float T = FMath::Clamp((Outer-D)/(Outer-Inner), 0.f, 1.f);
			ExploredGrid[Y*FogGridN+X] = FMath::Max(ExploredGrid[Y*FogGridN+X], T*T*(3.f-2.f*T));
		}
	}
}

void AWaveDirector::ApplyUpgrade(EUpgradeId Id)
{
	if (IsRunTerminated()) { return; }
	// 协议卡/血祭的落点：队友评分器与火力层（缓存一次，卡后常驻）
	ACompanionAIController* Ctl = ResolveCompanionCtl();

	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
	UHealthComponent* PlayerHealth = PlayerPawn ? PlayerPawn->FindComponentByClass<UHealthComponent>() : nullptr;
	AAICompanionDemoCharacter* PlayerChar = Cast<AAICompanionDemoCharacter>(PlayerPawn);

	switch (Id)
	{
	case EUpgradeId::Blink:
		if (PlayerChar)
		{
			PlayerChar->bBlinkUnlocked = true;
		}
		UnlockedIds.Add(TEXT("Blink"));
		break;
	case EUpgradeId::Scatter:
		if (Ctl)
		{
			Ctl->bScatterUnlocked = true;
		}
		UnlockedIds.Add(TEXT("Scatter"));
		break;
	case EUpgradeId::PierceRounds:
		if (Ctl)
		{
			Ctl->bPierceUnlocked = true;
		}
		UnlockedIds.Add(TEXT("PierceRounds"));
		break;
	case EUpgradeId::RapidFire:
		if (Ctl)
		{
			Ctl->bRapidFireUnlocked = true;
			Ctl->AttackInterval = FMath::Max(Ctl->AttackInterval * 0.6f, 0.25f);
		}
		UnlockedIds.Add(TEXT("RapidFire"));
		break;
	case EUpgradeId::HeavyRounds:
		if (Ctl)
		{
			Ctl->bHeavyRoundsUnlocked = true;
			Ctl->AttackDamage *= 1.5f;
		}
		break;
	case EUpgradeId::Radiance:
		// 10-08 辉耀成长：首抽4m内每0.5s伤害5，再抽范围+1m、每拍伤害+10
		if (bRadianceUnlocked)
		{
			RadianceRadius += 100.f;
			RadianceDamagePerTick += 10;
		}
		else
		{
			bRadianceUnlocked = true;
			UnlockedIds.Add(TEXT("Radiance"));
		}
		break;
	case EUpgradeId::Barrage:
		if (Ctl)
		{
			Ctl->bBarrageUnlocked = true;
		}
		UnlockedIds.Add(TEXT("Barrage"));
		break;
	case EUpgradeId::BlinkBomb:
		if (PlayerChar)
		{
			PlayerChar->bBlinkBombUnlocked = true;
		}
		UnlockedIds.Add(TEXT("BlinkBomb"));
		break;
	case EUpgradeId::HealNova:
		// 09-29 叠加化（十三节 A 表"治疗流进阶"）：首抽解锁，再抽 阈值-5（下限 15）/爆发伤害+10
		if (PlayerChar && PlayerChar->bHealNovaUnlocked)
		{
			PlayerChar->HealNovaThreshold = FMath::Max(PlayerChar->HealNovaThreshold - 5.f, 15.f);
			PlayerChar->HealNovaDamage += 10.f;
		}
		else if (PlayerChar)
		{
			PlayerChar->bHealNovaUnlocked = true;
			UnlockedIds.Add(TEXT("HealNova"));
		}
		break;
	case EUpgradeId::ShockOverload:
		// 基础传导20→40保障后期存在感，较高队友火力仍沿比例.3→.55成长。
		if (Ctl)
		{
			Ctl->ConductionScale = 0.55f;
			Ctl->ConductionBaseDamage = FMath::Max(Ctl->ConductionBaseDamage, 40.f);
		}
		UnlockedIds.Add(TEXT("ShockOverload"));
		break;
	case EUpgradeId::MarkMaster:
		// 元素深化·猎杀大师（09-30）：印记上限 3→5、处决 15→22/层
		if (PlayerChar)
		{
			PlayerChar->HuntMarkMaxStacks = 5;
		}
		if (Ctl)
		{
			Ctl->ExecuteDamagePerMark = 22.f;
		}
		UnlockedIds.Add(TEXT("MarkMaster"));
		break;
	case EUpgradeId::FrostDeep:
		// 元素深化·深度冻结（09-30）：冰霜减速 1.5s/0.7→4s/0.4
		if (PlayerChar)
		{
			PlayerChar->FrostSlowSeconds = 4.f;
			PlayerChar->FrostSlowFactor = 0.4f;
		}
		UnlockedIds.Add(TEXT("FrostDeep"));
		break;
	case EUpgradeId::GoldPouch:
		// 金币袋（09-30 锁箱干涸保底）：直接入账——厚礼不是凑数卡
		AddGold(15);
		break;
	case EUpgradeId::PlayerScatter:
	case EUpgradeId::PlayerPierce:
	case EUpgradeId::PlayerPrism:
		// 玩家穿透/折射可共同强化；散射独立定型。即便调试跨线领卡也先清互斥态。
		if (PlayerChar)
		{
			if (Id == EUpgradeId::PlayerScatter)
			{
				PlayerChar->bPlayerScatter = true;
				PlayerChar->bPlayerPierce = PlayerChar->bPlayerPrism = false;
			}
			else
			{
				PlayerChar->bPlayerScatter = false;
				if (Id == EUpgradeId::PlayerPierce) PlayerChar->bPlayerPierce = true;
				else PlayerChar->bPlayerPrism = true;
			}
		}
		UnlockedIds.Add(Id == EUpgradeId::PlayerScatter ? TEXT("PlayerScatter")
			: Id == EUpgradeId::PlayerPierce ? TEXT("PlayerPierce") : TEXT("PlayerPrism"));
		break;
	case EUpgradeId::FrostShotPlayer:
		// 元素·寒霜弹·本体版（09-29 十二节 A.1）：玩家弹挂冰霜轻减速；命中灼烧残留体=融化 ×2
		if (PlayerChar)
		{
			PlayerChar->bFrostShotUnlocked = true;
		}
		UnlockedIds.Add(TEXT("FrostShotPlayer"));
		break;
	case EUpgradeId::ShockShot:
		// 元素·感电弹（09-29 十二节 A.2）：玩家弹挂感电 3s——AI 弹命中残留体=电弧传导
		if (PlayerChar)
		{
			PlayerChar->bShockShotUnlocked = true;
		}
		UnlockedIds.Add(TEXT("ShockShot"));
		break;
	case EUpgradeId::HuntMark:
		// 元素·猎杀印记（09-29 十二节 A.3）：玩家弹叠 ◆（首抽解锁上限 3；再抽=印记强化，上限 +1）
		if (PlayerChar && PlayerChar->bHuntMarkUnlocked)
		{
			PlayerChar->HuntMarkMaxStacks += 1;
		}
		else if (PlayerChar)
		{
			PlayerChar->bHuntMarkUnlocked = true;
			UnlockedIds.Add(TEXT("HuntMark"));
		}
		break;
	case EUpgradeId::RadianceOverload:
		RadianceRadius = FMath::Max(RadianceRadius, 600.f);
		RadianceDamagePerTick *= 2;
		UnlockedIds.Add(TEXT("RadianceOverload"));
		break;
	case EUpgradeId::PrismRounds:
		if (Ctl)
		{
			Ctl->bPrismUnlocked = true;
		}
		UnlockedIds.Add(TEXT("PrismRounds"));
		break;
	case EUpgradeId::ExplosiveRounds:
		if (Ctl)
		{
			Ctl->bExplosiveUnlocked = true;
			Ctl->ExplosionRadius += 80.f;           // 09-25 收敛：+100→+80（半径滚雪球是"太厉害"主因之一）
			Ctl->ExplosionDamageScale += 0.12f;     // 09-25 削弱：+0.2→+0.12
		}
		break;
	case EUpgradeId::FrostRounds:
		if (Ctl)
		{
			Ctl->bFrostUnlocked = true;
		}
		break;
	case EUpgradeId::SwiftMove:
		// 移速复利（可叠加）：走位是玩家唯一的主动输出，速度就是伤害
		if (PlayerChar && PlayerChar->GetCharacterMovement())
		{
			PlayerChar->GetCharacterMovement()->MaxWalkSpeed *= 1.15f;
		}
		break;
	case EUpgradeId::BuddyHealing:
		// 09-29 叠加化（十三节 A 表"治疗量可叠"）：首抽解锁，再抽 +1 HP/拍
		if (bBondHealUnlocked)
		{
			BondHealPerTick += 1;
		}
		else
		{
			bBondHealUnlocked = true;
			UnlockedIds.Add(TEXT("BuddyHealing"));
		}
		break;
	case EUpgradeId::GlassCannon:
		if (PlayerHealth)
		{
			PlayerHealth->SetMaxHealth(PlayerHealth->GetMaxHealth() * 0.6f);
		}
		if (PlayerChar && PlayerChar->GetCharacterMovement())
		{
			PlayerChar->GetCharacterMovement()->MaxWalkSpeed = 500.f * 1.4f;
		}
		UnlockedIds.Add(TEXT("GlassCannon"));
		break;
	case EUpgradeId::BloodSacrifice:
		if (Ctl)
		{
			Ctl->AttackDamage *= 1.5f;
		}
		if (PlayerHealth)
		{
			PlayerHealth->SetMaxHealth(FMath::Max(PlayerHealth->GetMaxHealth() - 20.f, 20.f));
		}
		UnlockedIds.Add(TEXT("BloodSacrifice"));
		break;
	case EUpgradeId::Vigor:
		if (PlayerHealth)
		{
			PlayerHealth->SetMaxHealth(PlayerHealth->GetMaxHealth() + 30.f);
			PlayerHealth->Heal(9999.f);
		}
		break;
	case EUpgradeId::RadianceBond:
		bRadianceBondUnlocked = true;
		UnlockedIds.Add(TEXT("RadianceBond"));
		// 10-07 用户裁决：辉耀结界协同位——回响脉冲"满血治疗也计数"仅在此卡持有后生效（非常态）
		if (PlayerChar)
		{
			PlayerChar->bHealNovaCountWhenFull = true;
		}
		break;
	}

	UE_LOG(LogAICompanionDemo, Warning, TEXT("[Wave] 选卡：%s"), *DescribeCard(Id));
	// 已获强化记账（09-21 G1）：叠加卡计数——HUD 强化列表的数据源（行为真源仍是 UnlockedIds/各旗标）
	++CardTakenCounts.FindOrAdd(Id);

	// 卡面台词（09-27 用户定调"抽到新技能冒一句"）：队友评论式播报——气泡+空间化语音（远处的队友喊话会变远）
	if (Ctl)
	{
		EGameSFX CardVoice = EGameSFX::None;
		const FString CardLine = CardBarkOf(Id, CardVoice);
		if (!CardLine.IsEmpty())
		{
			Ctl->Bark(CardLine, TEXT("Card"), 0.f, CardVoice);
		}
	}
	// 领取由卡面灰显/已获图标与队友台词反馈，详细技能说明仅留在卡面和日志。

	// 锁箱六选二（09-30）：双选状态——选一张扣一次，还剩就**重建式刷新面板**（已选卡置灰打勾——
	// 09-30 用户实测"选了没反馈是低级失误"根治），选满两张才关面板解冻
	if (bChestDoublePick)
	{
		--ChestPicksRemaining;
		if (ChestPicksRemaining > 0)
		{
			// 重建面板（拆+建，状态保留——HideUpgradeOverlay 会复位状态故不走它）
			if (UpgradeOverlay.IsValid() && GEngine && GEngine->GameViewport)
			{
				GEngine->GameViewport->RemoveViewportWidgetContent(UpgradeOverlay.ToSharedRef());
			}
			UpgradeOverlay.Reset();
			ShowUpgradeOverlay(true);
			return;
		}
		bChestDoublePick = false;
	}

	// 拆卡面浮层 + 恢复鼠标隐藏 + 复位双选态，世界恢复流速
	CompleteChoice();
}

void AWaveDirector::DebugPreviewUpgradeCards(int32 FirstCard, const bool bChest, const bool bGoldOnly)
{
#if WITH_EDITOR
	if (IsRunTerminated()) { return; }
	HideUpgradeOverlay();
	bRelicPending = false;
	bUpgradePending = true;
	bChestDoublePick = bChest;
	ChestPicksRemaining = bChest ? 2 : 0;
	ChestTakenSlots.Reset();
	constexpr int32 Count = static_cast<int32>(EUpgradeId::GoldPouch) + 1;
	FirstCard = FMath::Clamp(FirstCard, 0, Count - 1);
	for (int32 Slot = 0; Slot < (bChest ? 6 : 3); ++Slot)
	{
		const EUpgradeId Id = bGoldOnly ? EUpgradeId::GoldPouch : static_cast<EUpgradeId>((FirstCard + Slot) % Count);
		if (bChest) { ChestOffers[Slot] = Id; } else { Offers[Slot] = Id; }
	}
	UGameplayStatics::SetGlobalTimeDilation(this, 0.f);
	ShowUpgradeOverlay(bChest);
#endif
}

void AWaveDirector::DebugChooseUpgradeCard(const int32 Slot)
{
#if WITH_EDITOR
	SelectUpgradeOffer(Slot);
#endif
}

FString AWaveDirector::DebugUpgradeCardState() const
{
#if WITH_EDITOR
	FString Cards;
	for (const FHudUpgradeCard& Card : GetHudSnapshot().UpgradeCards)
	{
		if (!Cards.IsEmpty()) { Cards += TEXT(","); }
		Cards += FString::Printf(TEXT("{\"id\":\"%s\",\"count\":%d,\"rarity\":%d}"),
			*Card.UpgradeId.ToString(), Card.Count, static_cast<int32>(Card.Rarity));
	}
	FString Candidates;
	for (int32 Slot = 0; bUpgradePending && Slot < (bChestDoublePick ? 6 : 3); ++Slot)
	{
		if (bRelicPending)
		{
			if (!Candidates.IsEmpty()) { Candidates += TEXT(","); }
			Candidates += FString::Printf(TEXT("{\"id\":\"%s\",\"rarity\":3,\"fusion\":false}"),
				*RelicIdName(RelicOffers[Slot]).ToString());
			continue;
		}
		const EUpgradeId Id = bChestDoublePick ? ChestOffers[Slot] : Offers[Slot];
		if (!Candidates.IsEmpty()) { Candidates += TEXT(","); }
		Candidates += FString::Printf(TEXT("{\"id\":\"%s\",\"rarity\":%d,\"fusion\":%s}"),
			*UpgradeCardKey(Id).ToString(), static_cast<int32>(PresentationRarity(CardRarityOf(Id))),
			IsFusionLegendary(Id) ? TEXT("true") : TEXT("false"));
	}
	return FString::Printf(TEXT("{\"pending\":%s,\"chest\":%s,\"remaining\":%d,\"takenSlots\":%d,\"gold\":%d,\"upgrades\":[%s],\"offers\":[%s]}"),
		bUpgradePending ? TEXT("true") : TEXT("false"), bChestDoublePick ? TEXT("true") : TEXT("false"),
		ChestPicksRemaining, ChestTakenSlots.Num(), GoldCount, *Cards, *Candidates);
#else
	return TEXT("{}");
#endif
}

void AWaveDirector::DebugSpawnBoss(int32 BossIndex)
{
	const APawn* P = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!P)
	{
		return;
	}
	const int32 Idx = FMath::Clamp(BossIndex, 0, 2);
	const TSubclassOf<ABossBase> Cls = BossClasses.IsValidIndex(Idx) ? BossClasses[Idx] : TSubclassOf<ABossBase>(ABossBase::StaticClass());
	const FVector Dir = P->GetActorForwardVector().GetSafeNormal2D();
	const FVector Loc = P->GetActorLocation() + Dir * 800.f + FVector(0.f, 0.f, 180.f);

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	if (ABossBase* Boss = GetWorld()->SpawnActor<ABossBase>(Cls, Loc, FRotator::ZeroRotator, Params))
	{
		const FString Name = BossNames.IsValidIndex(Idx) ? BossNames[Idx] : FString::Printf(TEXT("BOSS %d"), Idx + 1);
		Boss->SetBossIdentity(Name, Idx + 1);
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(97, 4.f, FColor::Red, FString::Printf(TEXT("[Debug] %s 已召唤"), *Name));
		}
		UE_LOG(LogAICompanionDemo, Warning, TEXT("[Debug] Boss 召唤：%s"), *Name);
	}
}

void AWaveDirector::DebugSpawnEnemies(int32 Count)
{
	for (int32 i = 0; i < Count; ++i)
	{
		if (SpawnEnemyAtRing(i % 2 == 0 ? EGruntVariant::Standard : EGruntVariant::Rusher)) ++AliveCount;
	}
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(96, 3.f, FColor::Orange, FString::Printf(TEXT("[Debug] 刷怪 %d 只"), Count));
	}
}

void AWaveDirector::DebugClearEnemies()
{
	int32 Cleared = 0;
	for (TActorIterator<AEnemyGrunt> It(GetWorld()); It; ++It)
	{
		if (AEnemyGrunt* Grunt = *It)
		{
			Grunt->Destroy();
			++Cleared;
		}
	}
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(96, 3.f, FColor::Orange, FString::Printf(TEXT("[Debug] 清场 %d 只"), Cleared));
	}
}

void AWaveDirector::DebugApplyUpgrade(EUpgradeId Id)
{
	ApplyUpgrade(Id);
}

void AWaveDirector::DebugGrantRandomUpgrades(int32 Count)
{
	// 测试模式开局随机武装（用户点名"测试还要自己加技能太麻烦"）：每次 RollOffers 权重抽三
	// 选一应用——互斥离池/一次性离池/干涸保底全复用正式抽卡管线，零旁路零特判
	int32 Granted = 0;
	for (int32 i = 0; i < Count; ++i)
	{
		RollOffers(/*bChestPool*/ false);
		ApplyUpgrade(Offers[FMath::RandRange(0, 2)]);
		++Granted;
	}
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(96, 4.f, FColor::FromHex(TEXT("FFD700")),
			FString::Printf(TEXT("[Test] 已随机发放 %d 个升级"), Granted));
	}
	UE_LOG(LogAICompanionDemo, Warning, TEXT("[Test] 随机发放 %d 个升级（测试模式开局武装）"), Granted);
}

void AWaveDirector::SetLevel(int32 TargetLevel)
{
	// 直接设置等级（测试模式开局满级——绕过 XP 曲线，跳过逐级三选一）
	while (Level < TargetLevel)
	{
		CurrentXp = 0;
		++Level;
		XpToNextLevel = FMath::RoundToInt(XpToNextLevel * 1.4f);
	}
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(96, 4.f, FColor::FromHex(TEXT("FFD700")), FString::Printf(TEXT("[Test] 等级设为 Lv.%d"), Level));
	}
	UE_LOG(LogAICompanionDemo, Warning, TEXT("[Test] 等级设为 Lv.%d"), Level);
}

void AWaveDirector::DebugApplyUpgradeByIdx(int32 UpgradeIndex)
{
	if (UpgradeIndex >= 0 && UpgradeIndex <= static_cast<int32>(EUpgradeId::GoldPouch))
	{
		ApplyUpgrade(static_cast<EUpgradeId>(UpgradeIndex));
	}
}

void AWaveDirector::NotifyBossKilled(int32 BossIndex, const FString& BossName, const FVector& BossLocation)
{
	if (IsRunTerminated()) { return; }
	++BossKilledCount;
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(kBannerKey, 8.f, FColor::FromHex(TEXT("FFD700")),
			FString::Printf(TEXT("☠ %s 已被击倒！（%d/3）"), *BossName, BossKilledCount));
	}
	UE_LOG(LogAICompanionDemo, Warning, TEXT("[Boss] %s 击倒确认（累计 %d/3）"), *BossName, BossKilledCount);

	// Death callbacks finish before next-frame reward display; concurrent rewards serialize.
	if (BossIndex>=1 && BossIndex<=3) { PendingChoices.Add(BossIndex+1); }
	if (BossIndex == 3)
	{
		if (AAICompanionDemoGameMode* GM = GetWorld()->GetAuthGameMode<AAICompanionDemoGameMode>())
		{
			// 门先生成但奖励队列先领取，GM和门都复验HasPendingRewardChoices。
			GM->NotifyWurmDefeated(BossLocation);
		}
	}
}

void AWaveDirector::TickBondHeal()
{
	// 贴贴疗愈（C1 羁绊卡）：0.5s 节拍内检测玩家↔队友距离，圈内双方缓回——
	// 生存机制做成"贴住队友"的走位决策（队友在哪敌在哪，贴贴即冒险），而非干加血
	if (!bBondHealUnlocked || bUpgradePending)
	{
		return;
	}

	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
	ACompanionCharacter* Companion = CompanionPawn.Get();
	if (!Companion)
	{
		Companion = Cast<ACompanionCharacter>(UGameplayStatics::GetActorOfClass(this, ACompanionCharacter::StaticClass()));
		CompanionPawn = Companion;
	}
	if (!PlayerPawn || !Companion)
	{
		return;
	}

	// 融合·辉耀结界（09-25）：贴贴距离放宽到辉耀圈半径——圈中心是队友，玩家在圈内=双方都在光里，
	// "圈内即贴贴"；未融合时维持 2.5m 贴身张力
	const float HealRadius = bRadianceBondUnlocked ? RadianceRadius : BondHealRadius;
	if (FVector(PlayerPawn->GetActorLocation() - Companion->GetActorLocation()).Size2D() > HealRadius)
	{
		return;
	}

	UHealthComponent* PlayerHealth = PlayerPawn->FindComponentByClass<UHealthComponent>();
	UHealthComponent* CompanionHealth = Companion->FindComponentByClass<UHealthComponent>();
	if (PlayerHealth && !PlayerHealth->IsDead() && PlayerHealth->GetCurrentHealth() < PlayerHealth->GetMaxHealth())
	{
		PlayerHealth->Heal(static_cast<float>(BondHealPerTick));
	}
	if (CompanionHealth && !CompanionHealth->IsDead() && CompanionHealth->GetCurrentHealth() < CompanionHealth->GetMaxHealth())
	{
		CompanionHealth->Heal(static_cast<float>(BondHealPerTick));
	}
}

void AWaveDirector::TickRadiance()
{
	// 辉耀领域（C1 领域卡，用户 09-12 点名"类似辉耀的效果"）：AI 的领域感——
	// 光圈可视化常驻（LifeTime 略长于 0.5s 节拍→视觉连续）+ 圈内敌人持续灼烧（伤害 Instigator=队友，击杀链自动入账）
	if (!bRadianceUnlocked || bUpgradePending)
	{
		return;
	}

	ACompanionCharacter* Companion = CompanionPawn.Get();
	if (!Companion)
	{
		Companion = Cast<ACompanionCharacter>(UGameplayStatics::GetActorOfClass(this, ACompanionCharacter::StaticClass()));
		CompanionPawn = Companion;
	}
	if (!Companion)
	{
		return;
	}

	const FVector Center = Companion->GetActorLocation();
	DrawDebugCircle(GetWorld(), Center, RadianceRadius, 48,
		FColor::Cyan, /*bPersistentLines*/ false, /*LifeTime*/ 0.6f, /*DepthPriority*/ 0, /*Thickness*/ 3.f,
		FVector(1.f, 0.f, 0.f), FVector(0.f, 1.f, 0.f), /*bDrawAxis*/ false);

	TArray<AActor*> Enemies;
	UGameplayStatics::GetAllActorsOfClass(this, AEnemyGrunt::StaticClass(), Enemies);
	for (AActor* Enemy : Enemies)
	{
		if (!Enemy || FVector(Enemy->GetActorLocation() - Center).Size2D() > RadianceRadius)
		{
			continue;
		}
		if (const ICombatant* Victim = Cast<ICombatant>(Enemy))
		{
			if (UHealthComponent* Health = Victim->GetHealthComponent())
			{
				if (!Health->IsDead())
				{
					Health->ApplyDamage(static_cast<float>(RadianceDamagePerTick), /*Instigator*/ Companion);
					// 灼烧残留（09-29 元素反应）：每拍刷新 1.5s 判定面——玩家冰弹命中残留体=融化（×2）
					if (AEnemyGrunt* BurningGrunt = Cast<AEnemyGrunt>(Enemy))
					{
						BurningGrunt->RefreshBurnResidue();
					}
					// 灼烧命中特效（用户 2026-09-13："辉耀打到人身上看不出效果"）——
					// 青绿色小火花在敌人胸口，每跳一闪：伤害来源的可视辨识（辉耀=青，激光=黄，爆射=品红）
					DrawDebugSphere(GetWorld(), Enemy->GetActorLocation() + FVector(0.f, 0.f, 40.f),
						7.f, 6, FColor::Turquoise, false, 0.35f, 0, 1.f);
					// 灼烧飘字（09-24 用户点名"辉耀伤害没跳数字"）：青色小字——持续伤害可读，
					// 与直击黄/折射银/溅射橙同属"来源分色"语言
					DrawDebugString(GetWorld(), Enemy->GetActorLocation() + FVector(0.f, 0.f, 70.f),
						FString::Printf(TEXT("-%d"), RadianceDamagePerTick), nullptr, FColor::Turquoise, 0.45f, false, 0.8f);
				}
			}
		}
	}
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

// ---------- HUD 快照出口（09-21 G1） ----------

// ---------- 稀有度（09-25 W2） ----------

const TCHAR* AWaveDirector::RarityTag(ECardRarity Rarity)
{
	switch (Rarity)
	{
	case ECardRarity::Epic:  return TEXT("◆传说");
	case ECardRarity::Rare:  return TEXT("◇稀有");
	default:                 return TEXT("");
	}
}

AWaveDirector::ECardRarity AWaveDirector::CardRarityOf(EUpgradeId Id) const
{
	switch (Id)
	{
	case EUpgradeId::Blink:
	case EUpgradeId::PierceRounds:
	case EUpgradeId::PrismRounds:
	case EUpgradeId::Radiance:
	case EUpgradeId::ExplosiveRounds:
	case EUpgradeId::BloodSacrifice:
	case EUpgradeId::Barrage:
	case EUpgradeId::BlinkBomb:
	case EUpgradeId::RadianceOverload:
	case EUpgradeId::RadianceBond:
	case EUpgradeId::ShockOverload:
	case EUpgradeId::MarkMaster:
	case EUpgradeId::FrostDeep:
		return ECardRarity::Epic;
	case EUpgradeId::Scatter:
	case EUpgradeId::HeavyRounds:
	case EUpgradeId::FrostRounds:
	case EUpgradeId::GlassCannon:
	case EUpgradeId::HealNova:
	case EUpgradeId::FrostShotPlayer:
	case EUpgradeId::ShockShot:
	case EUpgradeId::HuntMark:
	case EUpgradeId::PlayerScatter:
	case EUpgradeId::PlayerPierce:
	case EUpgradeId::PlayerPrism:
	case EUpgradeId::GoldPouch:
		return ECardRarity::Rare;
	default:
		return ECardRarity::Common;   // RapidFire/Vigor/SwiftMove/BuddyHealing——数值成长与安全垫
	}
}

FString AWaveDirector::CardShortName(EUpgradeId Id) const
{
	const FString Full = DescribeCard(Id);
	const int32 Sep = Full.Find(TEXT("｜"));
	return Sep > 0 ? Full.Left(Sep) : Full;
}

EUpgradeCardRarity AWaveDirector::PresentationRarity(const ECardRarity Rarity)
{
	return Rarity == ECardRarity::Epic ? EUpgradeCardRarity::Legendary
		: (Rarity == ECardRarity::Rare ? EUpgradeCardRarity::Rare : EUpgradeCardRarity::Common);
}

bool AWaveDirector::IsFusionLegendary(const EUpgradeId Id)
{
	return Id == EUpgradeId::Barrage || Id == EUpgradeId::BlinkBomb
		|| Id == EUpgradeId::RadianceOverload || Id == EUpgradeId::RadianceBond;
}

FString AWaveDirector::CardBarkOf(EUpgradeId Id, EGameSFX& OutVoice) const
{
	// 卡面台词表（09-27 配音轮，冷峻人偶人格——机械感与游戏世界观对位；语音槽与文案同源）
	OutVoice = EGameSFX::None;
	switch (Id)
	{
	case EUpgradeId::Blink:            OutVoice = EGameSFX::CardVoice_Blink;           return TEXT("空间折叠，出发。");
	case EUpgradeId::Scatter:          OutVoice = EGameSFX::CardVoice_Scatter;         return TEXT("现在可以攻击到更多敌人了。");
	case EUpgradeId::PierceRounds:     OutVoice = EGameSFX::CardVoice_Pierce;          return TEXT("这一枪，谁也挡不住。");
	case EUpgradeId::PrismRounds:      OutVoice = EGameSFX::CardVoice_Prism;           return TEXT("光线会替我找到你。");
	case EUpgradeId::RapidFire:        OutVoice = EGameSFX::CardVoice_RapidFire;       return TEXT("射速提升，火力压制。");
	case EUpgradeId::HeavyRounds:      OutVoice = EGameSFX::CardVoice_Heavy;           return TEXT("每一发都更重了。");
	case EUpgradeId::ExplosiveRounds:  OutVoice = EGameSFX::CardVoice_Explosive;       return TEXT("让我们来制造一些爆炸。");
	case EUpgradeId::FrostRounds:      OutVoice = EGameSFX::CardVoice_Frost;           return TEXT("把它们冻在原地。");
	case EUpgradeId::Radiance:         OutVoice = EGameSFX::CardVoice_Radiance;        return TEXT("光之领域，随我展开。");
	case EUpgradeId::BuddyHealing:     OutVoice = EGameSFX::CardVoice_BuddyHeal;       return TEXT("靠近我，就能活下去。");
	case EUpgradeId::HealNova:         OutVoice = EGameSFX::CardVoice_HealNova;        return TEXT("治愈之力，亦可杀敌。");
	// 元素三卡（09-29）：语音复用近似槽（零新槽零新 TTS），语义大方向对——专属台词后续按需补槽
	case EUpgradeId::FrostShotPlayer:  OutVoice = EGameSFX::CardVoice_Frost;           return TEXT("寒霜上膛。");
	case EUpgradeId::ShockShot:        OutVoice = EGameSFX::CardVoice_Pierce;          return TEXT("电流会替我找到你。");
	case EUpgradeId::HuntMark:         OutVoice = EGameSFX::CardVoice_Heavy;           return TEXT("标记已上。");
	case EUpgradeId::GlassCannon:      OutVoice = EGameSFX::CardVoice_GlassCannon;     return TEXT("防御？我们不需要防御。");
	case EUpgradeId::BloodSacrifice:   OutVoice = EGameSFX::CardVoice_BloodSacrifice;  return TEXT("力量，总有代价。");
	case EUpgradeId::Vigor:            OutVoice = EGameSFX::CardVoice_Vigor;           return TEXT("结构强化完毕。");
	case EUpgradeId::SwiftMove:        OutVoice = EGameSFX::CardVoice_SwiftMove;       return TEXT("移动模块，提速。");
	case EUpgradeId::Barrage:          OutVoice = EGameSFX::CardVoice_Barrage;         return TEXT("弹幕覆盖，一个不留。");
	case EUpgradeId::BlinkBomb:        OutVoice = EGameSFX::CardVoice_BlinkBomb;       return TEXT("落点，即是爆点。");
	case EUpgradeId::RadianceOverload: OutVoice = EGameSFX::CardVoice_RadianceOverload; return TEXT("领域扩张至极限。");
	case EUpgradeId::RadianceBond:     OutVoice = EGameSFX::CardVoice_RadianceBond;    return TEXT("光域之内，皆受庇护。");
	default:                           break;
	}
	return FString();
}

AWaveDirector::FEndlessHudSnapshot AWaveDirector::GetHudSnapshot() const
{
	FEndlessHudSnapshot Snap;
	Snap.SurvivalSeconds = ElapsedSeconds;
	Snap.HpMultiplier = bEndlessMode ? FMath::Pow(HpGrowthPerMinute, GetElapsedMinutes()) : 1.f;
	Snap.AliveCount = AliveCount;
	Snap.KillCount = KillCount;
	Snap.Level = Level;
	Snap.CurrentXp = CurrentXp;
	Snap.XpToNext = XpToNextLevel;
	Snap.GoldCount = GoldCount;
	Snap.BossKilledCount = BossKilledCount;
	if (const APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		if (const AAICompanionDemoCharacter* Char = Cast<AAICompanionDemoCharacter>(PlayerPawn))
		{
			Snap.bHasTreasureKey = Char->HasTreasureKey();
		}
	}
	for (const TPair<EUpgradeId, int32>& Entry : CardTakenCounts)
	{
		FHudUpgradeCard& Card = Snap.UpgradeCards.AddDefaulted_GetRef();
		Card.UpgradeId = UpgradeCardKey(Entry.Key);
		Card.Name = CardShortName(Entry.Key);
		Card.Count = Entry.Value;
		Card.Rarity = PresentationRarity(CardRarityOf(Entry.Key));
	}
	AppendRelicHudCards(Snap);
	Snap.UpgradeCards.Sort([](const FHudUpgradeCard& A, const FHudUpgradeCard& B)
	{
		return A.UpgradeId.LexicalLess(B.UpgradeId);
	});
	return Snap;
}

void AWaveDirector::AddGold(const int32 Amount){
	if (IsRunTerminated() || Amount <= 0)
	{
		return;
	}
	GoldCount += Amount;
}

// ---------- 金币消费口（09-25 信标等非升级消费） ----------

bool AWaveDirector::TrySpendGold(const int32 Cost)
{
	// 纯扣费：与 TrySpendGoldForUpgrade 的差别只在"不进三选一"——消费即效果自证（信标传送）
	if (IsRunTerminated() || GoldCount < Cost)
	{
		return false;
	}
	GoldCount -= Cost;
	return true;
}

void AWaveDirector::RefundGold(const int32 Amount)
{
	if (IsRunTerminated()) { return; }
	GoldCount += Amount;
}

void AWaveDirector::GrantFreeUpgrade()
{
	// 免费锁箱六选二：账本零进出，现有选择期间排队，交给EnterUpgradePause统一续接。
	if (IsRunTerminated())
	{
		return;
	}
	UE_LOG(LogAICompanionDemo, Warning, TEXT("[Wave] 锁箱奖励——免费六选二！"));
	if (ACompanionAIController* VoiceCtl = ResolveCompanionCtl())
	{
		VoiceCtl->Bark(TEXT("运气不错。"), TEXT("Chest"), 0.f, EGameSFX::Voice_LockedChest);
	}
	EnterUpgradePause(true);
}

bool AWaveDirector::TrySpendGoldForUpgrade(const int32 Cost)
{
	// 暂停中/钱不够都不开（宝箱侧负责提示差额，本口只管账本与升级流）
	if (IsRunTerminated() || bUpgradePending || GoldCount < Cost)
	{
		return false;
	}

	GoldCount -= Cost;
	++ChestOpenCount;   // 09-25 价格递增账本：下一口更贵（GetNextChestCost = 基础 + 步进×计数）
	UE_LOG(LogAICompanionDemo, Warning, TEXT("[Wave] 开箱消耗 %d 金（余 %d，下一箱 %d 金）——进入三选一"), Cost, GoldCount, GetNextChestCost());
	if (ACompanionAIController* VoiceCtl = ResolveCompanionCtl())
	{
		VoiceCtl->Bark(TEXT("补给开启。"), TEXT("Chest"), 0.f, EGameSFX::Voice_ChestOpen);
	}
	EnterUpgradePause(false);
	return true;
}

