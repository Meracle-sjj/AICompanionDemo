/**
 * [INPUT]: 稳定升级ID与真实玩家/队友状态。
 * [OUTPUT]: 原ApplyUpgrade正文完整保留；效果/数值/领取恢复时序不变。
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

// ---------- 原ApplyUpgrade正文完整保留；效果/数值/领取恢复时序不变。 ----------

void AWaveDirector::ApplyUpgrade(EUpgradeId Id)
{
	if (IsRunTerminated()) { return; }
	if (Id == EUpgradeId::Flip) { FlipCurrentReward(); return; }
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
		// 保留未减半基数；CompanionCombatShots唯一传导入口统一乘50%，避免二次减半。
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
			PlayerChar->HuntMarkMaxStacks = FMath::Max(PlayerChar->HuntMarkMaxStacks, 5);
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
		// 元素·感电弹：玩家主弹/枪口散射挂电5s，队友周期配对持续传导而不消费。
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
	case EUpgradeId::PlayerRapidFire:
		if (!HasPlayerAttackSpeedBenefit()) { return; }
		PlayerChar->PlayerShotInterval = FMath::Max(PlayerChar->PlayerShotInterval * kPlayerAttackIntervalFactor, kPlayerAttackIntervalFloor);
		break;
	case EUpgradeId::CompanionSwiftMove:
		if (!HasCompanionMoveSpeedBenefit()) { return; }
		if (ACompanionCharacter* Buddy = Cast<ACompanionCharacter>(UGameplayStatics::GetActorOfClass(this, ACompanionCharacter::StaticClass())))
		{
			Buddy->GetCharacterMovement()->MaxWalkSpeed = FMath::Min(Buddy->GetCharacterMovement()->MaxWalkSpeed * kCompanionMoveFactor,
				PlayerChar->GetCharacterMovement()->MaxWalkSpeed);
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
			// 重建面板，保留本次宝箱的已选槽位和剩余领取数。
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
