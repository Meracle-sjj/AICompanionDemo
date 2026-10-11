/**
 * [INPUT]: CompanionVoiceSubsystem队列契约、真实音频状态与SFXLibrary固定录音字幕。
 * [OUTPUT]: 普通串行与最新紧急抢占；八类最高事件跨奖励冻结起播/续播，真实暂停一律冻结；起播预算/字幕/历史同刻。
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
	if (USFXLibrary::IsLimitedItemVoice(Event) && PlayedByEvent.FindRef(Event) >= 3)
	{
		++BudgetSkippedCount;
		return false;
	}
	if (USFXLibrary::IsInterruptingVoice(Event))
	{
		// 相同最高级也取最新事实；清除全部旧话，避免恢复后补播过时提醒。
		DiscardedPendingCount += Pending.Num();
		Pending.Reset();
		if (ActiveAudio) ++InterruptedCount;
		ClearActive();
	}
	FQueuedCompanionVoice& Item = Pending.AddDefaulted_GetRef();
	Item.Sound = Sound;
	Item.Event = Event;
	Item.Volume = Volume;
	Item.Location = Location;
	Item.bSpatial = bSpatial;
	if (!ActiveAudio) StartNext();
	return true;
}

EGameSFX UCompanionVoiceSubsystem::RegisterChestOpened()
{
	check(IsInGameThread());
	++SuccessfulChestCount;
	if (SuccessfulChestCount == 1) return EGameSFX::Voice_ChestOpen;
	if (SuccessfulChestCount == 2) return EGameSFX::Voice_ChestOpenSecond;
	if (SuccessfulChestCount == 3) return EGameSFX::Voice_ChestOpenThird;
	return EGameSFX::None;
}

bool UCompanionVoiceSubsystem::CanPlayVoice(EGameSFX Event) const
{
	const UWorld* World = GetWorld();
	return World && !World->IsPaused()
		&& (UGameplayStatics::GetGlobalTimeDilation(this) >= .01f || USFXLibrary::IsInterruptingVoice(Event));
}

void UCompanionVoiceSubsystem::StartNext()
{
	while (!ActiveAudio && !Pending.IsEmpty())
	{
		// 每次迭代先核真实队首事件；当前None不能挡紧急，紧急结束也不能偷播冻结中的普通词。
		if (!CanPlayVoice(Pending[0].Event)) return;
		const FQueuedCompanionVoice Item = Pending[0];
		Pending.RemoveAt(0, 1, EAllowShrinking::No);
		if (!IsValid(Item.Sound)) continue;
		// 排队不预占次数；即使同一种已排多句，第四次仍不能起播。
		if (USFXLibrary::IsLimitedItemVoice(Item.Event) && PlayedByEvent.FindRef(Item.Event) >= 3)
		{
			++BudgetSkippedCount;
			continue;
		}
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
		if (!IsValid(ActiveAudio) || !ActiveAudio->IsPlaying())
		{
			ClearActive();
			continue;
		}
		ActiveEvent = Item.Event;
		ActiveLine = USFXLibrary::GetVoiceLine(Item.Event);
		ActiveKind = USFXLibrary::GetVoiceKind(Item.Event);
		++StartedCount;
		++PlayedByEvent.FindOrAdd(Item.Event);
		for (TActorIterator<ACompanionCharacter> It(GetWorld()); It; ++It)
		{
			It->ShowSpeechBubble(ActiveLine, FMath::Max(.5f, Item.Sound->GetDuration()), ActiveKind,
				USFXLibrary::IsInterruptingVoice(Item.Event));
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
	const bool bShouldPause = !CanPlayVoice(ActiveEvent);
	if (IsValid(ActiveAudio) && bAudioPaused != bShouldPause)
	{
		ActiveAudio->SetPaused(bShouldPause);
		bAudioPaused = bShouldPause;
	}
	if (World->IsPaused() || (IsValid(ActiveAudio) && bShouldPause)) return;
	if (ActiveAudio && (!IsValid(ActiveAudio) || !ActiveAudio->IsPlaying()))
	{
		++CompletedCount;
		ClearActive();
	}
	if (!ActiveAudio) StartNext();
}

void UCompanionVoiceSubsystem::ClearActive()
{
	const bool bHadLine = !ActiveLine.IsEmpty();
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
	if (bHadLine && IsValid(GetWorld()))
		for (TActorIterator<ACompanionCharacter> It(GetWorld()); It; ++It)
		{
			It->ShowSpeechBubble(FString(), 0.f);
			break;
		}
}

void UCompanionVoiceSubsystem::Deinitialize()
{
	Pending.Reset();
	ClearActive();
	Attenuation = nullptr;
	PlayedByEvent.Reset();
	SuccessfulChestCount = 0;
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
	State->SetNumberField(TEXT("interrupted"), InterruptedCount);
	State->SetNumberField(TEXT("discarded_pending"), DiscardedPendingCount);
	State->SetNumberField(TEXT("budget_skipped"), BudgetSkippedCount);
	State->SetNumberField(TEXT("successful_chests"), SuccessfulChestCount);
	State->SetStringField(TEXT("event"), USFXLibrary::GetVoiceEventId(ActiveEvent).ToString());
	State->SetStringField(TEXT("line"), ActiveLine);
	State->SetNumberField(TEXT("kind"), static_cast<int32>(ActiveKind));
	State->SetStringField(TEXT("audio"), GetPathNameSafe(ActiveAudio));
	State->SetStringField(TEXT("sound"), IsValid(ActiveAudio) ? GetPathNameSafe(ActiveAudio->Sound) : FString());
	State->SetBoolField(TEXT("playing"), IsValid(ActiveAudio) && ActiveAudio->IsPlaying());
	State->SetBoolField(TEXT("paused"), bAudioPaused);
	State->SetBoolField(TEXT("continue_during_reward"), USFXLibrary::IsInterruptingVoice(ActiveEvent));
	State->SetBoolField(TEXT("next_can_start"), !Pending.IsEmpty() && CanPlayVoice(Pending[0].Event));
	TArray<TSharedPtr<FJsonValue>> Events;
	for (const FQueuedCompanionVoice& Item : Pending)
		Events.Add(MakeShared<FJsonValueString>(USFXLibrary::GetVoiceEventId(Item.Event).ToString()));
	State->SetArrayField(TEXT("queued_events"), Events);
	TSharedRef<FJsonObject> Counts = MakeShared<FJsonObject>();
	for (const TPair<EGameSFX, int32>& Pair : PlayedByEvent)
		Counts->SetNumberField(USFXLibrary::GetVoiceEventId(Pair.Key).ToString(), Pair.Value);
	State->SetObjectField(TEXT("played_by_event"), Counts);
	FString Json;
	FJsonSerializer::Serialize(State, TJsonWriterFactory<>::Create(&Json));
	return Json;
}
