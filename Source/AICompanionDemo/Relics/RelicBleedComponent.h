/**
 * [INPUT]: ICombatant/Health唯一伤害入口、地刺遗物的真实命中与参数、ParagonGrux既有血滴粒子。
 * [OUTPUT]: 每目标一个可刷新流血状态；游戏时间DoT、不叠层、不进入主攻链；死亡/来源消失/EndPlay立即清理。
 * [POS]: Relics/地刺次生状态，独立于冰火电残留；目标Actor拥有组件，来源只用弱引用。
 * [PROTOCOL]: 变更时更新此头部；成员地图由主agent统一维护。
 */
#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RelicBleedComponent.generated.h"

class UHealthComponent;
class UParticleSystem;
class UParticleSystemComponent;

UCLASS(ClassGroup=(Custom))
class AICOMPANIONDEMO_API URelicBleedComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	URelicBleedComponent();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
	/** 只对活ICombatant创建/刷新；刷新剩余时长，不叠加伤害、不重置下一拍。 */
	static URelicBleedComponent* ApplyTo(AActor* Target, AActor* Source, float Duration, float Damage, float Interval);
	void StopBleeding();
	UFUNCTION(BlueprintPure, Category="Debug|BossRelics") FString DebugBleedState() const;
	UFUNCTION(BlueprintPure, Category="BossRelics") float GetBleedRemaining() const { return Remaining; }

private:
	struct FBloodVisual { TWeakObjectPtr<UParticleSystemComponent> Component; float Remaining = 1.f; };
	UPROPERTY(Transient) TObjectPtr<UHealthComponent> TargetHealth;
	UPROPERTY(Transient) TObjectPtr<UParticleSystem> BloodSystem;
	TWeakObjectPtr<AActor> DamageSource;
	TArray<FBloodVisual> BloodVisuals;
	float Remaining = 0.f;
	float DamagePerTick = 0.f;
	float TickInterval = .5f;
	float Accumulated = 0.f;
	float TotalDamage = 0.f;
	int32 DamageTicks = 0;
	int32 RefreshCount = 0;
	bool bActive = false;
	bool SourceAlive() const;
	void Refresh(AActor* Source, float Duration, float Damage, float Interval);
	void SpawnBlood();
	void TickBloodVisuals(float DeltaTime);
	UFUNCTION() void HandleTargetDeath(UHealthComponent* Health);
};
