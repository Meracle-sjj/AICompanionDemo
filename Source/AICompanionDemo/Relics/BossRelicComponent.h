/**
 * [INPUT]: ActorComponent/HealthComponent；主攻、队友直击、真实受击与成功闪烁由共享宿主接线。
 * [OUTPUT]: 九遗物参数/效果与只读JSON；步行火球与闪烁落点三颗追踪球，直击30/邻敌20互斥；地刺50与每跳10可刷新流血。
 * [POS]: Relics/唯一玩法所有者；次生只写Health，死亡/EndPlay幂等销毁球/火圈/石刺及两种临时VFX，不延长正常寿命。
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "BossRelicTypes.h"
#include "BossRelicComponent.generated.h"

class UHealthComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class UNiagaraComponent;
class ABossOrb;
class URelicBleedComponent;

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class AICOMPANIONDEMO_API UBossRelicComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UBossRelicComponent();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UFUNCTION(BlueprintCallable, Category="BossRelics") bool AcquireRelic(EBossRelicId Id);
	UFUNCTION(BlueprintPure, Category="BossRelics") bool HasRelic(EBossRelicId Id) const;
	/** 每次玩家主攻首个真实命中最多调用一次；穿透、散射、折射及次生效果不得再次调用。 */
	void NotifyPrimaryAttack(AActor* Attacker, AActor* PrimaryTarget, const FVector& ImpactPoint);
	/** 闪现轰击真实命中的独立概率地刺口；调用者每目标只调用一次，不触发主攻震击/熔核爆弹。 */
	void NotifyElementalRayHit(AActor* Attacker, AActor* Target, const FVector& ImpactPoint);
	/** 编辑器专项入口复用真实主攻分支；非编辑器不执行。 */
	UFUNCTION(BlueprintCallable, Category="Debug|BossRelics") void DebugPrimaryAttack(AActor* PrimaryTarget, const FVector& ImpactPoint);
	/** 只传本次队友直击实际扣血，元素/处决/爆炸的次生伤害不得计入。 */
	void NotifyCompanionHit(AActor* CompanionPawn, float ActualDamage);
	float ModifyIncomingDamage(float Damage) const;
	void NotifyPlayerDamaged(AActor* Source, float ActualDamage);
	void NotifyBlink(const FVector& Start, const FVector& End);
	float GetPotionFrenzyDurationMultiplier() const;
	/** 接管临时火浪等Actor的提前清理；保持原LifeSpan，死后迟到登记立即销毁。 */
	void TrackTransientVfx(AActor* Fx);
	/** 友方球爆闪自有硬寿命，玩家死亡/EndPlay也提前清理。 */
	void TrackTransientNiagara(UNiagaraComponent* Fx, float Life);
	UFUNCTION(BlueprintPure, Category="BossRelics") FString DescribeRelic(EBossRelicId Id) const;
	UFUNCTION(BlueprintPure, Category="BossRelics") FString DebugState() const;
	UFUNCTION(BlueprintPure, Category="BossRelics") bool IsFlameWardActive() const;
	/** 周期计时只读Clock/NextFrenzyAt；持续时间读队友实际FrenzyTimer，药剂延长也实时反映。 */
	UFUNCTION(BlueprintPure, Category="BossRelics") float GetSpiritFrenzyCooldownRemaining() const;
	UFUNCTION(BlueprintPure, Category="BossRelics") float GetSpiritFrenzyCooldownTotal() const;
	UFUNCTION(BlueprintPure, Category="BossRelics") float GetSpiritFrenzyActiveRemaining() const;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="BossRelics") int32 OwnedMask = 0;
	UPROPERTY(EditAnywhere, Category="BossRelics|BloodLink", meta=(ClampMin="0")) float BloodLinkRange = 800.f;
	UPROPERTY(EditAnywhere, Category="BossRelics|BloodLink", meta=(ClampMin="0",ClampMax="1")) float BloodLinkHealingRatio = .25f;
	UPROPERTY(EditAnywhere, Category="BossRelics|SpiritFrenzy", meta=(ClampMin="0.1")) float FrenzyInterval = 20.f;
	UPROPERTY(EditAnywhere, Category="BossRelics|SpiritFrenzy", meta=(ClampMin="0.1")) float FrenzyDuration = 5.f;
	UPROPERTY(EditAnywhere, Category="BossRelics|SpiritFrenzy", meta=(ClampMin="1")) float PotionFrenzyMultiplier = 1.5f;
	UPROPERTY(EditAnywhere, Category="BossRelics|MovingFireball", meta=(ClampMin="1")) float FireballTravelDistance = 1500.f;
	UPROPERTY(EditAnywhere, Category="BossRelics|MovingFireball", meta=(ClampMin="0")) float FireballDamage = 30.f;
	UPROPERTY(EditAnywhere, Category="BossRelics|MovingFireball", meta=(ClampMin="1")) float FireballRadius = 300.f;
	UPROPERTY(EditAnywhere, Category="BossRelics|MovingFireball", meta=(ClampMin="0")) float BlinkFireballSplashDamage = 20.f;
	UPROPERTY(EditAnywhere, Category="BossRelics|StoneSpikes", meta=(ClampMin="0",ClampMax="1")) float SpikeChance = .2f;
	UPROPERTY(EditAnywhere, Category="BossRelics|StoneSpikes", meta=(ClampMin="0")) float SpikeDamage = 50.f;
	UPROPERTY(EditAnywhere, Category="BossRelics|StoneSpikes", meta=(ClampMin="1")) float SpikeRadius = 180.f;
	UPROPERTY(EditAnywhere, Category="BossRelics|StoneSpikes", meta=(ClampMin="0.1")) float SpikeSlowDuration = 2.f;
	UPROPERTY(EditAnywhere, Category="BossRelics|StoneSpikes", meta=(ClampMin="0.05",ClampMax="1")) float SpikeSpeedFactor = .6f;
	UPROPERTY(EditAnywhere, Category="BossRelics|StoneSpikes", meta=(ClampMin="0.1")) float SpikeBleedDuration = 5.f;
	UPROPERTY(EditAnywhere, Category="BossRelics|StoneSpikes", meta=(ClampMin="0")) float SpikeBleedDamage = 10.f;
	UPROPERTY(EditAnywhere, Category="BossRelics|StoneSpikes", meta=(ClampMin="0.1")) float SpikeBleedInterval = .5f;
	UPROPERTY(EditAnywhere, Category="BossRelics|SeismicStrike", meta=(ClampMin="0.1")) float SeismicInterval = 6.f;
	UPROPERTY(EditAnywhere, Category="BossRelics|SeismicStrike", meta=(ClampMin="0")) float SeismicDamage = 35.f;
	UPROPERTY(EditAnywhere, Category="BossRelics|SeismicStrike", meta=(ClampMin="1")) float SeismicRadius = 450.f;
	UPROPERTY(EditAnywhere, Category="BossRelics|SeismicStrike", meta=(ClampMin="0")) float SeismicUpSpeed = 480.f;
	UPROPERTY(EditAnywhere, Category="BossRelics|StoneCarapace", meta=(ClampMin="0",ClampMax="0.95")) float CarapaceReduction = .15f;
	UPROPERTY(EditAnywhere, Category="BossRelics|StoneCarapace", meta=(ClampMin="0")) float ThornsDamage = 10.f;
	UPROPERTY(EditAnywhere, Category="BossRelics|VolcanicRounds", meta=(ClampMin="0")) float VolcanicDamage = 12.f;
	UPROPERTY(EditAnywhere, Category="BossRelics|VolcanicRounds", meta=(ClampMin="1")) float VolcanicRadius = 250.f;
	UPROPERTY(EditAnywhere, Category="BossRelics|FlameWard", meta=(ClampMin="0.1")) float WardInterval = 10.f;
	UPROPERTY(EditAnywhere, Category="BossRelics|FlameWard", meta=(ClampMin="0.1")) float WardDuration = 4.f;
	UPROPERTY(EditAnywhere, Category="BossRelics|FlameWard", meta=(ClampMin="0",ClampMax="0.95")) float WardReduction = .2f;
	UPROPERTY(EditAnywhere, Category="BossRelics|FlameWard", meta=(ClampMin="1")) float WardRadius = 400.f;
	UPROPERTY(EditAnywhere, Category="BossRelics|FlameWard", meta=(ClampMin="0")) float WardDamagePerTick = 3.f;
	UPROPERTY(EditAnywhere, Category="BossRelics|FlameWard", meta=(ClampMin="0.1")) float WardDamageInterval = .5f;
	UPROPERTY(EditAnywhere, Category="BossRelics|BlinkPyre", meta=(ClampMin="1")) float PyreRadius = 300.f;
	UPROPERTY(EditAnywhere, Category="BossRelics|BlinkPyre", meta=(ClampMin="0.1")) float PyreDuration = 4.f;
	UPROPERTY(EditAnywhere, Category="BossRelics|BlinkPyre", meta=(ClampMin="0")) float PyreDamagePerTick = 4.f;
	UPROPERTY(EditAnywhere, Category="BossRelics|BlinkPyre", meta=(ClampMin="0.1")) float PyreDamageInterval = .5f;

private:
	struct FPyre
	{
		FVector Center;
		TArray<FVector> Ring;
		double EndsAt = 0;
		double NextDamageAt = 0;
		TWeakObjectPtr<AActor> Vfx;
	};
	struct FRelicSpike
	{
		TWeakObjectPtr<UStaticMeshComponent> Mesh;
		FVector Ground;
		float Age = 0.f;
	};
	UPROPERTY(Transient) TObjectPtr<UHealthComponent> OwnerHealth;
	UPROPERTY(Transient) TObjectPtr<UStaticMesh> SpikeMesh;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> SpikeMaterial;
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> SpikeComponents;
	TArray<FRelicSpike> Spikes;
	TArray<FPyre> Pyres;
	TArray<TWeakObjectPtr<ABossOrb>> FriendlyOrbs;
	TArray<TWeakObjectPtr<AActor>> TransientVfx;
	TArray<TWeakObjectPtr<UNiagaraComponent>> TransientNiagaraVfx;
	TArray<TWeakObjectPtr<URelicBleedComponent>> AppliedBleeds;
	FVector PreviousLocation = FVector::ZeroVector;
	double Clock = 0;
	double NextFrenzyAt = 0;
	double NextWardAt = 0;
	double WardEndsAt = 0;
	double NextWardDamageAt = 0;
	double SeismicReadyAt = 0;
	float TravelAccumulated = 0.f;
	float LatestBloodDamage = 0.f;
	float LatestHealAttempted = 0.f;
	float LatestHealActual = 0.f;
	int32 FireballsSpawned = 0;
	int32 BlinkFireballsSpawned = 0;
	int32 FrenzyActivations = 0;
	int32 WardActivations = 0;
	int32 WardDamageTicks = 0;
	int32 PyreDamageTicks = 0;
	int32 SpikeActivations = 0;
	int32 SeismicActivations = 0;
	int32 VolcanicActivations = 0;
	int32 ThornsActivations = 0;
	int32 MeltReactions = 0;
	bool bProcessingEffect = false;
	bool bStopped = false;

	bool OwnerAlive() const;
	bool CanTrigger() const;
	UFUNCTION() void HandleOwnerDeath(UHealthComponent* Health);
	void ClearRuntime();
	void TickPeriodic();
	void TickWalking(float DeltaTime);
	void TickPyres();
	void TickSpikes(float DeltaTime);
	void SpawnMovingFireball();
	void SpawnBlinkFireballs(const FVector& Landing);
	void RegisterFriendlyOrb(ABossOrb* Orb);
	void ActivatePeriodicFrenzy();
	void SpawnPyre(const FVector& Location);
	void SpawnSpikeVisuals(const FVector& Location);
	void TryStoneSpikes(const FVector& ImpactPoint);
	void Blast(const FVector& Center, float Radius, float Damage, bool bKnockUp);
	void FireAreaDamage(const FVector& Center, float Radius, float Damage);
	float DamageEnemy(AActor* Target, float Damage);
	FVector GroundPoint(const FVector& Location) const;
	void CacheGroundRing(const FVector& Center, float Radius, TArray<FVector>& Out) const;
	void DrawRing(const TArray<FVector>& Ring, const FColor& Color, float Life, float Width) const;
	void SpawnTrackedVfx(uint8 Kind, const FVector& Point, float Life, float Scale);
};
