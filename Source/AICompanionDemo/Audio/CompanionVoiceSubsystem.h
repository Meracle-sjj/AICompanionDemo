/**
 * [INPUT]: 世界生命周期、SFXLibrary固定录音字幕/槽位和UAudioComponent真实播放状态。
 * [OUTPUT]: Boss不可抢占、生命可抢普通、同级普通FIFO的三级队列；Boss/生命跨奖励冻结，真实暂停全停；起播预算/记录与前三成功箱计数。
 * [POS]: Audio/语音排程唯一所有者；仅Game/PIE，UPROPERTY保活声源/组件，Deinitialize停播清局。
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */
#pragma once

#include "CoreMinimal.h"
#include "Audio/SFXLibrary.h"
#include "Events/RunEventLog.h"
#include "Subsystems/WorldSubsystem.h"
#include "CompanionVoiceSubsystem.generated.h"

class UAudioComponent;
class USoundBase;
class USoundAttenuation;

USTRUCT()
struct FQueuedCompanionVoice
{
	GENERATED_BODY()
	UPROPERTY() TObjectPtr<USoundBase> Sound;
	UPROPERTY() EGameSFX Event = EGameSFX::None;
	UPROPERTY() FVector Location = FVector::ZeroVector;
	UPROPERTY() float Volume = 1.f;
	UPROPERTY() bool bSpatial = false;
};

UCLASS()
class AICOMPANIONDEMO_API UCompanionVoiceSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()
public:
	bool Enqueue(USoundBase* Sound, EGameSFX Event, float Volume, const FVector& Location, bool bSpatial);
	EGameSFX RegisterChestOpened();
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickableWhenPaused() const override { return true; }
	virtual void Deinitialize() override;
	UFUNCTION(BlueprintPure, Category = "Debug|Voice") FString DebugVoiceState() const;
	const FString& GetActiveLine() const { return ActiveLine; }
	ERunEventKind GetActiveKind() const { return ActiveKind; }
protected:
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
private:
	bool CanPlayVoice(EGameSFX Event) const;
	void StartNext();
	void ClearActive();
	UPROPERTY() TArray<FQueuedCompanionVoice> Pending;
	UPROPERTY() TObjectPtr<UAudioComponent> ActiveAudio;
	UPROPERTY() TObjectPtr<USoundAttenuation> Attenuation;
	FString ActiveLine;
	EGameSFX ActiveEvent = EGameSFX::None;
	ERunEventKind ActiveKind = ERunEventKind::General;
	bool bAudioPaused = false;
	int64 StartedCount = 0;
	int64 CompletedCount = 0;
	int64 InterruptedCount = 0;
	int64 DiscardedPendingCount = 0;
	int64 BudgetSkippedCount = 0;
	int32 SuccessfulChestCount = 0;
	TMap<EGameSFX, int32> PlayedByEvent;
};
