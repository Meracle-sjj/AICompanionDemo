/**
 * [INPUT]: 世界生命周期、SFXLibrary固定录音字幕/槽位和UAudioComponent真实播放状态。
 * [OUTPUT]: 每世界FIFO串行语音；音频开始才发布字幕/历史及关键事件类型，暂停与奖励冻结同步。
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
};
