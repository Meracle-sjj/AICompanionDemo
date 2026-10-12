/**
 * [INPUT]: CombatElementalDamage契约、ICombatant生命口、EnemyGrunt双冰钟、Pawn/Owner/Instigator真实归属。
 * [OUTPUT]: 共用Pawn/Owner/Instigator解析的分来源冰挂载，以及原火/异人融化独立两笔；失败/免伤/致死原火不消费冰或追加尸体伤害。
 * [POS]: 原弹路/Nova/领域/遗物/友方火球共用的来源与反应器；借用参数不借用施加者，同raw同instigator交Health，不再次乘算。
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */
#include "CombatElementalDamage.h"
#include "AI/Combatant.h"
#include "AI/CompanionAIController.h"
#include "AI/EnemyGrunt.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"
#include "Health/HealthComponent.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	enum class EFireOwner : uint8 { Unknown, Player, Companion };
	EFireOwner ResolveFireOwner(AActor* Source, UWorld* World)
	{
		const APawn* Player = UGameplayStatics::GetPlayerPawn(World, 0);
		for (int32 Depth = 0; IsValid(Source) && Depth < 8; ++Depth)
		{
			if (Cast<ACompanionAIController>(Source)) return EFireOwner::Companion;
			if (const APawn* Pawn = Cast<APawn>(Source))
				if (Cast<ACompanionAIController>(Pawn->GetController())) return EFireOwner::Companion;
			if (const APawn* Pawn = Source->GetInstigator())
				if (Cast<ACompanionAIController>(Pawn->GetController())) return EFireOwner::Companion;
			if (Player && (Source == Player || Source->GetInstigator() == Player)) return EFireOwner::Player;
			Source = Source->GetOwner();
		}
		return EFireOwner::Unknown;
	}
}

void FCombatElementalDamage::ApplyFrost(AActor* Target, AActor* Instigator, float Duration,
	float SpeedFactor, bool bActualPlayerShot)
{
	AEnemyGrunt* Grunt = IsValid(Target) ? Cast<AEnemyGrunt>(Target) : nullptr;
	if (!Grunt) return;
	const EFireOwner Owner = ResolveFireOwner(Instigator, Target->GetWorld());
	if (Owner == EFireOwner::Player)
	{
		AActor* SpreadSource = bActualPlayerShot ? UGameplayStatics::GetPlayerPawn(Target->GetWorld(), 0) : nullptr;
		Grunt->ApplyPlayerFrost(Duration, SpeedFactor, SpreadSource);
	}
	else if (Owner == EFireOwner::Companion)
		Grunt->ApplyCompanionFrost(Duration, SpeedFactor);
}

float FCombatElementalDamage::ApplyFire(AActor* Target, AActor* Instigator, float RawDamage,
	ECombatDamageKind FireKind, const FVector& Location, bool* OutMelt)
{
	if (OutMelt) *OutMelt = false;
	ICombatant* Combatant = IsValid(Target) ? Cast<ICombatant>(Target) : nullptr;
	UHealthComponent* Health = Combatant ? Combatant->GetHealthComponent() : nullptr;
	if (!Combatant || Combatant->IsDead() || !IsValid(Health) || !FMath::IsFinite(RawDamage) || RawDamage <= 0.f) return 0.f;
	const float Before = Health->GetCurrentHealth();
	Health->ApplyCombatDamage(RawDamage, Instigator, {FireKind, Location});
	const float ActualFire = IsValid(Health) ? FMath::Max(Before - Health->GetCurrentHealth(), 0.f) : 0.f;
	// 致死火伤只保留第一笔；死亡委托可能销毁目标，所有后续访问重新核真实存活。
	if (ActualFire <= 0.f || !IsValid(Target) || !IsValid(Health) || Health->IsDead()
		|| Health->GetCurrentHealth() <= 0.f || Health->IsInvulnerable()) return ActualFire;
	AEnemyGrunt* Grunt = Cast<AEnemyGrunt>(Target);
	if (!Grunt) return ActualFire; // Boss保持原无冰残留/控制免疫适用面。
	const bool bFireKind = FireKind == ECombatDamageKind::Burn || FireKind == ECombatDamageKind::Fireball
		|| FireKind == ECombatDamageKind::Explosion || FireKind == ECombatDamageKind::Radiance;
	if (!bFireKind) return ActualFire;
	const EFireOwner Owner = ResolveFireOwner(Instigator, Target->GetWorld());
	const bool bConsumePlayer = Owner == EFireOwner::Companion && Grunt->GetPlayerFrostResidue() > 0.f;
	const bool bConsumeCompanion = Owner == EFireOwner::Player && Grunt->GetCompanionFrostResidue() > 0.f;
	if (!bConsumePlayer && !bConsumeCompanion) return ActualFire;
	// 先消费相反来源，避免同步反伤/回调重入再次使用同一冰；同源冰及其传播资格保持。
	Grunt->ConsumeFrostResidue(bConsumePlayer);
	const float BeforeMelt = Health->GetCurrentHealth();
	Health->ApplyCombatDamage(RawDamage, Instigator, {ECombatDamageKind::Melt, Location});
	if (OutMelt) *OutMelt = IsValid(Health) && BeforeMelt > Health->GetCurrentHealth();
	return ActualFire;
}
