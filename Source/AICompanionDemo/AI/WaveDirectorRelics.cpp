/**
 * [INPUT]: WaveDirector choice queue, BossRelicComponent persistent effects, shared card presentation.
 * [OUTPUT]: Source/earned-level preserving reward queue; Boss-specific rewards and serialized pauses; component-owned relic descriptions/cooldowns in HUD; completed choices restore input through GameMode arbitration; editor audit JSON.
 * [POS]: AI/WaveDirector reward lifecycle; uses the existing viewport/texture GC ownership.
 * [PROTOCOL]: Update module/UI AGENTS when the reward contract changes.
 */
#include "AI/WaveDirector.h"
#include "AICompanionDemo.h"
#include "AICompanionDemoGameMode.h"
#include "AICompanionDemoCharacter.h"
#include "Audio/SFXLibrary.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"
#include "Health/HealthComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Relics/BossRelicComponent.h"
#include "UI/UpgradeCardView.h"

void AWaveDirector::AppendRelicHudCards(FEndlessHudSnapshot& Snapshot) const
{
	const APawn* Hero = UGameplayStatics::GetPlayerPawn(this, 0);
	const UBossRelicComponent* Relics = Hero ? Hero->FindComponentByClass<UBossRelicComponent>() : nullptr;
	if (!Relics) return;
	for (int32 Index = 0; Index < static_cast<int32>(EBossRelicId::Count); ++Index)
	{
		const EBossRelicId Id = static_cast<EBossRelicId>(Index);
		if (!Relics->HasRelic(Id)) continue;
		FHudUpgradeCard& Card = Snapshot.UpgradeCards.AddDefaulted_GetRef();
		Card.UpgradeId = RelicIdName(Id);
		if (const auto* Meta = UpgradeCardPresentation::FindCardMetadata(Card.UpgradeId)) Card.Name = Meta->Title;
		Card.Description = Relics->DescribeRelic(Id);
		Card.Rarity = EUpgradeCardRarity::UniqueRelic;
		if (Id == EBossRelicId::SpiritFrenzy)
		{
			Card.CooldownRemaining = Relics->GetSpiritFrenzyCooldownRemaining();
			Card.CooldownTotal = Relics->GetSpiritFrenzyCooldownTotal();
			Card.ActiveRemaining = Relics->GetSpiritFrenzyActiveRemaining();
		}
	}
}

void AWaveDirector::ProcessNextChoice()
{
	const APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
	const UHealthComponent* Health = Player ? Player->FindComponentByClass<UHealthComponent>() : nullptr;
	if (IsRunTerminated() || !Health || Health->GetCurrentHealth() <= 0.f)
	{ PendingChoices.Reset(); return; }
	while (!bUpgradePending && !PendingChoices.IsEmpty())
	{
		const FRewardChoice Reward = PendingChoices[0];
		PendingChoices.RemoveAt(0);
		if (Reward.Source == ERewardSource::BossRelic) { ShowRelicChoice(Reward.BossIndex); }
		else { StartUpgradeReward(Reward); }
	}
}

void AWaveDirector::CompleteChoice()
{
	if (IsRunTerminated()) { return; }
	HideUpgradeOverlay();
	ChestTakenSlots.Reset();
	bChestDoublePick = false;
	bUpgradePending = false;
	bRelicPending = false;
	RelicChoiceBoss = 0;
	ActiveReward = FRewardChoice();
	ActiveRewardPool = ERewardPool::Mixed;
	bRewardFlipped = false;
	OfferFallbackNote.Reset();
	ProcessNextChoice();
	if (!bUpgradePending) { UGameplayStatics::SetGlobalTimeDilation(this, 1.f); }
	if (AAICompanionDemoGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<AAICompanionDemoGameMode>() : nullptr)
	{
		GM->RestoreRunInput();
	}
	else if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		const bool bNeedsUI = bTestMode || bUpgradePending;
		PC->bShowMouseCursor = bNeedsUI;
		if (bNeedsUI)
		{
			FInputModeGameAndUI Mode;
			Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
			Mode.SetHideCursorDuringCapture(false);
			PC->SetInputMode(Mode);
		}
		else { PC->SetInputMode(FInputModeGameOnly()); }
	}
}

void AWaveDirector::ShowRelicChoice(int32 BossIndex)
{
	if (IsRunTerminated() || BossIndex < 1 || BossIndex > 3) { return; }
	APawn* Hero = UGameplayStatics::GetPlayerPawn(this, 0);
	UBossRelicComponent* Relics = Hero ? Hero->FindComponentByClass<UBossRelicComponent>() : nullptr;
	if (!Relics || !GEngine || !GEngine->GameViewport) { return; }
	bool bAllOwned = true;
	for (int32 Slot=0; Slot<3; ++Slot)
	{
		RelicOffers[Slot] = static_cast<EBossRelicId>((BossIndex-1)*3+Slot);
		bAllOwned &= Relics->HasRelic(RelicOffers[Slot]);
	}
	if (bAllOwned) { return; }
	RelicChoiceBoss = BossIndex;
	ActiveReward = { ERewardSource::BossRelic, 0, BossIndex };
	ActiveRewardPool = ERewardPool::Mixed;
	bRewardFlipped = false;
	OfferFallbackNote.Reset();
	bUpgradePending = true;
	bRelicPending = true;
	bChestDoublePick = false;
	UGameplayStatics::SetGlobalTimeDilation(this, 0.f);
	if (!UpgradeCardView) { UpgradeCardView = NewObject<UUpgradeCardView>(this); }
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		PC->bShowMouseCursor = true;
		FInputModeGameAndUI Mode;
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PC->SetInputMode(Mode);
	}
	TArray<FUpgradeCardOption> Options;
	const uint32 Generation = ++ChoiceGeneration;
	for (int32 Slot=0; Slot<3; ++Slot)
	{
		FUpgradeCardOption& Card = Options.AddDefaulted_GetRef();
		Card.UpgradeId = RelicIdName(RelicOffers[Slot]);
		Card.Slot = Slot;
		Card.Rarity = EUpgradeCardRarity::UniqueRelic;
		Card.bTaken = Relics->HasRelic(RelicOffers[Slot]);
		Card.FullDescription = Relics->DescribeRelic(RelicOffers[Slot]);
		Card.OnSelected = FSimpleDelegate::CreateWeakLambda(this, [this, Slot, Generation]()
		{ if (Generation == ChoiceGeneration) { SelectRelicOffer(Slot); } });
	}
	const FString Boss = BossNames.IsValidIndex(BossIndex-1) ? BossNames[BossIndex-1] : TEXT("Boss");
	UpgradeOverlay = UpgradeCardView->Build(Options, Boss+TEXT(" · 遗物三选一"),
		TEXT("独特 · 金红遗物｜选择一项永久能力 · 点击或按 1 / 2 / 3"), false);
	GEngine->GameViewport->AddViewportWidgetContent(UpgradeOverlay.ToSharedRef(), 100);
	USFXLibrary::PlaySFX(GetWorld(), EGameSFX::UpgradeChoice);
	UE_LOG(LogAICompanionDemo, Warning, TEXT("[Relic] Boss%d遗物三选一已展示"), BossIndex);
}

void AWaveDirector::SelectRelicOffer(int32 Slot)
{
	if (IsRunTerminated() || !bUpgradePending || !bRelicPending || Slot<0 || Slot>=3) { return; }
	APawn* Hero = UGameplayStatics::GetPlayerPawn(this, 0);
	UBossRelicComponent* Relics = Hero ? Hero->FindComponentByClass<UBossRelicComponent>() : nullptr;
	if (!Relics || !Relics->AcquireRelic(RelicOffers[Slot])) { return; }
	UE_LOG(LogAICompanionDemo, Warning, TEXT("[Relic] 领取%s，恢复/续接下一奖励"), *RelicIdName(RelicOffers[Slot]).ToString());
	CompleteChoice();
}

void AWaveDirector::DebugGrantRelic(int32 RelicIndex)
{
#if WITH_EDITOR
	APawn* Hero = UGameplayStatics::GetPlayerPawn(this, 0);
	UBossRelicComponent* Relics = Hero ? Hero->FindComponentByClass<UBossRelicComponent>() : nullptr;
	if (Relics && RelicIndex>=0 && RelicIndex<9) { Relics->AcquireRelic(static_cast<EBossRelicId>(RelicIndex)); }
#endif
}

FString AWaveDirector::DebugRelicState() const
{
#if WITH_EDITOR
	const AAICompanionDemoCharacter* Hero = Cast<AAICompanionDemoCharacter>(UGameplayStatics::GetPlayerPawn(this, 0));
	const UBossRelicComponent* Relics = Hero ? Hero->FindComponentByClass<UBossRelicComponent>() : nullptr;
	return FString::Printf(TEXT("{\"relicPending\":%s,\"boss\":%d,\"queue\":%d,\"radianceDamage\":%d,\"radianceRadius\":%.1f,\"blinkCD\":%.1f,\"recallCD\":%.1f,\"blinkRemaining\":%.3f,\"recallRemaining\":%.3f,\"effects\":%s}"),
		bRelicPending ? TEXT("true") : TEXT("false"), RelicChoiceBoss, PendingChoices.Num(),
		RadianceDamagePerTick, RadianceRadius, Hero ? Hero->GetDashCooldownTotal() : 0.f,
		Hero ? Hero->GetRecallCooldownTotal() : 0.f, Hero ? Hero->GetDashCooldownRemaining() : 0.f,
		Hero ? Hero->GetRecallCooldownRemaining() : 0.f, Relics ? *Relics->DebugState() : TEXT("{}"));
#else
	return TEXT("{}");
#endif
}
