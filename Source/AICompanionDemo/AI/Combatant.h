/**
 * [INPUT]: 依赖 EngineUtils 的 TActorIterator（活敌收集器的全场遍历）
 * [OUTPUT]: ICombatant存活/威胁/健康与可选猎印Add/Get/Consume契约；普通敌人与Boss共享玩家叠层及队友处决出口；CollectAliveCombatants为活敌单点真源
 * [POS]: AI/跨形体战斗契约；不支持猎印的旧训练目标默认不叠层，BossBase家族与EnemyGrunt明确实现，伤害仍归HealthComponent
 * [PROTOCOL]: 变更时更新此头部，然后检查../AGENTS.md
 */

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "EngineUtils.h"
#include "Combatant.generated.h"

class UHealthComponent;

/** 战斗单位接口标记（纯 C++：蓝图不可实现——评分器是 C++ 决策面，YAGNI） */
UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UCombatant : public UInterface
{
	GENERATED_BODY()
};

class AICOMPANIONDEMO_API ICombatant
{
	GENERATED_BODY()

public:
	/** 死亡判定：评分器跳过阵亡候选、火力层拒绝补枪 */
	virtual bool IsDead() const = 0;

	/** 仇恨信号：正在贴脸攻击玩家——威胁分仇恨覆盖拉满的依据 */
	virtual bool IsEngagingPlayer() const = 0;

	/** 血量真源：残血分原料 + 火力层伤害出口 */
	virtual UHealthComponent* GetHealthComponent() const = 0;

	/** 可选猎印能力：状态由宿主保存，玩家弹叠层、队友直击消费；不绑定具体敌人类型。 */
	virtual void AddHuntMark(int32 /*MaxStacks*/) { }
	virtual int32 GetHuntMarks() const { return 0; }
	virtual int32 ConsumeHuntMarks() { return 0; }
};

/**
 * 活敌收集器：火力结算层的"敌人"单点真源（09-30 召唤师案定罪产物）。
 * 决策层（评分器）早已面向 ICombatant 契约——Boss 也能被选中集火；但走廊命中/溅射/
 * 折射弹跳/玩家自动开火目标曾用 TActorIterator<AEnemyGrunt> 类型扫描，"敌人"存在两套
 * 定义：决策层认接口、火力层认类型——召唤师（ABossBase 系）被排除在命中集外，弹道
 * 穿体而过、无特效、不掉血（胶囊 Visibility 修复无效的真相：那条命中路径不查物理世界）。
 * 此处按契约收集：ICombatant 实现者且存活即入集——玩家/队友非 ICombatant 天然豁免
 * （走廊友伤防线原样保留），新敌形体（含 Boss 家族）自动进入火力链，永绝后患。
 */
inline void CollectAliveCombatants(UWorld* World, TArray<AActor*>& OutCombatants)
{
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		const ICombatant* Combatant = Cast<ICombatant>(Actor);
		if (Actor && Combatant && !Combatant->IsDead())
		{
			OutCombatants.Add(Actor);
		}
	}
}
