/**
 * [INPUT]: Character回响解锁/治疗累计/领域参数；HealthComponent的OnHealApplied与真实血量；CollectAliveCombatants敌人契约；PointLight/Timer/DrawDebug与SFXLibrary已存在Explosion槽
 * [OUTPUT]: 玩家回响脉冲治疗触发、2s扩散、0.1s/1伤持续压制、25伤终爆与清理；保留10DPS，卡帧补拍按历史扩散半径，统一Health实际量漫画反馈、命中闪光和跟随光脉冲；终爆只播放一次蠕虫撞地同款Explosion
 * [POS]: 模块根Character能力实现的局部拆分；方法与状态仍归AAICompanionDemoCharacter，主文件仅负责订阅/逐帧调用/死亡清理，未新增玩法所有者
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */

#include "AICompanionDemoCharacter.h"
#include "AI/Combatant.h"
#include "AICompanionDemo.h"
#include "Audio/SFXLibrary.h"
#include "Components/PointLightComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "Health/HealthComponent.h"
#include "TimerManager.h"

void AAICompanionDemoCharacter::HandleHealApplied(UHealthComponent* /*HealthComp*/, float Attempted, float Actual)
{
	// 持辉耀结界按施放量计，未持卡只计真实回血；治疗意图事件满血仍会广播。
	if (!bHealNovaUnlocked)
	{
		return;
	}
	const float Count = bHealNovaCountWhenFull ? Attempted : Actual;
	if (Count <= 0.f)
	{
		return;
	}
	AccumHealSinceNova += Count;
	if (AccumHealSinceNova >= HealNovaThreshold)
	{
		FireHealNova();
	}
}

void AAICompanionDemoCharacter::FireHealNova()
{
	AccumHealSinceNova = 0.f;
	// 活跃领域不叠加，维持原治疗累计语义；下一次独立领域重新从首拍计时。
	if (bNovaActive)
	{
		return;
	}
	bNovaActive = true;
	bNovaFinishing = false;
	NovaElapsed = 0.f;
	NovaDotCountdown = FMath::Max(HealNovaDotInterval, 0.1f);
	NovaHitPulseTime = 0.f;
	if (UPointLightComponent* Light = NewObject<UPointLightComponent>(this))
	{
		Light->SetLightColor(FLinearColor(FColor::FromHex(TEXT("7CFC8A"))));
		Light->SetAttenuationRadius(HealNovaMaxRadius + 500.f);
		Light->SetIntensity(1.f);
		Light->RegisterComponent();
		Light->SetWorldLocation(GetActorLocation() + FVector(0.f, 0.f, 120.f));
		NovaLightRef = Light;
	}
	UE_LOG(LogAICompanionDemo, Warning,
		TEXT("[HealNova] 回响脉冲蔓延启动：0→%.0fcm / %.1fs；%.1f伤/%.2fs（%.1fDPS）"),
		HealNovaMaxRadius, HealNovaExpandTime, HealNovaDotDamage, NovaDotCountdown,
		HealNovaDotDamage / NovaDotCountdown);
}

void AAICompanionDemoCharacter::TickHealNova(float DeltaSeconds)
{
	if (!bNovaActive || bNovaFinishing)
	{
		return;
	}
	const float ExpandTime = FMath::Max(HealNovaExpandTime, 0.2f);
	const float Interval = FMath::Max(HealNovaDotInterval, 0.1f);
	const float PreviousElapsed = FMath::Min(NovaElapsed, ExpandTime);
	NovaElapsed = FMath::Min(PreviousElapsed + FMath::Max(DeltaSeconds, 0.f), ExpandTime);
	const float Advance = NovaElapsed - PreviousElapsed;
	NovaHitPulseTime = FMath::Max(NovaHitPulseTime - Advance, 0.f);
	NovaDotCountdown -= Advance;

	// 保留跨帧余数；一个卡帧可补多拍，但每拍只覆盖当时已经展开的半径。
	// 只扣领域时长以内的时间，跨过2s的长帧不会多算伤害；终点DOT和终爆各结算一次。
	while (bNovaActive && NovaDotCountdown <= KINDA_SMALL_NUMBER)
	{
		const float PulseElapsed = FMath::Clamp(NovaElapsed + NovaDotCountdown, 0.f, ExpandTime);
		const float PulseRadius = HealNovaMaxRadius * PulseElapsed / ExpandTime;
		NovaDotCountdown += Interval;
		// 同一帧仅最后一拍显示命中闪光；每次真实扣血的数字统一交Health。
		const float FeedbackLife = NovaDotCountdown > KINDA_SMALL_NUMBER && NovaElapsed < ExpandTime
			? FMath::Min(FMath::Min(Interval * 0.8f, 0.08f), ExpandTime - NovaElapsed) : 0.f;
		const int32 Hits = NovaDamageRing(HealNovaDotDamage, PulseRadius, FeedbackLife);
		if (!bNovaActive)
		{
			return; // 同步伤害回调若让玩家死亡，ClearHealNovaState已接管清理。
		}
		if (Hits > 0)
		{
			NovaHitPulseTime = 0.08f;
		}
		UE_LOG(LogAICompanionDemo, Verbose,
			TEXT("[HealNova][DOT] t=%.2f r=%.0f hits=%d damage=%.1f"),
			PulseElapsed, PulseRadius, Hits, HealNovaDotDamage);
	}

	const float Alpha = NovaElapsed / ExpandTime;
	const float Radius = HealNovaMaxRadius * Alpha;
	const FVector Center = GetActorLocation() + FVector(0.f, 0.f, 40.f);
	// 连续外形走引擎逐帧清空的World批次：低帧率仍可见，升级暂停不积累长寿命线段。
	DrawDebugSphere(GetWorld(), Center, Radius, 20, FColor::FromHex(TEXT("7CFC8A")), false, -1.f, 0, 2.f);
	DrawDebugSphere(GetWorld(), Center, Radius * 0.66f, 14, FColor::FromHex(TEXT("7CFC8A")), false, -1.f, 0, 1.f);
	DrawDebugSphere(GetWorld(), Center, Radius * 0.33f, 10, FColor::FromHex(TEXT("E8FFB0")), false, -1.f, 0, 1.f);
	if (NovaLightRef.IsValid())
	{
		NovaLightRef->SetWorldLocation(Center + FVector(0.f, 0.f, 80.f));
		const float HitPulse = FMath::Clamp(NovaHitPulseTime / 0.08f, 0.f, 1.f);
		NovaLightRef->SetIntensity(Alpha * (3000.f + 1000.f * HitPulse));
	}
	if (NovaElapsed >= ExpandTime)
	{
		FinishHealNova();
	}
}

int32 AAICompanionDemoCharacter::NovaDamageRing(float Damage, float Radius, float FeedbackLifetime)
{
	const FVector Center = GetActorLocation() + FVector(0.f, 0.f, 40.f);
	int32 Hits = 0;
	TArray<AActor*> RingVictims;
	CollectAliveCombatants(GetWorld(), RingVictims);
	for (AActor* Victim : RingVictims)
	{
		const FVector VictimLocation = Victim->GetActorLocation();
		if (FVector::DistSquared(Center, VictimLocation) > Radius * Radius)
		{
			continue;
		}
		if (UHealthComponent* VictimHealth = Cast<ICombatant>(Victim)->GetHealthComponent())
		{
			const float Before = VictimHealth->GetCurrentHealth();
			VictimHealth->ApplyCombatDamage(Damage, this, {ECombatDamageKind::Pulse, VictimLocation + FVector(0.f, 0.f, 95.f)});
			const float AppliedDamage = Before - VictimHealth->GetCurrentHealth();
			if (AppliedDamage <= 0.f)
			{
				continue; // 无敌/零伤害不画虚假的扣血数字或命中闪光。
			}
			++Hits;
			if (FeedbackLifetime > 0.f)
			{
				DrawDebugSphere(GetWorld(), VictimLocation + FVector(0.f, 0.f, 35.f),
					24.f, 8, FColor::FromHex(TEXT("E8FFB0")), false,
					FMath::Min(FeedbackLifetime, 0.08f), 0, 1.5f);
			}
		}
	}
	return Hits;
}

void AAICompanionDemoCharacter::FinishHealNova()
{
	if (!bNovaActive || bNovaFinishing)
	{
		return;
	}
	// 终爆期间保持活跃以拒绝新领域；独立闸门避免同步回调重入重复结算。
	bNovaFinishing = true;
	const int32 Hits = NovaDamageRing(HealNovaDamage, HealNovaMaxRadius);
	const FVector Center = GetActorLocation() + FVector(0.f, 0.f, 40.f);
	DrawDebugSphere(GetWorld(), Center, HealNovaMaxRadius, 20, FColor::FromHex(TEXT("7CFC8A")), false, 0.4f, 0, 4.f);
	DrawDebugSphere(GetWorld(), Center, HealNovaMaxRadius * 0.6f, 14, FColor::FromHex(TEXT("E8FFB0")), false, 0.3f, 0, 3.f);
	constexpr int32 Seg = 24;
	for (int32 i = 0; i < Seg; ++i)
	{
		const float A0 = 2.f * PI * i / Seg;
		const float A1 = 2.f * PI * (i + 1) / Seg;
		DrawDebugLine(GetWorld(),
			Center + FVector(FMath::Cos(A0) * HealNovaMaxRadius, FMath::Sin(A0) * HealNovaMaxRadius, 10.f),
			Center + FVector(FMath::Cos(A1) * HealNovaMaxRadius, FMath::Sin(A1) * HealNovaMaxRadius, 10.f),
			FColor::FromHex(TEXT("7CFC8A")), false, 0.5f, 0, 6.f);
	}
	if (UPointLightComponent* FlashLight = NewObject<UPointLightComponent>(this))
	{
		FlashLight->SetIntensity(6000.f);
		FlashLight->SetLightColor(FLinearColor(FColor::FromHex(TEXT("7CFC8A"))));
		FlashLight->SetAttenuationRadius(HealNovaMaxRadius + 1000.f);
		FlashLight->RegisterComponent();
		FlashLight->SetWorldLocation(Center + FVector(0.f, 0.f, 150.f));
		TWeakObjectPtr<UPointLightComponent> WeakLight(FlashLight);
		FTimerHandle FlashHandle;
		GetWorldTimerManager().SetTimer(FlashHandle, [WeakLight]()
		{
			if (WeakLight.IsValid())
			{
				WeakLight->DestroyComponent();
			}
		}, 0.35f, false);
	}
	ClearHealNovaState();
	// 与BossWurmEffects::DiveImpactBurst同入口/同槽；每个领域只在终爆响一声。
	USFXLibrary::PlaySFX(GetWorld(), EGameSFX::Explosion);
	UE_LOG(LogAICompanionDemo, Warning,
		TEXT("[HealNova] 回响脉冲终点爆：%d 敌受创 %.0f 伤 @ %.0fcm；SFX=Explosion"),
		Hits, HealNovaDamage, HealNovaMaxRadius);
}

void AAICompanionDemoCharacter::ClearHealNovaState()
{
	bNovaActive = false;
	bNovaFinishing = false;
	NovaElapsed = 0.f;
	NovaDotCountdown = 0.f;
	NovaHitPulseTime = 0.f;
	if (NovaLightRef.IsValid())
	{
		NovaLightRef->DestroyComponent();
	}
	NovaLightRef = nullptr;
}
