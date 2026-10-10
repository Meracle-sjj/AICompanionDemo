/**
 * [INPUT]: 依赖 Engine 的 Actor/Tick 与 Kismet 的 GetPlayerPawn、本模块 HealthComponent（自身血量 + 玩家血量查询）、AIModule 的 AIPerceptionStimuliSource（视觉刺激源）
 * [OUTPUT]: 对外提供 AEnemyTarget——M3 猎手敌人（球体）：逼近玩家 + 近身伤害节拍（威胁维度的原料来源）+ IsEngagingPlayer 仇恨信号；实现 ICombatant 契约（M3.5 起与假人 EnemyGrunt 同构，供队友评分器跨形体识别）；保留血量变色/死亡自毁/感知源三件套
 * [POS]: AI/ 的敌对方实体，与 CompanionCharacter 平行（互不引用，交互全走 Health/Perception 通用链路）；移动走直移不寻路（开阔灰盒 KISS，绕障需求出现再上 MoveTo——YAGNI）
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AI/Combatant.h"
#include "EnemyTarget.generated.h"

class UHealthComponent;
class UStaticMeshComponent;
class UAIPerceptionStimuliSourceComponent;
class UMaterialInstanceDynamic;

UCLASS()
class AICOMPANIONDEMO_API AEnemyTarget : public AActor, public ICombatant
{
	GENERATED_BODY()

public:
	AEnemyTarget();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** 观感调试入口：屏幕行与弹道线需要唯一标识目标（GetNameSafe） */
	UFUNCTION(BlueprintPure, Category = "Enemy")
	virtual UHealthComponent* GetHealthComponent() const override { return HealthComponent; }

	/** 死亡转发：目标是否已退出战斗（调用方无需触达 HealthComponent 内部） */
	UFUNCTION(BlueprintPure, Category = "Enemy")
	virtual bool IsDead() const override;

	/** 仇恨信号：正在贴脸攻击玩家（距离是潜在威胁，咬人才是现实威胁——队友威胁分覆盖拉满的依据） */
	UFUNCTION(BlueprintPure, Category = "Enemy")
	virtual bool IsEngagingPlayer() const override;

	/** 逼近速度（厘米/秒）：M3 评分"威胁分"的行为来源 */
	UPROPERTY(EditAnywhere, Category = "Enemy|Hunt", meta = (ClampMin = "0.0"))
	float MoveSpeed = 200.f;

	/** 对玩家的近身伤害：攻击节拍单发数值（默认 5×3 敌/1.5s 间隔 ≈ 10 dps——站桩约 30s 倒地，跑动可无限风筝：观察者玩法的张力线） */
	UPROPERTY(EditAnywhere, Category = "Enemy|Hunt", meta = (ClampMin = "1.0"))
	float AttackDamageToPlayer = 5.f;

	/** 近身攻击触发距离（厘米）：进入即停步开打 */
	UPROPERTY(EditAnywhere, Category = "Enemy|Hunt", meta = (ClampMin = "50.0"))
	float PlayerEngageRange = 160.f;

	/** 对玩家的攻击间隔（秒） */
	UPROPERTY(EditAnywhere, Category = "Enemy|Hunt", meta = (ClampMin = "0.1"))
	float PlayerAttackInterval = 1.5f;

protected:
	/** 逼近/攻击一体节拍：超距直移逼近，贴脸按节拍对玩家 ApplyDamage（玩家死亡/自身死亡即停手） */
	void HuntPlayer(float DeltaSeconds);

private:
	/** 贴地校准：向下 trace 地面并把 Z 抬到"球底离地 2cm"——直移体无重力，Z 全程自管（BeginPlay 首帧落地 + HuntPlayer 每帧跟地共用） */
	void SnapToGround(FVector& InOutLocation) const;
	/** 受击回调：按血量百分比把球体从敌对红渐变到濒死暗灰 */
	UFUNCTION()
	void HandleHealthChanged(UHealthComponent* HealthComp, float NewHealth, float Delta);

	/** 死亡回调：变灰定版，短暂停留（给玩家视觉确认）后自毁——"退出目标集合"的最小实现 */
	UFUNCTION()
	void HandleDeath(UHealthComponent* HealthComp);

	/** 根=碰撞球（队友视线 LineTrace 与感知系统的命中体） */
	UPROPERTY(VisibleAnywhere, Category = "Enemy")
	TObjectPtr<UStaticMeshComponent> BodyMesh;

	UPROPERTY(VisibleAnywhere, Category = "Enemy")
	TObjectPtr<UHealthComponent> HealthComponent;

	/** 向队友的 AIPerception 注册自己为视觉刺激源（R6：感知走引擎，不自研） */
	UPROPERTY(VisibleAnywhere, Category = "Enemy")
	TObjectPtr<UAIPerceptionStimuliSourceComponent> StimuliSource;

	/** 运行时创建的 MID：血量变色全走它（基础形状材质的 BaseColor 参数） */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BodyMaterial;

	/** 对玩家的攻击冷却累计器（Tick 驱动，节拍与 PlayerAttackInterval 比较） */
	float PlayerAttackCooldown = 0.f;
};
