/**
 * [INPUT]: SFXLibrary解析的两类妖术师短声、玩家Pawn距离、ICombatant存活契约和世界暂停/生命周期。
 * [OUTPUT]: 每世界两个空间组件预算与成功起播间隔；玩家8至24米距离音量/原位空间方向，暂停冻结/死亡清理与只读探针。
 * [POS]: Audio仅妖术师两声的生命周期所有者；不接语音/BGM，不修改技能时序或伤害。
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */
#pragma once

#include "CoreMinimal.h"
#include "Audio/SFXLibrary.h"
#include "Subsystems/WorldSubsystem.h"
#include "WarlockAudioSubsystem.generated.h"

class USoundAttenuation;

USTRUCT()
struct FWarlockSoundInstance
{
	GENERATED_BODY()
	UPROPERTY(Transient) TObjectPtr<UAudioComponent> Audio;
	UPROPERTY(Transient) TWeakObjectPtr<AActor> Source;
	UPROPERTY(Transient) EGameSFX Event = EGameSFX::None;
	float BaseVolume = 0.f;
};

UCLASS()
class AICOMPANIONDEMO_API UWarlockAudioSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()
public:
	bool Play(USoundBase* Sound, EGameSFX Event, float Volume, float Pitch, AActor* Source, const FVector& Location);
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickableWhenPaused() const override { return true; }
	virtual void Deinitialize() override;
	UFUNCTION(BlueprintPure, Category = "Debug|Audio") FString DebugWarlockAudioState() const;
	UFUNCTION(BlueprintCallable, Category = "Debug|Audio") bool DebugTryWarlockSFX(AActor* Source, bool bFire);
protected:
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
private:
	void PruneActive();
	bool IsFrozen() const;
	UPROPERTY(Transient) TArray<FWarlockSoundInstance> Active;
	UPROPERTY(Transient) TObjectPtr<USoundAttenuation> Attenuation;
	double ChargeReadyAt = 0.0;
	double FireReadyAt = 0.0;
	int64 RequestedCount = 0;
	int64 StartedComponentCount = 0;
	int64 RejectedFrozenCount = 0;
	int64 RejectedBudgetCount = 0;
	int64 RejectedCooldownCount = 0;
	int64 RejectedSourceCount = 0;
	int64 RejectedDistanceCount = 0;
	int64 RejectedListenerCount = 0;
	int64 FailedSpawnCount = 0;
	int64 FinishedComponentCount = 0;
	int64 StoppedSourceCount = 0;
	int32 MaxObservedComponents = 0;
};
