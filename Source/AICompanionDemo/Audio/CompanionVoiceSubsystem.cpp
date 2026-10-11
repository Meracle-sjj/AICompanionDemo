/**
 * [INPUT]: CompanionVoiceSubsystem队列契约、真实音频状态与SFXLibrary固定录音字幕。
 * [OUTPUT]: 串行起播/气泡/历史、暂停恢复、只读JSON审计与离局清理。
 * [POS]: Audio/每世界语音所有者；不推进战斗、不保留事件调用者或已结束世界。
 * [PROTOCOL]: 变更时更新此头部，然后检查 CLAUDE.md
 */
#include "Audio/CompanionVoiceSubsystem.h"
#include "AICompanionDemo.h"
#include "AI/CompanionCharacter.h"
#include "Components/AudioComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"

bool UCompanionVoiceSubsystem::Enqueue(USoundBase* Sound, EGameSFX Event, float Volume,
	const FVector& Location, bool bSpatial)
{
	if (!IsValid(Sound) || Volume <= 0.f || USFXLibrary::GetVoiceLine(Event).IsEmpty()) return false;
	check(IsInGameThread());
	FQueuedCompanionVoice& Item = Pending.AddDefaulted_GetRef();
	Item.Sound = Sound;
	Item.Event = Event;
	Item.Volume = Volume;
	Item.Location = Location;
	Item.bSpatial = bSpatial;
	if (!ActiveAudio && !GetWorld()->IsPaused() && UGameplayStatics::GetGlobalTimeDilation(this) >= .01f)
		StartNext();
	return true;
}

void UCompanionVoiceSubsystem::StartNext()
{
	while (!ActiveAudio && !Pending.IsEmpty())
	{
		const FQueuedCompanionVoice Item = Pending[0];
		Pending.RemoveAt(0, 1, EAllowShrinking::No);
		if (!IsValid(Item.Sound)) continue;
		if (Item.bSpatial)
		{
			if (!Attenuation)
			{
				Attenuation = NewObject<USoundAttenuation>(this);
				Attenuation->Attenuation.bAttenuate = true;
				Attenuation->Attenuation.bSpatialize = true;
				Attenuation->Attenuation.DistanceAlgorithm = EAttenuationDistanceModel::Linear;
				Attenuation->Attenuation.AttenuationShape = EAttenuationShape::Sphere;
				Attenuation->Attenuation.AttenuationShapeExtents = FVector(400.f, 0.f, 0.f);
				Attenuation->Attenuation.FalloffDistance = 3600.f;
			}
			FVector Location = Item.Location;
			for (TActorIterator<ACompanionCharacter> It(GetWorld()); It; ++It)
			{
				Location = It->GetActorLocation() + FVector(0.f, 0.f, 60.f);
				break;
			}
			ActiveAudio = UGameplayStatics::SpawnSoundAtLocation(GetWorld(), Item.Sound, Location,
				FRotator::ZeroRotator, Item.Volume, 1.f, 0.f, Attenuation, nullptr, false);
		}
		else
		{
			ActiveAudio = UGameplayStatics::SpawnSound2D(GetWorld(), Item.Sound, Item.Volume,
				1.f, 0.f, nullptr, false, false);
		}
		if (!ActiveAudio) continue;
		ActiveEvent = Item.Event;
		ActiveLine = USFXLibrary::GetVoiceLine(Item.Event);
		ActiveKind = USFXLibrary::GetVoiceKind(Item.Event);
		++StartedCount;
		for (TActorIterator<ACompanionCharacter> It(GetWorld()); It; ++It)
		{
			It->ShowSpeechBubble(ActiveLine, FMath::Max(.5f, Item.Sound->GetDuration()), ActiveKind);
			break;
		}
		URunEventLog::Publish(GetWorld(), TEXT("队友"), ActiveLine,
			USFXLibrary::GetVoiceEventId(Item.Event), ActiveKind);
		UE_LOG(LogAICompanionDemo, Log, TEXT("[VoiceQueue] start=%lld event=%s pending=%d sound=%s text=%s"),
			StartedCount, *USFXLibrary::GetVoiceEventId(Item.Event).ToString(), Pending.Num(),
			*GetNameSafe(Item.Sound), *ActiveLine);
	}
}

void UCompanionVoiceSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UWorld* World = GetWorld();
	if (!World) return;
	const bool bShouldPause = World->IsPaused() || UGameplayStatics::GetGlobalTimeDilation(this) < .01f;
	if (IsValid(ActiveAudio) && bAudioPaused != bShouldPause)
	{
		ActiveAudio->SetPaused(bShouldPause);
		bAudioPaused = bShouldPause;
	}
	if (bShouldPause) return;
	if (ActiveAudio && (!IsValid(ActiveAudio) || !ActiveAudio->IsPlaying()))
	{
		++CompletedCount;
		ClearActive();
	}
	if (!ActiveAudio) StartNext();
}

void UCompanionVoiceSubsystem::ClearActive()
{
	if (IsValid(ActiveAudio))
	{
		ActiveAudio->Stop();
		ActiveAudio->DestroyComponent();
	}
	ActiveAudio = nullptr;
	ActiveLine.Reset();
	ActiveEvent = EGameSFX::None;
	ActiveKind = ERunEventKind::General;
	bAudioPaused = false;
}

void UCompanionVoiceSubsystem::Deinitialize()
{
	Pending.Reset();
	ClearActive();
	Attenuation = nullptr;
	Super::Deinitialize();
}

bool UCompanionVoiceSubsystem::DoesSupportWorldType(EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UCompanionVoiceSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UCompanionVoiceSubsystem, STATGROUP_Tickables);
}

FString UCompanionVoiceSubsystem::DebugVoiceState() const
{
	TSharedRef<FJsonObject> State = MakeShared<FJsonObject>();
	State->SetNumberField(TEXT("pending"), Pending.Num());
	State->SetNumberField(TEXT("started"), StartedCount);
	State->SetNumberField(TEXT("completed"), CompletedCount);
	State->SetStringField(TEXT("event"), USFXLibrary::GetVoiceEventId(ActiveEvent).ToString());
	State->SetStringField(TEXT("line"), ActiveLine);
	State->SetNumberField(TEXT("kind"), static_cast<int32>(ActiveKind));
	State->SetStringField(TEXT("audio"), GetPathNameSafe(ActiveAudio));
	State->SetStringField(TEXT("sound"), IsValid(ActiveAudio) ? GetPathNameSafe(ActiveAudio->Sound) : FString());
	State->SetBoolField(TEXT("playing"), IsValid(ActiveAudio) && ActiveAudio->IsPlaying());
	State->SetBoolField(TEXT("paused"), bAudioPaused);
	TArray<TSharedPtr<FJsonValue>> Events;
	for (const FQueuedCompanionVoice& Item : Pending)
		Events.Add(MakeShared<FJsonValueString>(USFXLibrary::GetVoiceEventId(Item.Event).ToString()));
	State->SetArrayField(TEXT("queued_events"), Events);
	FString Json;
	FJsonSerializer::Serialize(State, TJsonWriterFactory<>::Create(&Json));
	return Json;
}
