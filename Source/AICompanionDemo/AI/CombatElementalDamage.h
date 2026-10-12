/**
 * [INPUT]: 活ICombatant/Health、EnemyGrunt分来源冰状态及真实玩家/队友归属。
 * [OUTPUT]: FCombatElementalDamage::ApplyFrost/ApplyFire共用实际归属解析；冰参数与施加者分开，火成功非致死后消费异源冰并另结同raw的Melt。
 * [POS]: AI跨元素路径的来源与反应入口；不保存血量/计时、不改火伤成长，不负责飘字或视觉。
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */
#pragma once
#include "CoreMinimal.h"
#include "Health/CombatDamageFeedback.h"

class AActor;

struct AICOMPANIONDEMO_API FCombatElementalDamage
{
	// 参数可继承玩家，冰来源只认实际施加者；仅真实玩家弹路可授传播资格。
	static void ApplyFrost(AActor* Target, AActor* Instigator, float Duration,
		float SpeedFactor, bool bActualPlayerShot = false);

	// 返回原火实际扣血；OutMelt仅表示第二笔确实扣血，模式/减伤均由Health每笔结算一次。
	static float ApplyFire(AActor* Target, AActor* Instigator, float RawDamage,
		ECombatDamageKind FireKind, const FVector& Location, bool* OutMelt = nullptr);
};
