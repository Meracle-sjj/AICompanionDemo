/** [INPUT]: 玩家自动射击、ICombatant活敌、共用元素来源解析与遗物主攻回调。
 * [OUTPUT]: 玩家弹路整发命中去重，遗物主攻仅一次；每个成功弹射命中另走独立针刺概率口，不重复主攻爆炸；本体弹路可传播、本体Nova挂本体冰不授源。
 * [POS]: 玩家战斗局部实现，与输入/生命周期分离。 [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md */
#include "AICompanionDemoCharacter.h"
#include "AICompanionDemo.h"
#include "AI/Combatant.h"
#include "AI/CombatElementalDamage.h"
#include "AI/EnemyGrunt.h"
#include "Audio/SFXLibrary.h"
#include "Camera/CameraComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "Health/HealthComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Relics/BossRelicComponent.h"
#include "Vfx/VfxLibrary.h"

// ---------- 玩家射击（09-25 等级成长：Lv.6 解锁左键） ----------

void AAICompanionDemoCharacter::PlayerFire(AActor* Target)
{
	if (!Health || Health->GetCurrentHealth() <= 0.f || GetWorld()->IsPaused()
		|| UGameplayStatics::GetGlobalTimeDilation(this) < .01f) return;
	const FVector Start = GetActorLocation() + FVector(0, 0, 80);
	const FVector Dir = IsValid(Target)
		? (Target->GetActorLocation() + FVector(0, 0, 40) - Start).GetSafeNormal()
		: (GetFollowCamera() ? GetFollowCamera()->GetForwardVector() : FVector::ZeroVector);
	if (Dir.IsNearlyZero()) return;
	const FVector End = Start + Dir * PlayerShotRange;
	struct FDirectHit
	{
		TWeakObjectPtr<AActor> Victim;
		FVector Point;
		FVector SourceCenter;
	};
	TArray<FDirectHit> DirectHits;
	TSet<AActor*> PayloadTargets;
	TArray<AActor*> LiveCandidates;
	CollectAliveCombatants(GetWorld(), LiveCandidates);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PlayerShot), false, this);
	FVector BeamEnd = End;
	// SweepMulti在首个Block命中后停止；逐次忽略已找到单位可真实贯穿胶囊，同时仍被墙/地形挡住。
	for (int32 Pass = 0; Pass <= LiveCandidates.Num(); ++Pass)
	{
		FHitResult Hit;
		if (!GetWorld()->SweepSingleByChannel(Hit, Start, End, FQuat::Identity,
			ECC_Visibility, FCollisionShape::MakeSphere(12.f), Params)) break;
		BeamEnd = Hit.ImpactPoint;
		AActor* Actor = Hit.GetActor();
		ICombatant* Combatant = Cast<ICombatant>(Actor);
		if (!IsValid(Actor) || !Combatant) break; // 实体墙不是可穿透的战斗者。
		Params.AddIgnoredActor(Actor);
		if (!Combatant->IsDead()) DirectHits.Add({Actor, Hit.ImpactPoint, Actor->GetActorLocation()});
		if (!bPlayerPierce) break;
		BeamEnd = End;
	}
	TWeakObjectPtr<AActor> Primary;
	if (DirectHits.Num()) Primary = DirectHits[0].Victim;
	const FVector PrimaryPoint = DirectHits.Num() ? DirectHits[0].Point : BeamEnd;
	const auto DealHit = [this, &PayloadTargets](AActor* Actor, const FVector& Point, float Damage, FColor& Color, ECombatDamageKind Kind)
	{
		if (!IsValid(Actor) || PayloadTargets.Contains(Actor)) return false;
		ICombatant* Combatant = Cast<ICombatant>(Actor);
		UHealthComponent* VictimHealth = Combatant ? Combatant->GetHealthComponent() : nullptr;
		if (!Combatant || Combatant->IsDead() || !IsValid(VictimHealth)) return false;
		PayloadTargets.Add(Actor); // 同一发的主弹/侧弹/贯穿/单跳只算一次元素及伤害。
		const float FinalDamage = ApplyElementPayload(Actor, Damage, Color, Point, &Kind, true);
		VictimHealth->ApplyCombatDamage(FinalDamage, this, {Kind, Point + FVector(0, 0, 30)});
		return true;
	};
	FColor ShotColor = FColor::White;
	for (const FDirectHit& Hit : DirectHits)
	{
		FColor Color = FColor::White;
		if (DealHit(Hit.Victim.Get(), Hit.Point, PlayerShotDamage, Color, ECombatDamageKind::Projectile))
		{
			ShotColor = Color;
			USFXLibrary::PlaySFX(GetWorld(), EGameSFX::Hit_Enemy);
		}
	}
	DrawDebugLine(GetWorld(), Start, BeamEnd, ShotColor, false, .1f, 0, 2.f);
	if (bPlayerScatter)
	{
		for (const float Yaw : {15.f, -15.f})
		{
			const FVector SideEnd = Start + Dir.RotateAngleAxis(Yaw, FVector::UpVector) * PlayerShotRange;
			FHitResult SideHit;
			FCollisionQueryParams SideParams(SCENE_QUERY_STAT(PlayerSideShot), false, this);
			const bool Hit = GetWorld()->SweepSingleByChannel(SideHit, Start, SideEnd, FQuat::Identity,
				ECC_Visibility, FCollisionShape::MakeSphere(12.f), SideParams);
			FColor Color = FColor::White;
			if (Hit) DealHit(SideHit.GetActor(), SideHit.ImpactPoint, PlayerShotDamage * .7f, Color, ECombatDamageKind::Projectile);
			DrawDebugLine(GetWorld(), Start, Hit ? SideHit.ImpactPoint : SideEnd, Color, false, .1f, 0, 1.5f);
		}
	}
	// 感电的额外弹道与主弹同时从枪口散开，空射也保留清楚的分叉形状；不追踪/弹跳。
	// 已有三向散射时让赠叉再偏到30度，避免与现成15度侧弹完全重合。
	if (bShockShotUnlocked)
	{
		const float ForkYaw = bPlayerScatter ? 30.f : 15.f;
		const FVector ForkEnd = Start + Dir.RotateAngleAxis(ForkYaw, FVector::UpVector) * PlayerShotRange;
		FHitResult ForkHit;
		FCollisionQueryParams ForkParams(SCENE_QUERY_STAT(PlayerShockFork), false, this);
		const bool bForkHit = GetWorld()->SweepSingleByChannel(ForkHit, Start, ForkEnd, FQuat::Identity,
			ECC_Visibility, FCollisionShape::MakeSphere(12.f), ForkParams);
		FColor ForkColor = FColor::FromHex(TEXT("C9A6FF"));
		if (bForkHit) DealHit(ForkHit.GetActor(), ForkHit.ImpactPoint,
			PlayerShotDamage * FMath::Clamp(PlayerPrismScale, .1f, 1.f), ForkColor, ECombatDamageKind::Projectile);
		DrawDebugLine(GetWorld(), Start, bForkHit ? ForkHit.ImpactPoint : ForkEnd, ForkColor, false, .18f, 0, 2.5f);
	}
	// 玩家折射仍独立允许穿透协同，每个穿透主单位各产生一次非递归单跳。
	if (bPlayerPrism)
	{
		for (const FDirectHit& RootHit : DirectHits)
		{
			TArray<AActor*> Candidates;
			CollectAliveCombatants(GetWorld(), Candidates);
			AActor* Next = nullptr;
			float BestSquared = FMath::Square(800.f);
			for (AActor* Candidate : Candidates)
			{
				if (!IsValid(Candidate) || PayloadTargets.Contains(Candidate)) continue;
				UHealthComponent* NextHealth = Cast<ICombatant>(Candidate)->GetHealthComponent();
				if (!IsValid(NextHealth) || NextHealth->IsInvulnerable()) continue;
				const float Squared = FVector::DistSquared(RootHit.SourceCenter, Candidate->GetActorLocation());
				if (Squared <= BestSquared) { BestSquared = Squared; Next = Candidate; }
			}
			if (!Next) continue;
			const FVector Point = Next->GetActorLocation() + FVector(0, 0, 40);
			FColor Color = bShockShotUnlocked ? FColor::FromHex(TEXT("C9A6FF")) : FColor::Magenta;
			if (DealHit(Next, Point, PlayerShotDamage * FMath::Clamp(PlayerPrismScale, .1f, 1.f), Color, ECombatDamageKind::Ricochet))
			{
				DrawDebugLine(GetWorld(), RootHit.SourceCenter + FVector(0, 0, 40), Point, Color, false, .18f, 0, 2.f);
				if (UBossRelicComponent* Relics = FindComponentByClass<UBossRelicComponent>())
					Relics->NotifyElementalRayHit(this, Next, Point);
			}
		}
	}
	if (Primary.IsValid())
		if (UBossRelicComponent* Relics = FindComponentByClass<UBossRelicComponent>())
			Relics->NotifyPrimaryAttack(this, Primary.Get(), PrimaryPoint);
}
float AAICompanionDemoCharacter::ApplyElementPayload(AActor* VictimActor, float BaseDamage, FColor& InOutShotColor, const FVector& FxLocation,
	ECombatDamageKind* OutDamageKind, bool bCanSpreadFrost, AActor* ElementInstigator)
{
	// 元素弹头挂载统一入口（09-30 用户裁决"三线继承元素"——弹道形态与元素弹头正交）：主弹/散射侧弹/
	// 穿透走廊/折射跳四条命中路径全经此——冰霜/感电/印记一处挂载，反应仅在火伤入口结算
	float FinalDamage = BaseDamage;
	if (bHuntMarkUnlocked)
	{
		if (ICombatant* MarkVictim = Cast<ICombatant>(VictimActor))
		{
			MarkVictim->AddHuntMark(HuntMarkMaxStacks);
		}
	}
	AEnemyGrunt* GruntVictim = Cast<AEnemyGrunt>(VictimActor);
	if (!GruntVictim)
	{
		return FinalDamage;
	}

	// 参数与实际施加者显式传递；本体Nova也挂本体冰，但只有本体普通弹路可授传播资格。
	if (bFrostShotUnlocked)
	{
		if (OutDamageKind) *OutDamageKind = ECombatDamageKind::Frost;
		FCombatElementalDamage::ApplyFrost(GruntVictim, ElementInstigator ? ElementInstigator : this,
			FrostSlowSeconds, FrostSlowFactor, bCanSpreadFrost && BaseDamage > 0.f);
		InOutShotColor = FColor::FromHex(TEXT("9BD7FF"));
		UVfxLibrary::SpawnVfx(GetWorld(), EVfx::Frost, FxLocation, .4f, .7f);
	}

	// 感电用固定闪电标记表达残留；真正连接电弧仅在AI子弹配对两个带电活敌时生成。
	if (bShockShotUnlocked)
	{
		GruntVictim->ApplyShockResidue(5.f);
		InOutShotColor = FColor::FromHex(TEXT("C9A6FF"));
	}
	return FinalDamage;
}

