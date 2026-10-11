/**
 * [INPUT]: 经验金币账本、奖励队列、选择视图和候选池。
 * [OUTPUT]: 奖励来源/等级/Flip/槽位/前置资格与金币账本归导演；成功开箱台词由箱体提交，不在奖励入口重复发声。
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

// ---------- 奖励来源/等级/Flip/槽位/前置资格仍归导演，不改变抽取行为。 ----------

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
		EnterUpgradePause(false, ERewardSource::LevelUp, Level);
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
	if (CanFlipCurrentReward() && PC->WasInputKeyJustPressed(EKeys::F))
	{
		FlipCurrentReward();
		return; // 同帧F与数字键只执行一次操作，避免新池被同帧领取。
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

void AWaveDirector::EnterUpgradePause(const bool bChestPool, const ERewardSource Source, const int32 RewardLevel)
{
	if (IsRunTerminated()) { return; }
	const FRewardChoice Reward { bChestPool ? ERewardSource::LockedChest : Source, RewardLevel, 0 };
	if (bUpgradePending) { PendingChoices.Add(Reward); return; }
	StartUpgradeReward(Reward);
}

void AWaveDirector::StartUpgradeReward(const FRewardChoice& Reward)
{
	if (IsRunTerminated()) { return; }
	ActiveReward = Reward;
	bRewardFlipped = false;
	ActiveRewardPool = ERewardPool::Mixed;
	if (Reward.Source == ERewardSource::LevelUp && Reward.EarnedLevel >= 9)
	{
		ActiveRewardPool = ((Reward.EarnedLevel - 9) % 2 == 0) ? ERewardPool::Player : ERewardPool::AI;
	}
	const bool bChestPool = Reward.Source == ERewardSource::LockedChest;
	UGameplayStatics::SetGlobalTimeDilation(this, 0.f);
	bUpgradePending = true;
	bRelicPending = false;
	bChestDoublePick = false;
	ChestPicksRemaining = 0;
	ChestTakenSlots.Reset();
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

bool AWaveDirector::CanFlipCurrentReward() const
{
	const AAICompanionDemoGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<AAICompanionDemoGameMode>() : nullptr;
	return !IsRunTerminated() && bUpgradePending && !bRelicPending && !bChestDoublePick
		&& ActiveReward.Source == ERewardSource::LevelUp && ActiveRewardPool == ERewardPool::Player && !bRewardFlipped
		&& GetWorld() && !GetWorld()->IsPaused()
		&& (!GM || (!GM->IsGameplayPaused() && !GM->IsEndingShown()));
}

void AWaveDirector::FlipCurrentReward()
{
	if (!CanFlipCurrentReward()) { return; }
	bRewardFlipped = true;
	ActiveRewardPool = ERewardPool::AI;
	if (UpgradeOverlay.IsValid() && GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(UpgradeOverlay.ToSharedRef());
	}
	UpgradeOverlay.Reset();
	++ChoiceGeneration;
	RollOffers(false);
	ShowUpgradeOverlay(false);
	UE_LOG(LogAICompanionDemo, Warning, TEXT("[Growth] Lv.%d 本次玩家奖励Flip为AI奖励，排队奖励不变"), ActiveReward.EarnedLevel);
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
	if (AAICompanionDemoGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<AAICompanionDemoGameMode>() : nullptr)
	{
		GM->RestoreRunInput();
	}
	else if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		PC->bShowMouseCursor = true;
		FInputModeGameAndUI InputMode;
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		InputMode.SetHideCursorDuringCapture(false);
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
	FString Heading;
	if (bChest) { Heading = TEXT("锁箱大奖 · 六选二"); }
	else if (ActiveReward.Source == ERewardSource::StarterChest) { Heading = TEXT("初始补给 · 免费三选一"); }
	else if (ActiveReward.Source == ERewardSource::NormalChest) { Heading = TEXT("宝箱补给 · 三选一"); }
	else
	{
		const int32 RewardLevel = ActiveReward.EarnedLevel > 0 ? ActiveReward.EarnedLevel : Level;
		const TCHAR* PoolLabel = ActiveRewardPool == ERewardPool::Player ? TEXT("玩家成长")
			: ActiveRewardPool == ERewardPool::AI ? TEXT("AI成长") : TEXT("混合成长");
		Heading = FString::Printf(TEXT("升级 · Lv.%d · %s%s"), RewardLevel, PoolLabel, bRewardFlipped ? TEXT("（Flip）") : TEXT(""));
	}
	FString Hint = bChest ? FString::Printf(TEXT("还可领取 %d 张 · 点击卡片或按 1–6"), ChestPicksRemaining)
		: TEXT("选择一项强化 · 点击卡片或按 1 / 2 / 3");
	if (!OfferFallbackNote.IsEmpty()) { Hint += TEXT("\n") + OfferFallbackNote; }
	FSimpleDelegate FlipAction;
	if (CanFlipCurrentReward())
	{
		FlipAction = FSimpleDelegate::CreateWeakLambda(this, [this, Generation]()
		{ if (Generation == ChoiceGeneration) { FlipCurrentReward(); } });
	}
	UpgradeOverlay = UpgradeCardView->Build(Options, Heading, Hint, bChest, FlipAction);
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
	const EUpgradeId Id = bChestDoublePick ? ChestOffers[Slot] : Offers[Slot];
	if (Id == EUpgradeId::Flip) { FlipCurrentReward(); return; }
	ApplyUpgrade(Id);
}

void AWaveDirector::HideUpgradeOverlay()
{
	if (UpgradeOverlay.IsValid() && GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(UpgradeOverlay.ToSharedRef());
	}
	UpgradeOverlay.Reset();
	// 这里只拆视口树；奖励队列完成状态后由组合根统一仲裁输入，EndPlay不抢焦点。
}

void AWaveDirector::RollOffers(const bool bChestPool)
{
	OfferFallbackNote.Reset();
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
	if (HasPlayerAttackSpeedBenefit())
	{
		Pool.Add({ EUpgradeId::PlayerRapidFire, TEXT("PlayerAttackRate"), 0.8f, NAME_None, ECardRarity::Common });
	}
	if (HasCompanionMoveSpeedBenefit())
	{
		Pool.Add({ EUpgradeId::CompanionSwiftMove, TEXT("CompanionMove"), 0.7f, NAME_None, ECardRarity::Common });
	}
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

	if (!bChestPool && ActiveRewardPool != ERewardPool::Mixed)
	{
		const bool bPlayerPool = ActiveRewardPool == ERewardPool::Player;
		Pool.RemoveAll([bPlayerPool](const FUpgradeCardDef& C) { return IsPlayerPoolCard(C.Id) != bPlayerPool; });
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
			const EUpgradeId Filler = bChestPool ? EUpgradeId::GoldPouch
				: ActiveRewardPool == ERewardPool::AI ? EUpgradeId::HeavyRounds : EUpgradeId::Vigor;
			OfferFallbackNote = bChestPool ? TEXT("候选不足：空槽以金币袋填位，仍为六选二。")
				: ActiveRewardPool == ERewardPool::AI ? TEXT("AI候选不足：空槽以可叠加重击弹头填位。")
				: ActiveRewardPool == ERewardPool::Player ? TEXT("玩家候选不足：空槽以可叠加强身填位。")
				: TEXT("混合候选不足：空槽以可叠加强身填位。");
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

bool AWaveDirector::IsPlayerPoolCard(const EUpgradeId Id)
{
	switch (Id)
	{
	case EUpgradeId::Blink:
	case EUpgradeId::GlassCannon:
	case EUpgradeId::Vigor:
	case EUpgradeId::SwiftMove:
	case EUpgradeId::HealNova:
	case EUpgradeId::BlinkBomb:
	case EUpgradeId::FrostShotPlayer:
	case EUpgradeId::ShockShot:
	case EUpgradeId::HuntMark:
	case EUpgradeId::ShockOverload:
	case EUpgradeId::MarkMaster:
	case EUpgradeId::FrostDeep:
	case EUpgradeId::PlayerScatter:
	case EUpgradeId::PlayerPierce:
	case EUpgradeId::PlayerPrism:
	case EUpgradeId::PlayerRapidFire:
	case EUpgradeId::CompanionSwiftMove:
		return true;
	default: return false;
	}
}

bool AWaveDirector::HasPlayerAttackSpeedBenefit() const
{
	const AAICompanionDemoCharacter* Hero = Cast<AAICompanionDemoCharacter>(UGameplayStatics::GetPlayerPawn(this, 0));
	return Hero && Hero->bPlayerShootingUnlocked && FMath::IsFinite(Hero->PlayerShotInterval)
		&& Hero->PlayerShotInterval > kPlayerAttackIntervalFloor + KINDA_SMALL_NUMBER;
}

bool AWaveDirector::HasCompanionMoveSpeedBenefit() const
{
	if (CardTakenCounts.FindRef(EUpgradeId::CompanionSwiftMove) >= kCompanionMoveMaxPicks) { return false; }
	const AAICompanionDemoCharacter* Hero = Cast<AAICompanionDemoCharacter>(UGameplayStatics::GetPlayerPawn(this, 0));
	const ACompanionCharacter* Buddy = Cast<ACompanionCharacter>(UGameplayStatics::GetActorOfClass(this, ACompanionCharacter::StaticClass()));
	const UCharacterMovementComponent* HeroMove = Hero ? Hero->GetCharacterMovement() : nullptr;
	const UCharacterMovementComponent* BuddyMove = Buddy ? Buddy->GetCharacterMovement() : nullptr;
	if (!HeroMove || !BuddyMove || !FMath::IsFinite(BuddyMove->MaxWalkSpeed) || !FMath::IsFinite(HeroMove->MaxWalkSpeed)) { return false; }
	const float NewSpeed = FMath::Min(BuddyMove->MaxWalkSpeed * kCompanionMoveFactor, HeroMove->MaxWalkSpeed);
	return NewSpeed > BuddyMove->MaxWalkSpeed + KINDA_SMALL_NUMBER;
}

void AWaveDirector::AddGold(const int32 Amount){
	if (IsRunTerminated() || Amount <= 0)
	{
		return;
	}
	GoldCount += Amount;
}

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
	// 台词由成功开箱实体提交统一前三箱序号；奖励账本不再重复发声。
	EnterUpgradePause(true);
}

bool AWaveDirector::TryGrantStarterUpgrade()
{
	if (IsRunTerminated() || bUpgradePending || bStarterUpgradeClaimed) { return false; }
	bStarterUpgradeClaimed = true;
	UE_LOG(LogAICompanionDemo, Warning, TEXT("[Growth] 初始宝箱免费三选一；金币=%d 收费箱次数=%d"), GoldCount, ChestOpenCount);
	EnterUpgradePause(false, ERewardSource::StarterChest);
	return true;
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
	// 台词交回宝箱真实成功点，金币不足或重复互动不占成功序号。
	EnterUpgradePause(false);
	return true;
}
