/**
 * [INPUT]: 导演的地图/祭坛/首领奖励与领域治疗状态。
 * [OUTPUT]: 原运行效果及调度方法完整保留，原游戏时钟/伤害归属不变。
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

// ---------- 原运行效果及调度方法完整保留，原游戏时钟/伤害归属不变。 ----------

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

void AWaveDirector::ProcessScheduledEvents()
{
	if (IsRunTerminated() || !GetWorld()) { return; }
	if (BossAltarTimes.Num() > 0 && BossAltarSpawned.Num() != BossAltarTimes.Num())
	{
		BossAltarSpawned.Init(false, BossAltarTimes.Num());
	}
	for (int32 i = 0; i < BossAltarTimes.Num(); ++i)
	{
		if (!BossAltarSpawned[i] && ElapsedSeconds >= BossAltarTimes[i])
		{
			if (const APawn* P = UGameplayStatics::GetPlayerPawn(this, 0))
			{
				const float Angle = FMath::FRandRange(0.f, 2.f * PI);
				const float Dist = FMath::FRandRange(6000.f, 8000.f);
				const FVector Loc(P->GetActorLocation().X + FMath::Cos(Angle) * Dist,
					P->GetActorLocation().Y + FMath::Sin(Angle) * Dist, P->GetActorLocation().Z);
				if (ABossAltar* Altar = GetWorld()->SpawnActor<ABossAltar>(Loc, FRotator::ZeroRotator))
				{
					BossAltarSpawned[i] = true;
					const TSubclassOf<ABossBase> SlotClass = BossClasses.IsValidIndex(i) ? BossClasses[i] : BossClasses[0];
					const FString BossName = BossNames.IsValidIndex(i) ? BossNames[i] : FString::Printf(TEXT("BOSS %d"), i + 1);
					Altar->SetBossInfo(SlotClass, BossName, i + 1);
					URunEventLog::Publish(GetWorld(), TEXT("神秘的声音"), BossName + TEXT("已出现，寻找它的祭坛吧"),
						FName(*FString::Printf(TEXT("BossAltar_%d"), i + 1)));
					USFXLibrary::PlaySFX(GetWorld(), EGameSFX::BossWarn);
					if (GEngine)
					{
						GEngine->AddOnScreenDebugMessage(kBannerKey, 6.f, FColor::Red,
							FString::Printf(TEXT("☠ %s 的祭坛已出现——循红色光柱前往！"), *BossName));
					}
					UE_LOG(LogAICompanionDemo, Warning, TEXT("[Boss] %d 号祭坛冒出 @ %s"), i + 1, *Loc.ToCompactString());
				}
			}
		}
	}
	if (!bEnraged && ElapsedSeconds >= 720.f)
	{
		SetEnraged(true);
		URunEventLog::Publish(GetWorld(), TEXT("神秘的声音"), TEXT("你怎么还在这里？"), TEXT("WorldEnrage"));
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(kBannerKey, 8.f, FColor::Red, TEXT("☠ 狂 暴 开 始 —— 逃 是 逃 不 掉 的"));
		}
		UE_LOG(LogAICompanionDemo, Warning, TEXT("[Wave] 狂暴态开启——怪物曲线换挡不封顶"));
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
	if (BossIndex>=1 && BossIndex<=3) { PendingChoices.Add({ ERewardSource::BossRelic, 0, BossIndex }); }
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
