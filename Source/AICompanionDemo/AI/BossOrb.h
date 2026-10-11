/**
 * [INPUT]: 依赖 Engine 的 AActor/StaticMeshComponent（承载碰撞体）/PointLightComponent（弹幕自发光）、Niagara 的 SpawnSystemAttached（火焰弹体，09-30 用户裁决"外观要像一团火"）、Vfx/VfxLibrary 的 Fire 命中特效、HealthComponent 的玩家伤害口、Kismet 的 GetPlayerPawn
 * [OUTPUT]: 召唤师火球以可见35cm橙核为连续三维球扫体积；直接扫玩家实际胶囊，与BlockPawn地形/实体按首次接触排序，地形优先且立即销毁；Launch三维瞄准、6s/宿主/玩家死亡清理，保留同栈反伤防护。
 *  LaunchFriendly保留步行直线球；LaunchFriendlyHoming供闪烁三球持续追踪活敌、无敌时等窗口，直击/范围伤互斥；爆炸由遗物登记，暂停/死亡取消伤害。
 * [POS]: AI/ 的 Boss 弹幕件（Boss 家族攻击扩展）：召唤师护盾/眩晕两态的持续压制手段——可躲=压迫而不失公平
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BossOrb.generated.h"

class UStaticMeshComponent;
class UPointLightComponent;
class UNiagaraSystem;

UCLASS()
class AICOMPANIONDEMO_API ABossOrb : public AActor
{
	GENERATED_BODY()

public:
	ABossOrb();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** 发射初始化：方向单位向量 + 伤害（召唤师发射时调用） */
	void Launch(const FVector& Direction, float InDamage, float Speed = 800.f);
	UFUNCTION(BlueprintPure, Category = "Debug|Boss")
	FString DebugOrbCombatState() const;
	UFUNCTION(BlueprintPure, Category = "Debug|Boss")
	FString DebugFriendlyHomingState() const;
	/** 编辑器隔离专项：重新发射已登记的真实弹体；非编辑器无效。 */
	UFUNCTION(BlueprintCallable, Category = "Debug|Boss")
	void DebugLaunchOrb(const FVector& Direction, float InDamage, float Speed);
	/** 遗物友方火球：复用同一弹体，静态遮挡/连续敌人命中与范围爆炸。 */
	void LaunchFriendly(const FVector& Direction, float InDamage, float ExplosionRadius, AActor* DamageInstigator);
	/** 闪烁遗物追踪球：直击与周围爆炸伤害逐目标互斥；目标死亡后重选。 */
	void LaunchFriendlyHoming(AActor* Target, float DirectDamage, float SplashDamage, float ExplosionRadius, AActor* DamageInstigator);

private:
	/** 弹体挂点（碰撞/光源/火焰的承载根——视觉本体是 Niagara 火焰，mesh 隐藏） */
	UPROPERTY(VisibleAnywhere, Category = "Orb")
	TObjectPtr<class UStaticMeshComponent> BodyMesh;
	/** 明确的命中核心；火焰只作外围装饰，半径与球扫完全一致。友方维持原外观。 */
	UPROPERTY(VisibleAnywhere, Category = "Orb")
	TObjectPtr<UStaticMeshComponent> HitCore;
	static constexpr float HitRadius = 35.f;

	/** 弹幕自发光：火光跟随（60m 外的可读性杠杆——纯特效在日照沙丘上不可见） */
	UPROPERTY(VisibleAnywhere, Category = "Orb")
	TObjectPtr<class UPointLightComponent> PointLight;

	/** 火焰弹体模板（09-30 用户裁决"外观像一团火"——Fire_EXP 包循环火焰，BeginPlay 挂载） */
	UPROPERTY(Transient)
	TObjectPtr<class UNiagaraSystem> FireTemplate;

	FVector Velocity = FVector::ZeroVector;
	float Damage = 12.f;
	float LifeSeconds = 6.f;
	bool bFriendly = false;
	bool bFriendlyHoming = false;
	float FriendlyExplosionRadius = 250.f;
	float FriendlySplashDamage = 20.f;
	TWeakObjectPtr<AActor> FriendlyInstigator;
	TWeakObjectPtr<AActor> FriendlyTarget;
	FVector FriendlyTargetLastLocation = FVector::ZeroVector;
	bool bHasFriendlyTargetLocation = false;
	float FriendlyHomingSpeed = 1200.f;
	void TickFriendly(float DeltaSeconds);
	void TickFriendlyHoming(float DeltaSeconds, AActor* Source);
	void ExplodeFriendlyHoming(AActor* Victim, AActor* Source);
	void SpawnFriendlyExplosionFx(AActor* Source);
};
