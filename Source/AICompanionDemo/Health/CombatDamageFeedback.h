/**
 * [INPUT]: CoreMinimal的世界坐标；业务命中路径提供语义，Health提供实际扣血与来源快照。
 * [OUTPUT]: 伤害类别、玩家/敌方/队友护卫或歼灭来源，以及类别/落点/猎印层数的小上下文。
 * [POS]: Health与现有UI数值层共享的纯数据契约；不结算伤害、不持Actor、不保存第二份战斗状态。
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */
#pragma once

#include "CoreMinimal.h"

enum class ECombatDamageKind : uint8
{
	Generic, Projectile, Frost, Melt, Explosion, Shock, Execute, Ricochet,
	Radiance, Pulse, Spike, Seismic, Reflect, Bleed, Burn, Fireball, Beam, Melee, MobFeedback
};

enum class ECombatDamageSource : uint8 { Unknown, Player, CompanionGuard, CompanionBerserk, Hostile };

struct FCombatDamageContext
{
	ECombatDamageKind Kind = ECombatDamageKind::Generic;
	FVector Location = FVector::ZeroVector;
	int32 MarkCount = 0;
};
