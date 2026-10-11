/**
 * [INPUT]: 既有Debug入口、真实选择/出生/应用方法与状态。
 * [OUTPUT]: 原调试方法正文保留，专属JSON转义helper仅在本TU定义。
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

namespace
{
	FString GrowthJsonString(FString Value)
	{
		Value.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
		Value.ReplaceInline(TEXT("\""), TEXT("\\\""));
		Value.ReplaceInline(TEXT("\r"), TEXT("\\r"));
		Value.ReplaceInline(TEXT("\n"), TEXT("\\n"));
		Value.ReplaceInline(TEXT("\t"), TEXT("\\t"));
		return TEXT("\"") + Value + TEXT("\"");
	}
}

// ---------- 原调试方法正文保留，专属JSON转义helper仅在本TU定义。 ----------

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
	ActiveReward = { ERewardSource::DebugPreview, Level, 0 };
	ActiveRewardPool = ERewardPool::Mixed;
	bRewardFlipped = false;
	OfferFallbackNote.Reset();
	const int32 Count = DebugUpgradeCardCount();
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
	return FString::Printf(TEXT("{\"pending\":%s,\"chest\":%s,\"remaining\":%d,\"takenSlots\":%d,\"gold\":%d,\"upgrades\":[%s],\"offers\":[%s],\"flipAvailable\":%s}"),
		bUpgradePending ? TEXT("true") : TEXT("false"), bChestDoublePick ? TEXT("true") : TEXT("false"),
		ChestPicksRemaining, ChestTakenSlots.Num(), GoldCount, *Cards, *Candidates, CanFlipCurrentReward() ? TEXT("true") : TEXT("false"));
#else
	return TEXT("{}");
#endif
}

bool AWaveDirector::DebugAdvanceScheduleTime(const float DeltaSeconds)
{
#if WITH_EDITOR
	if (IsRunTerminated() || !FMath::IsFinite(DeltaSeconds) || DeltaSeconds < 0.f) { return false; }
	// 人工推进调度时钟fixture；生成真实祭坛/切换真实狂暴，不伪造日志、不推进刷怪预算。
	ElapsedSeconds += DeltaSeconds;
	ProcessScheduledEvents();
	return true;
#else
	return false;
#endif
}

void AWaveDirector::DebugQueueLevelReward(const int32 RewardLevel)
{
#if WITH_EDITOR
	if (RewardLevel >= 2) { EnterUpgradePause(false, ERewardSource::LevelUp, RewardLevel); }
#endif
}

void AWaveDirector::DebugFlipCurrentReward()
{
#if WITH_EDITOR
	FlipCurrentReward();
#endif
}

FString AWaveDirector::DebugGrowthState() const
{
#if WITH_EDITOR
	auto SourceName = [](const ERewardSource Source) -> const TCHAR*
	{
		switch (Source)
		{
		case ERewardSource::LevelUp: return TEXT("LevelUp");
		case ERewardSource::NormalChest: return TEXT("NormalChest");
		case ERewardSource::StarterChest: return TEXT("StarterChest");
		case ERewardSource::LockedChest: return TEXT("LockedChest");
		case ERewardSource::BossRelic: return TEXT("BossRelic");
		default: return TEXT("DebugPreview");
		}
	};
	const TCHAR* PoolName = ActiveRewardPool == ERewardPool::Player ? TEXT("Player")
		: ActiveRewardPool == ERewardPool::AI ? TEXT("AI") : TEXT("Mixed");
	FString Queue;
	for (const FRewardChoice& Reward : PendingChoices)
	{
		if (!Queue.IsEmpty()) { Queue += TEXT(","); }
		Queue += FString::Printf(TEXT("{\"source\":\"%s\",\"level\":%d,\"boss\":%d}"), SourceName(Reward.Source), Reward.EarnedLevel, Reward.BossIndex);
	}
	FString Candidates;
	for (int32 Slot = 0; bUpgradePending && !bRelicPending && Slot < (bChestDoublePick ? 6 : 3); ++Slot)
	{
		const EUpgradeId Id = bChestDoublePick ? ChestOffers[Slot] : Offers[Slot];
		if (!Candidates.IsEmpty()) { Candidates += TEXT(","); }
		Candidates += FString::Printf(TEXT("{\"slot\":%d,\"id\":\"%s\",\"short\":%s,\"tooltip\":%s}"), Slot,
			*UpgradeCardKey(Id).ToString(), *GrowthJsonString(UpgradeCardDescription(Id)), *GrowthJsonString(DescribeCard(Id)));
	}
	FString ScheduledAltars;
	for (const bool bSpawned : BossAltarSpawned)
	{
		if (!ScheduledAltars.IsEmpty()) { ScheduledAltars += TEXT(","); }
		ScheduledAltars += bSpawned ? TEXT("true") : TEXT("false");
	}
	const AAICompanionDemoCharacter* Hero = Cast<AAICompanionDemoCharacter>(UGameplayStatics::GetPlayerPawn(this, 0));
	const ACompanionCharacter* Buddy = Cast<ACompanionCharacter>(UGameplayStatics::GetActorOfClass(this, ACompanionCharacter::StaticClass()));
	const ACompanionAIController* Ctl = CompanionController.Get();
	if (!Ctl) { Ctl = Cast<ACompanionAIController>(UGameplayStatics::GetActorOfClass(this, ACompanionAIController::StaticClass())); }
	return FString::Printf(TEXT("{\"pending\":%s,\"source\":\"%s\",\"rewardLevel\":%d,\"actualLevel\":%d,\"pool\":\"%s\",\"flipped\":%s,\"generation\":%u,\"queue\":[%s],\"offers\":[%s],\"fallback\":%s,\"gold\":%d,\"paidChestCount\":%d,\"nextChestCost\":%d,\"starterClaimed\":%s,\"markPicks\":%d,\"markCap\":%d,\"executeDamage\":%.3f,\"markDescription\":%s,\"playerShotInterval\":%.5f,\"playerAttackBenefit\":%s,\"playerMaxWalkSpeed\":%.3f,\"companionMaxWalkSpeed\":%.3f,\"companionMovePicks\":%d,\"companionMoveBenefit\":%s,\"elapsedSeconds\":%.3f,\"enraged\":%s,\"altarSpawned\":[%s]}"),
		bUpgradePending ? TEXT("true") : TEXT("false"), SourceName(ActiveReward.Source), ActiveReward.EarnedLevel, Level, PoolName,
		bRewardFlipped ? TEXT("true") : TEXT("false"), ChoiceGeneration, *Queue, *Candidates, *GrowthJsonString(OfferFallbackNote), GoldCount,
		ChestOpenCount, GetNextChestCost(), bStarterUpgradeClaimed ? TEXT("true") : TEXT("false"), CardTakenCounts.FindRef(EUpgradeId::HuntMark),
		Hero ? Hero->HuntMarkMaxStacks : 0, Ctl ? Ctl->ExecuteDamagePerMark : 0.f, *GrowthJsonString(AcquiredCardDescription(EUpgradeId::HuntMark)),
		Hero ? Hero->PlayerShotInterval : 0.f, HasPlayerAttackSpeedBenefit() ? TEXT("true") : TEXT("false"),
		Hero && Hero->GetCharacterMovement() ? Hero->GetCharacterMovement()->MaxWalkSpeed : 0.f,
		Buddy && Buddy->GetCharacterMovement() ? Buddy->GetCharacterMovement()->MaxWalkSpeed : 0.f, CardTakenCounts.FindRef(EUpgradeId::CompanionSwiftMove),
		HasCompanionMoveSpeedBenefit() ? TEXT("true") : TEXT("false"), ElapsedSeconds, bEnraged ? TEXT("true") : TEXT("false"), *ScheduledAltars);
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
	if (UpgradeIndex >= 0 && UpgradeIndex < DebugUpgradeCardCount())
	{
		ApplyUpgrade(static_cast<EUpgradeId>(UpgradeIndex));
	}
}

FString AWaveDirector::DebugUpgradeCardLabel(const int32 UpgradeIndex) const
{
	if (UpgradeIndex < 0 || UpgradeIndex >= DebugUpgradeCardCount()) { return FString(); }
	const EUpgradeId Id = static_cast<EUpgradeId>(UpgradeIndex);
	return Id == EUpgradeId::Flip ? TEXT("Flip（转换当前玩家等级奖励）") : CardShortName(Id);
}
