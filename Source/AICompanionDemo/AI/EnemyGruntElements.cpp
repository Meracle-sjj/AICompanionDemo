/**
 * [INPUT]: EnemyGrunt元素状态、HuntMark/Enrage/Shock显示组件、既有灼烧表现；宿主游戏Delta/暂停流速门、主弹挂电/队友集中持续配对
 * [OUTPUT]: 5秒感电刷新后自然衰减而不消费、固定闪电标记同步、猎印叠层/消费；暂停/<.01冻结残留，死亡隐藏，电弧寿命独立
 * [POS]: AI/EnemyGrunt的局部实现拆分；状态与生命周期仍属于AEnemyGrunt，不新增游戏规则所有者
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */
#include "EnemyGrunt.h"
#include "Vfx/HuntMarkVisual.h"
#include "Vfx/EnemyEnrageVisual.h"
#include "Vfx/ShockMarkVisual.h"
#include "Health/HealthComponent.h"
#include "Vfx/VfxLibrary.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"

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
	FrostResidue = FMath::Max(FrostResidue - DeltaSeconds, 0.f);
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
		MarkStacks, FrostResidue, IsDead() ? TEXT("true") : TEXT("false"),
		HuntMarkVisual ? *HuntMarkVisual->DebugVisualState() : TEXT("null"));
#else
	return FString();
#endif
}

