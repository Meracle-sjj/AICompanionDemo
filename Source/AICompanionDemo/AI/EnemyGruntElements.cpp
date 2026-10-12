/**
 * [INPUT]: EnemyGrunt双来源寒霜/传播资格、HuntMark/Enrage/Shock/Frost显示组件、既有灼烧表现；宿主游戏Delta/暂停流速门、主弹挂电/队友集中持续配对
 * [OUTPUT]: 本体冰/队友冰独立倒计时；只有本体实际弹命中源每0.5游戏秒传播200cm，感染者不接力；雪花显示、单来源消费与原感电/猎印保持，暂停/死亡清理
 * [POS]: AI/EnemyGrunt的局部实现拆分；状态与生命周期仍属于AEnemyGrunt，不新增游戏规则所有者
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */
#include "EnemyGrunt.h"
#include "Vfx/HuntMarkVisual.h"
#include "Vfx/EnemyEnrageVisual.h"
#include "Vfx/ShockMarkVisual.h"
#include "Vfx/FrostMarkVisual.h"
#include "AI/Combatant.h"
#include "Health/HealthComponent.h"
#include "Vfx/VfxLibrary.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	constexpr float FrostSpreadInterval = .5f;
	constexpr float FrostSpreadRadius = 200.f;
}

void AEnemyGrunt::SetEnraged(bool bInEnraged) { if (EnrageVisual) EnrageVisual->SetEnraged(bInEnraged); }
bool AEnemyGrunt::IsEnraged() const { return EnrageVisual && EnrageVisual->IsEnraged(); }
FString AEnemyGrunt::DebugEnragedState() const
{
#if WITH_EDITOR
	return EnrageVisual ? EnrageVisual->DebugVisualState() : TEXT("{}");
#else
	return TEXT("{}");
#endif
}

void AEnemyGrunt::TickElementResidue(float DeltaSeconds)
{
	// ---------- 与持续配对/电弧相同的游戏暂停门 ----------
	if (!GetWorld() || GetWorld()->IsPaused() || UGameplayStatics::GetGlobalTimeDilation(this) < .01f) return;
	BurnResidue = FMath::Max(BurnResidue - DeltaSeconds, 0.f);
	ShockResidue = FMath::Max(ShockResidue - DeltaSeconds, 0.f);
	if (IsDead())
	{
		PlayerFrostResidue = CompanionFrostResidue = FrostSpreadSourceRemaining = 0.f;
		FrostSpreadSource.Reset(); FrostSpreadAccum = 0.f;
		UpdateFrostVisual();
		if (ShockVisual) ShockVisual->SetShocked(false);
		if (HuntMarkVisual) HuntMarkVisual->SetStacks(0);
		return;
	}
	PlayerFrostResidue = FMath::Max(PlayerFrostResidue - DeltaSeconds, 0.f);
	CompanionFrostResidue = FMath::Max(CompanionFrostResidue - DeltaSeconds, 0.f);
	TickFrostSpread(DeltaSeconds);
	UpdateFrostVisual();
	if (ShockVisual) ShockVisual->SetShocked(ShockResidue>0.f && !IsDead());

	// 元素残留闪烁（09-30 两阶段裁决：挂标记=身上持续短闪，与 AI 弹触发的电弧连接爆闪区分）：
	// 09-30 节奏调优（用户实测"火焰没点出来就刷下一个=碎片化观感差"）：0.8s 节流+0.5s 寿命脉冲
	// ——"周期性电光跳动"而非密集闪烁；30m 距离裁剪（远处怪不刷特效）
	ElementFxAccum += DeltaSeconds;
	const APawn* ProxPlayer = GetWorld()->GetFirstPlayerController() ? GetWorld()->GetFirstPlayerController()->GetPawn() : nullptr;
	if (ElementFxAccum >= 0.8f && ProxPlayer && FVector::Dist2D(GetActorLocation(), ProxPlayer->GetActorLocation()) <= 3000.f)
	{
		ElementFxAccum = 0.f;
		const FVector Chest = GetActorLocation() + FVector(0.f, 0.f, 110.f);
		if (BurnResidue > 0.f)
		{
			UVfxLibrary::SpawnVfx(GetWorld(), EVfx::Fire, Chest + FVector(0.f, 0.f, -20.f), 0.55f, 0.6f);
		}
	}

	if (HuntMarkVisual) { HuntMarkVisual->SetStacks(IsDead() ? 0 : MarkStacks); }

}

void AEnemyGrunt::ApplyShockResidue(float Duration)
{
	if (IsDead() || Duration<=0.f || !HealthComponent || HealthComponent->IsInvulnerable()) return;
	ShockResidue=FMath::Max(ShockResidue,Duration);
	if (!ShockVisual)
	{
		ShockVisual=NewObject<UShockMarkVisual>(this,TEXT("ShockMarkVisual"));
		AddInstanceComponent(ShockVisual);
		ShockVisual->SetupAttachment(GetRootComponent());
		ShockVisual->SetRelativeLocation(FVector(0,0,70));
		ShockVisual->RegisterComponent();
	}
	ShockVisual->SetShocked(true);
}
FString AEnemyGrunt::DebugShockState() const
{
	return FString::Printf(TEXT("{\"remaining\":%.4f,\"dead\":%s,\"visual\":%s}"),
		GetShockResidue(),IsDead()?TEXT("true"):TEXT("false"),ShockVisual?*ShockVisual->DebugVisualState():TEXT("null"));
}

void AEnemyGrunt::AddHuntMark(int32 MaxStacks)
{
	MarkStacks = FMath::Min(MarkStacks + 1, MaxStacks);
	if (HuntMarkVisual) { HuntMarkVisual->SetStacks(IsDead() ? 0 : MarkStacks); }
}

int32 AEnemyGrunt::ConsumeHuntMarks()
{
	const int32 Count = MarkStacks;
	MarkStacks = 0;
	if (HuntMarkVisual) { HuntMarkVisual->SetStacks(0); }
	return Count;
}

FString AEnemyGrunt::DebugHuntMarkState() const
{
#if WITH_EDITOR
	return FString::Printf(TEXT("{\"mark_stacks\":%d,\"frost_residue\":%.4f,\"dead\":%s,\"visual\":%s}"),
		MarkStacks, GetFrostResidue(), IsDead() ? TEXT("true") : TEXT("false"),
		HuntMarkVisual ? *HuntMarkVisual->DebugVisualState() : TEXT("null"));
#else
	return FString();
#endif
}


// ---------- 寒霜来源与传播资格 ----------
void AEnemyGrunt::ApplyPlayerFrost(float Duration, float SpeedFactor, AActor* DirectShotSource)
{
	if (IsDead() || !HealthComponent || HealthComponent->IsInvulnerable() || HealthComponent->GetCurrentHealth() <= 0.f
		|| !FMath::IsFinite(Duration) || Duration <= 0.f || !FMath::IsFinite(SpeedFactor) || SpeedFactor <= 0.f || SpeedFactor > 1.f) return;
	PlayerFrostResidue = FMath::Max(PlayerFrostResidue, Duration);
	ApplySlow(Duration, SpeedFactor);
	// Nova与感染者传空来源；只有真实玩家弹路能登记有限时长的传播资格。
	if (IsValid(DirectShotSource) && DirectShotSource == UGameplayStatics::GetPlayerPawn(this, 0))
	{
		FrostSpreadSource = DirectShotSource;
		FrostSpreadSourceRemaining = FMath::Max(FrostSpreadSourceRemaining, Duration);
		FrostSpreadDuration = Duration; FrostSpreadSpeedFactor = SpeedFactor;
	}
	UpdateFrostVisual();
}

void AEnemyGrunt::ApplyCompanionFrost(float Duration, float SpeedFactor)
{
	if (IsDead() || !HealthComponent || HealthComponent->IsInvulnerable() || HealthComponent->GetCurrentHealth() <= 0.f
		|| !FMath::IsFinite(Duration) || Duration <= 0.f || !FMath::IsFinite(SpeedFactor) || SpeedFactor <= 0.f || SpeedFactor > 1.f) return;
	CompanionFrostResidue = FMath::Max(CompanionFrostResidue, Duration);
	ApplySlow(Duration, SpeedFactor); UpdateFrostVisual();
}

void AEnemyGrunt::ReceiveFrostSpread(float Duration, float SpeedFactor)
{
	if (IsDead() || !HealthComponent || HealthComponent->IsInvulnerable()) return;
	// 给队友冰补本体来源时保留既有控制，不把更强的减速改弱；传播绝不写SourceRemaining。
	if (GetFrostResidue() <= 0.f) ApplySlow(Duration, SpeedFactor);
	PlayerFrostResidue = FMath::Max(PlayerFrostResidue, Duration);
	UpdateFrostVisual();
}

void AEnemyGrunt::ConsumeFrostResidue(bool bPlayerSource)
{
	if (bPlayerSource)
	{
		PlayerFrostResidue = FrostSpreadSourceRemaining = 0.f;
		FrostSpreadSource.Reset(); FrostSpreadAccum = 0.f;
	}
	else CompanionFrostResidue = 0.f;
	UpdateFrostVisual();
}

void AEnemyGrunt::UpdateFrostVisual()
{
	const bool bVisible = GetFrostResidue() > 0.f;
	if (bVisible && !FrostVisual)
	{
		FrostVisual = NewObject<UFrostMarkVisual>(this, TEXT("FrostMarkVisual"));
		AddInstanceComponent(FrostVisual); FrostVisual->SetupAttachment(GetRootComponent());
		FrostVisual->SetRelativeLocation(FVector(0.f, 0.f, 70.f)); FrostVisual->RegisterComponent();
	}
	if (FrostVisual) FrostVisual->SetFrosted(bVisible);
}

void AEnemyGrunt::TickFrostSpread(float DeltaSeconds)
{
	FrostSpreadSourceRemaining = FMath::Max(FrostSpreadSourceRemaining - DeltaSeconds, 0.f);
	const AActor* Source = FrostSpreadSource.Get();
	const UHealthComponent* SourceHealth = IsValid(Source) ? Source->FindComponentByClass<UHealthComponent>() : nullptr;
	if (FrostSpreadSourceRemaining <= 0.f || GetPlayerFrostResidue() <= 0.f || !SourceHealth
		|| SourceHealth->IsDead() || SourceHealth->GetCurrentHealth() <= 0.f || Source->GetWorld() != GetWorld())
	{
		FrostSpreadSourceRemaining = 0.f; FrostSpreadSource.Reset(); FrostSpreadAccum = 0.f;
		return;
	}
	FrostSpreadAccum += DeltaSeconds;
	if (FrostSpreadAccum < FrostSpreadInterval) return;
	// 不回放历史位置或攒墙钟感染队列；卡帧本拍只检查当前近邻。
	FrostSpreadAccum = FMath::Fmod(FrostSpreadAccum, FrostSpreadInterval); ++FrostSpreadPulses;
	TArray<AActor*> Candidates; CollectAliveCombatants(GetWorld(), Candidates);
	for (AActor* Candidate : Candidates)
	{
		AEnemyGrunt* Other = Cast<AEnemyGrunt>(Candidate);
		if (!IsValid(Other) || Other == this || Other->GetPlayerFrostResidue() > 0.f
			|| FVector::DistSquared(GetActorLocation(), Other->GetActorLocation()) > FMath::Square(FrostSpreadRadius)) continue;
		Other->ReceiveFrostSpread(FrostSpreadDuration, FrostSpreadSpeedFactor);
		if (Other->GetPlayerFrostResidue() > 0.f) ++FrostSpreadApplications;
	}
}

FString AEnemyGrunt::DebugFrostState() const
{
	return FString::Printf(TEXT("{\"player_remaining\":%.4f,\"companion_remaining\":%.4f,\"spread_remaining\":%.4f,\"spread_interval\":%.2f,\"spread_radius\":%.0f,\"spread_pulses\":%d,\"spread_applications\":%d,\"source_valid\":%s,\"dead\":%s,\"visual\":%s}"),
		GetPlayerFrostResidue(), GetCompanionFrostResidue(), FrostSpreadSourceRemaining,
		FrostSpreadInterval, FrostSpreadRadius, FrostSpreadPulses, FrostSpreadApplications,
		FrostSpreadSource.IsValid() ? TEXT("true") : TEXT("false"), IsDead() ? TEXT("true") : TEXT("false"),
		FrostVisual ? *FrostVisual->DebugVisualState() : TEXT("null"));
}
